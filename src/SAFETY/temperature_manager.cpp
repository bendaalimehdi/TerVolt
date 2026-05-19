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

    int sensorCount = _sensors->getDeviceCount();
    _logger.info("TemperatureManager : Bus OneWire scanne. Capteurs physiques trouves : " + String(sensorCount));

    // --- LOGIQUE D'AUTO-APPRENTISSAGE EN FABRICATION (CYCLES ON/OFF) ---
    if (sensorCount > 0) {
        String savedEsp = _config.data.probes.pcb_esp;
        String savedEnergy = _config.data.probes.pcb_energy;
        String savedContacteur = _config.data.probes.contacteur;

        // Étape 1 : Seulement la carte ESP sous tension (1 seule sonde détectée)
        if (sensorCount == 1 && savedEsp.isEmpty()) {
            DeviceAddress addr;
            if (_sensors->getAddress(addr, 0)) {
                _config.data.probes.pcb_esp = addressToString(addr);
                _config.save();
                _logger.success("[USINE] Etape 1/3 reussie : Sonde PCB ESP32 enregistree (" + _config.data.probes.pcb_esp + "). Coupez l'alimentation et branchez le PCB Energy.");
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
                        _logger.success("[USINE] Etape 2/3 reussie : Sonde PCB ENERGY SENSE enregistree (" + _config.data.probes.pcb_energy + "). Coupez l'alimentation et branchez le Contacteur.");
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
                        _logger.success("[USINE] Etape 3/3 reussie : Sonde CONTACTEUR enregistree (" + _config.data.probes.contacteur + "). Configuration TerVolt terminee !");
                        break;
                    }
                }
            }
        }
    } else if (_config.data.debugMode) {
        _logger.warn("[DEV MODE] Aucune sonde DS18B20 connectee sur table. Apprentissage usine ignore.");
    }

    // --- INITIALISATION DU CAPTEUR THERMIQUE INTERNE ESP32 ---
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    esp_err_t err = temperature_sensor_install(&temp_sensor_config, &_espTempSensor);

    if (err == ESP_OK) {
        err = temperature_sensor_enable(_espTempSensor);
        if (err == ESP_OK) {
            _logger.success("Capteur thermique interne ESP32 initialise.");
        } else {
            _logger.error("Echec activation capteur thermique ESP32.");
            temperature_sensor_uninstall(_espTempSensor);
            _espTempSensor = NULL;
        }
    } else {
        _logger.error("Echec installation capteur thermique ESP32.");
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
        _sensors->requestTemperatures();

        DeviceAddress addr;

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
    float maxBoardTemp = 75.0f;       // Seuil max pour PCB ESP et PCB Energy Sense
    float maxContactorTemp = 80.0f;   // Seuil critique d'échauffement sur le contacteur

    // 1. Gestion de l'absence des sondes DS18B20
    if (isnan(_tempPcbEsp) || isnan(_tempPcbEnergy) || isnan(_tempContacteur)) {
        if (_config.data.debugMode) {
            // Mode DEV : On signale sans bloquer l'exécution ni couper les relais
            static unsigned long lastLog = 0;
            if (millis() - lastLog > 10000) { // Log non-bloquant toutes les 10 secondes
                _logger.warn("[DEV MODE] Une ou plusieurs sondes DS18B20 sont absentes ou déconnectées (NAN).");
                lastLog = millis();
            }
        } else {
            // Mode PROD : Sécurité maximale, arrêt d'urgence immédiat
            _logger.error("SECURITE : Une ou plusieurs sondes DS18B20 ne repondent plus !");
            return true;
        }
    }

    // 2. Gestion de l'absence du capteur interne de l'ESP32
    if (isnan(_tempESP)) {
        _logger.error("Erreur capteur temperature interne silicon ESP32.");
        return !_config.data.debugMode; // false en dev (non bloquant), true en prod (bloquant)
    }

    // ==========================================================
    // SURVEILLANCE DES SEUILS THERMIQUES PHYSIQUES
    // ==========================================================

    // Contrôle Silicon Interne ESP32
    if (!isnan(_tempESP) && _tempESP > maxEspSiliconTemp) {
        _logger.error("SURCHAUFFE CRITIQUE SILICON : ESP32 a " + String(_tempESP) + "°C");
        return true;
    }

    // Contrôle Température Ambiante Boîtier (PCB ESP)
    if (!isnan(_tempPcbEsp) && _tempPcbEsp > maxBoardTemp) {
        _logger.error("SURCHAUFFE CRITIQUE INTERNE : PCB ESP32 a " + String(_tempPcbEsp) + "°C");
        return true;
    }

    // Contrôle Échauffement Pistes de Mesure (PCB Energy ATM90)
    if (!isnan(_tempPcbEnergy) && _tempPcbEnergy > maxBoardTemp) {
        _logger.error("SURCHAUFFE CRITIQUE MESURE : PCB Energy a " + String(_tempPcbEnergy) + "°C");
        return true;
    }

    // Contrôle Échauffement Bornier de Puissance (Contacteur)
    if (!isnan(_tempContacteur) && _tempContacteur > maxContactorTemp) {
        _logger.error("SURCHAUFFE CRITIQUE PUISSANCE : Contacteur a " + String(_tempContacteur) + "°C");
        return true;
    }

    return false; // Tout est au frais, RAS !
}