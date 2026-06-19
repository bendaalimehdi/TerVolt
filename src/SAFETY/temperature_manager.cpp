#include "temperature_manager.h"
#include "driver/temperature_sensor.h"
#include <math.h>

TemperatureManager::TemperatureManager(Logger& logger, ConfigManager& config)
    : _logger(logger),
      _config(config),
      _oneWire(nullptr),
      _sensors(nullptr),
      _espTempSensor(NULL),
      _oneWirePin(18),
      _tempPcbEsp(NAN),
      _tempPcbEnergy(NAN),
      _tempContacteur(NAN),
      _tempESP(NAN),
      _lastReadTime(0),
      _readInterval(2000) {
}

TemperatureManager::~TemperatureManager() {
    if (_espTempSensor) {
        temperature_sensor_disable(_espTempSensor);
        temperature_sensor_uninstall(_espTempSensor);
        _espTempSensor = NULL;
    }
    if (_sensors) { delete _sensors; _sensors = nullptr; }
    if (_oneWire) { delete _oneWire; _oneWire = nullptr; }
}

// Convertit l'adresse binaire OneWire en String Hexadécimal pour la config
String TemperatureManager::addressToString(DeviceAddress deviceAddress) {
    String str = "";
    for (uint8_t i = 0; i < 8; i++) {
        if (deviceAddress[i] < 16) str += "0";
        str += String(deviceAddress[i], HEX);
    }
    str.toUpperCase();
    return str;
}

// Convertit le String Hexadécimal de la config en adresse binaire OneWire
bool TemperatureManager::stringToAddress(const String& addressStr, DeviceAddress deviceAddress) {
    if (addressStr.length() != 16) return false;
    for (uint8_t i = 0; i < 8; i++) {
        String byteStr = addressStr.substring(i * 2, (i * 2) + 2);
        deviceAddress[i] = (uint8_t)strtol(byteStr.c_str(), NULL, 16);
    }
    return true;
}

void TemperatureManager::begin() {
    // Initialisation du bus matériel OneWire sur le GPIO 18
    _oneWire = new OneWire(_oneWirePin);
    _sensors = new DallasTemperature(_oneWire);
    _sensors->begin();
    _sensors->setWaitForConversion(false);

    int sensorCount = _sensors->getDeviceCount();
    _logger.info("TemperatureManager : Bus OneWire scanné. Capteurs physiques trouvés : " + String(sensorCount));

    // Récupération des adresses stockées en configuration
    String savedEsp = _config.data.probes.pcb_esp;
    String savedEnergy = _config.data.probes.pcb_energy;
    String savedContacteur = _config.data.probes.contacteur;

    // ⚡ COURT-CIRCUIT DE L'APPRENTISSAGE : Si le profil thermique de la borne est déjà complet
    if (!savedEsp.isEmpty() && !savedEnergy.isEmpty() && !savedContacteur.isEmpty()) {
        _logger.success("TemperatureManager : Profil thermique validé dans config.json. Mode surveillance actif (Apprentissage ignoré).");
    } 
    // --- LOGIQUE D'AUTO-APPRENTISSAGE EN FABRICATION (Uniquement si configuration incomplète) ---
    else if (sensorCount > 0) {
        // Étape 1 : Seulement la carte ESP sous tension (1 seule sonde détectée)
        if (sensorCount == 1 && savedEsp.isEmpty()) {
            DeviceAddress addr;
            if (_sensors->getAddress(addr, 0)) {
                _config.data.probes.pcb_esp = addressToString(addr);
                _config.save();
                _logger.success("[USINE] Étape 1/3 réussie : Sonde PCB ESP32 enregistrée (" + _config.data.probes.pcb_esp + "). Coupez l'alimentation et branchez le PCB Energy.");
            }
        }
        // Étape 2 : PCB ESP + PCB Energy branchés (2 sondes détectées)
        else if (sensorCount == 2 && savedEnergy.isEmpty() && !savedEsp.isEmpty()) {
            for (int i = 0; i < 2; i++) {
                DeviceAddress addr;
                if (_sensors->getAddress(addr, i)) {
                    String currentAddr = addressToString(addr);
                    if (currentAddr != savedEsp) { // C'est la nouvelle sonde !
                        _config.data.probes.pcb_energy = currentAddr;
                        _config.save();
                        _logger.success("[USINE] Étape 2/3 réussie : Sonde PCB ENERGY SENSE enregistrée (" + _config.data.probes.pcb_energy + "). Coupez l'alimentation et branchez le Contacteur.");
                        break;
                    }
                }
            }
        }
        // Étape 3 : Les 3 sondes sont enfin branchées (3 sondes détectées)
        else if (sensorCount == 3 && savedContacteur.isEmpty() && !savedEsp.isEmpty() && !savedEnergy.isEmpty()) {
            for (int i = 0; i < 3; i++) {
                DeviceAddress addr;
                if (_sensors->getAddress(addr, i)) {
                    String currentAddr = addressToString(addr);
                    if (currentAddr != savedEsp && currentAddr != savedEnergy) { // C'est la dernière !
                        _config.data.probes.contacteur = currentAddr;
                        _config.save();
                        _logger.success("[USINE] Étape 3/3 réussie : Sonde CONTACTEUR enregistrée (" + _config.data.probes.contacteur + "). Configuration TerVolt terminée !");
                        break;
                    }
                }
            }
        }
    } else if (_config.data.debugMode) {
        _logger.warn("[DEV MODE] Aucune sonde DS18B20 connectée sur table. Apprentissage usine ignoré.");
    }

    // --- INITIALISATION DU CAPTEUR THERMIQUE INTERNE ESP32 ---
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    esp_err_t err = temperature_sensor_install(&temp_sensor_config, &_espTempSensor);

    if (err == ESP_OK) {
        err = temperature_sensor_enable(_espTempSensor);
        if (err == ESP_OK) {
            _logger.success("Capteur thermique interne ESP32 initialisé.");
        } else {
            _logger.error("Échec activation capteur thermique ESP32.");
            temperature_sensor_uninstall(_espTempSensor);
            _espTempSensor = NULL;
        }
    } else {
        _logger.error("Échec installation capteur thermique ESP32.");
        _espTempSensor = NULL;
    }
}

void TemperatureManager::update() {
    // Contrôle de la fréquence d'échantillonnage (toutes les 2 secondes)
    if (millis() - _lastReadTime < _readInterval) {
        return;
    }
    _lastReadTime = millis();

    if (_sensors) {
        DeviceAddress addr;

        // --- 1. LECTURE DES TEMPÉRATURES (Converties depuis le cycle précédent) ---
        
        // Lecture Ciblée 1 : PCB ESP
        if (stringToAddress(_config.data.probes.pcb_esp, addr)) {
            float t = _sensors->getTempC(addr);
            _tempPcbEsp = (t != DEVICE_DISCONNECTED_C) ? t : NAN;
        } else { _tempPcbEsp = NAN; }

        // Lecture Ciblée 2 : PCB Energy Sense (ATM90)
        if (stringToAddress(_config.data.probes.pcb_energy, addr)) {
            float t = _sensors->getTempC(addr);
            _tempPcbEnergy = (t != DEVICE_DISCONNECTED_C) ? t : NAN;
        } else { _tempPcbEnergy = NAN; }

        // Lecture Ciblée 3 : Côté Contacteur
        if (stringToAddress(_config.data.probes.contacteur, addr)) {
            float t = _sensors->getTempC(addr);
            _tempContacteur = (t != DEVICE_DISCONNECTED_C) ? t : NAN;
        } else { _tempContacteur = NAN; }

        // --- 2. REQUÊTE ASYNCHRONE POUR LE PROCHAIN CYCLE ---
        // Le capteur va travailler en arrière-plan pendant 750ms sans bloquer l'ESP32
        _sensors->requestTemperatures();
    }

    _tempESP = readInternalSiliconTemp();
}

float TemperatureManager::getPcbEspTemp()         { return _tempPcbEsp; }
float TemperatureManager::getPcbEnergyTemp()      { return _tempPcbEnergy; }
float TemperatureManager::getContacteurTemp()      { return _tempContacteur; }
float TemperatureManager::getInternalSiliconTemp() { return _tempESP; }

float TemperatureManager::readInternalSiliconTemp() {
    if (_espTempSensor == NULL) return NAN;
    float tsens_out = NAN;
    if (temperature_sensor_get_celsius(_espTempSensor, &tsens_out) == ESP_OK) {
        return tsens_out;
    }
    return NAN;
}

bool TemperatureManager::isOverheating() {
    float maxEspSiliconTemp = _config.data.temp_max_celsius;
    float maxBoardTemp      = 75.0f;   // Seuil max pour PCB ESP et PCB Energy Sense
    float maxContactorTemp  = 80.0f;   // Seuil critique d'échauffement sur le contacteur

    // ─── 1. GESTION DE L'ABSENCE OU DE LA PANNE DES SONDES DS18B20 ──────────────────
    if (isnan(_tempPcbEsp) || isnan(_tempPcbEnergy) || isnan(_tempContacteur)) {
        if (_config.data.debugMode) {
            // Mode DEV : On signale sans bloquer l'exécution ni couper les relais
            static unsigned long lastLog = 0;
            if (millis() - lastLog > 10000) { // Log non-bloquant toutes les 10 secondes
                _logger.warn("[DEV MODE] Une ou plusieurs sondes DS18B20 sont absentes ou déconnectées (NAN).");
                lastLog = millis();
            }
            // En mode dev, on ne renvoie pas true ici, on continue pour laisser la borne tourner
        } else {
            // Mode PROD : Sécurité maximale, arrêt d'urgence immédiat si un fil est coupé
            _logger.error("SÉCURITÉ CRITIQUE : Une ou plusieurs sondes DS18B20 ne répondent plus !");
            return true;
        }
    }

    // ─── 2. VÉRIFICATION DES SEUILS DE SURCHAUFFE RÉELS ─────────────────────────────
    // On utilise !isnan() pour s'assurer qu'on ne compare pas une valeur invalide en mode DEV

    // A. Surchauffe du microcontrôleur ESP32 (Sonde silicium interne ou dédiée)
    if (!isnan(_tempPcbEsp) && (_tempPcbEsp >= maxEspSiliconTemp)) {
        _logger.critical("🚨 SURCHAUFFE : PCB ESP32 à " + String(_tempPcbEsp) + "°C (Max: " + String(maxEspSiliconTemp) + "°C)");
        return true;
    }

    // B. Surchauffe de la carte de métrologie (ATM90E32 / Energy Sense)
    if (!isnan(_tempPcbEnergy) && (_tempPcbEnergy >= maxBoardTemp)) {
        _logger.critical("🚨 SURCHAUFFE : PCB Métrologie à " + String(_tempPcbEnergy) + "°C (Max: " + String(maxBoardTemp) + "°C)");
        return true;
    }

    // C. Surchauffe mécanique/électrique sur le contacteur de puissance
    if (!isnan(_tempContacteur) && (_tempContacteur >= maxContactorTemp)) {
        _logger.critical("🚨 SURCHAUFFE : Contacteur principal à " + String(_tempContacteur) + "°C (Max: " + String(maxContactorTemp) + "°C)");
        return true;
    }

    // Tout est nominal, aucune surchauffe détectée
    return false;
}