#include "server_manager.h"
#include <LittleFS.h>

ServerManager::ServerManager(Logger& logger, ConfigManager& config, EnergyManager& energy, ChargingManager& charger, OtaManager& otaManager,TemperatureManager& tempManager)
    : _logger(logger), _config(config), _energy(energy), _charger(charger), _otaManager(otaManager), _tempManager(tempManager), _client() {}

void ServerManager::begin(WiFiClient& espClient) {
    _client.setClient(espClient);
    _client.setServer(_config.data.mqttServer.c_str(), 1883);
    _client.setBufferSize(1024);
    _client.setCallback([this](char* topic, byte* payload, unsigned int length) {
        this->handleCallback(topic, payload, length);
    });
}

void ServerManager::initStorage() {
    if (!LittleFS.exists("/sessions")) {
        LittleFS.mkdir("/sessions");
        _logger.info("Dossier /sessions créé");
    }
}

// Sauvegarde locale en JSON
void ServerManager::saveSessionLocally(ChargeSession& s) {
    String path = "/sessions/" + s.getSessionId() + ".json";
    File file = LittleFS.open(path, "w");
    if (!file) {
        _logger.error("Impossible de créer le fichier session local");
        return;
    }

    JsonDocument doc;
    doc["type"] = "charge_session";
    doc["session_id"] = s.getSessionId();
    doc["from"] = _config.data.deviceId;
    doc["start_time"] = s.getStartTime();
    doc["end_time"] = s.getEndTime();
    doc["duration_sec"] = s.getDurationSec();
    doc["energy_kwh"] = s.getEnergyKwh();

    serializeJson(doc, file);
    file.close();
    _logger.success("[OK] Session sauvegardée localement : " + s.getSessionId());
}

// Synchronisation des sessions en attente
static bool _isSyncing = false;
void ServerManager::syncPendingSessions() {
    if (_isSyncing || !_client.connected()) return;

    _isSyncing = true;

    File root = LittleFS.open("/sessions");
    File file = root.openNextFile();
    while (file) {
        String path = file.name();
        if (path.endsWith(".json")) {
            _logger.info("[INFO] Tentative d'envoi session : " + path);
            String payload;
            while(file.available()) payload += (char)file.read();
            
            String topic = "tervolt/" + _config.data.deviceId + "/sessions";
            if (_client.publish(topic.c_str(), payload.c_str())) {
                _logger.info("[INFO] Session envoyée, en attente de l'ACK");
            }
        }
        file = root.openNextFile();
    }
    _isSyncing = false;
}

void ServerManager::reconnect() {
    if (!_client.connected()) {
        _logger.info("MQTT: tentative connexion à " + _config.data.mqttServer);

        if (_client.connect(_config.data.deviceId.c_str())) {
            _logger.success("MQTT Connecté : " + _config.data.deviceId);

            String topic = "tervolt/" + _config.data.deviceId;
            _client.subscribe(topic.c_str());
        } else {
            _logger.error("MQTT échec connexion, state=" + String(_client.state()));
        }
    }
}

void ServerManager::maintain() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    // Tentative de reconnexion espacée de 5 secondes au lieu de saturer le CPU
    if (!_client.connected()) {
        static unsigned long lastMqttRetry = 0;
        if (millis() - lastMqttRetry > 5000) {
            lastMqttRetry = millis();
            reconnect();
        }
    } else {
        _client.loop();
    }
}

bool ServerManager::publishFullStatus() {
    if (!isConnected()) return false;

    JsonDocument doc;
    doc["device_id"] = _config.data.deviceId;
    
    // Remplacement de now() par l'API POSIX standard pour le timestamp NTP
    doc["timestamp_unix"] = (uint32_t)time(nullptr); 

    // ─── 1. ÉTAT BINAIRE DES PINS SÉCURISÉ (Vérification != -1) ───
    JsonObject hwPins = doc["hardware_status"]["pins_state"].to<JsonObject>();
    
    hwPins["relay_main"] = (_config.data.pins.relay != -1) ? 
        (digitalRead(_config.data.pins.relay) ? "ON" : "OFF") : "UNCONFIGURED";
        
    hwPins["relay_precharge"] = (_config.data.pins.precharge != -1) ? 
        (digitalRead(_config.data.pins.precharge) ? "ON" : "OFF") : "UNCONFIGURED";
        
    hwPins["feedback_relay_glued"] = (_config.data.pins.feedback_relay != -1) ? 
        (digitalRead(_config.data.pins.feedback_relay) == LOW ? "FAULT" : "OK") : "UNCONFIGURED";
        
    hwPins["button_config_pressed"] = (_config.data.pins.btn_config != -1) ? 
        (digitalRead(_config.data.pins.btn_config) == LOW ? "PRESSED" : "RELEASED") : "UNCONFIGURED";

    // ─── 2. VALEURS ANALOGIQUES PROTÉGÉES (ADC) ───
    JsonObject hwAnalog = doc["hardware_status"]["analog_measurements"].to<JsonObject>();
    
    // Control Pilot (CP)
    if (_config.data.pins.cp_adc != -1) {
        int cpRaw = analogRead(_config.data.pins.cp_adc);
        hwAnalog["cp_adc_raw"]      = cpRaw;
        hwAnalog["cp_voltage_peak"] = _charger.getLatestPilotVoltage();
    } else {
        hwAnalog["cp_adc_raw"]      = -1;
        hwAnalog["cp_voltage_peak"] = 0.0f;
    }

    // Proximity Pilot (PP)
    if (_config.data.pins.pp_adc != -1) {
        int ppRaw = analogRead(_config.data.pins.pp_adc);
        hwAnalog["pp_adc_raw"]        = ppRaw;
        hwAnalog["pp_resistance_ohm"] = _charger.calculatePpResistance(ppRaw);
    } else {
        hwAnalog["pp_adc_raw"]        = -1;
        hwAnalog["pp_resistance_ohm"] = -1.0f;
    }
    hwAnalog["calculated_cable_max_amps"] = _config.data.maxAmps;

    // ─── 3. SANTÉ DE L'ESP32 ET CORRECTION RESET REASON ───
    JsonObject sysHealth = doc["system_health"].to<JsonObject>();
    sysHealth["free_heap_bytes"]     = ESP.getFreeHeap();
    sysHealth["min_free_heap_bytes"] = ESP.getMinFreeHeap();
    sysHealth["wifi_rssi_dbm"]       = WiFi.RSSI();
    
    // Utilisation de l'API moderne d'Espressif pour l'ESP32-S3
    sysHealth["reset_reason"]        = String((int)esp_reset_reason()); 

    // ─── 4. CAPTURE DU DERNIER LOG ───
    doc["latest_log"] = _logger.getLatestLog();

    String payload;
    serializeJson(doc, payload);

    String topic = "tervolt/" + _config.data.deviceId;

    if (_client.publish(topic.c_str(), payload.c_str())) {
        _logger.success("MQTT Télémétrie envoyée avec succès !");
        return true;
    } else {
        _logger.error("MQTT Télémétrie : Échec de l'envoi de l'enveloppe.");
        return false;
    }
}
bool ServerManager::isConnected() {
    return _client.connected();
}


// Gestion des messages reçus du serveur Python
void ServerManager::handleCallback(char* topic, byte* payload, unsigned int length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length)) return;

    // ─── 1. ACQUITTEMENT DE FIN DE SESSION ───────────────────────────────────
    if (doc["cmd"] == "session_ack" && doc["status"] == "ok") {
        String sid = doc["session_id"].as<String>();
        String path = "/sessions/" + sid + ".json";
        if (LittleFS.remove(path)) {
            _logger.success("[OK] ACK reçu. Session supprimée de LittleFS : " + sid);
        }
    }
    
    // ─── 2. COMMANDE DE DÉBLOCAGE APRÈS ARRÊT D'URGENCE ──────────────────────
    else if (doc["cmd"] == "unlock_evse") {
        _logger.warn("Commande de déverrouillage distante reçue (MQTT).");
        
        // Tentative de réinitialisation des défauts logiques et matériels
        _charger.resetFault();
        
        // Vérification passive du résultat de la réinitialisation
        if (_charger.isFault()) {
            _logger.error("❌ Échec du déverrouillage à distance : le défaut ou le collage persiste !");
        } else {
            _logger.success("✓ Borne déverrouillée à distance avec succès. Prête pour une nouvelle charge.");
        }
    }
    else if (doc["cmd"] == "calibrate_energy") {
        _logger.warn("Commande de calibration énergétique reçue (MQTT).");
        
        // Sécurité : On vérifie que les clés existent et ne sont pas nulles
        if (doc["voltage"].is<float>() && doc["current"].is<float>()) {
            float referenceVoltage = doc["voltage"].as<float>();
            float referenceCurrent = doc["current"].as<float>();

            // Lancement de la calibration via l'EnergyManager
            _energy.calibrate(referenceVoltage, referenceCurrent);
            _logger.success("✓ Calibration énergétique lancée avec les références : " + String(referenceVoltage) + "V, " + String(referenceCurrent) + "A");
        } else {
            _logger.error("❌ Échec calibration : paramètres 'voltage' ou 'current' manquants ou invalides dans le JSON.");
        }
    }

    else if (doc["cmd"] == "set_max_current") {
        if (doc["max_amps"].is<float>()) {
            float targetAmps = doc["max_amps"].as<float>();
            // Sécurité : On s'assure de respecter la plage minimale et maximale de la borne
            if (targetAmps >= 6.0f && targetAmps <= _config.data.maxAmps) {
                _charger.setMaxCurrent(targetAmps);
                _logger.success("✓ Puissance de charge ajustée par MQTT : " + String(targetAmps) + " A");
            } else {
                _logger.error("❌ Consigne d'intensité hors limites valides (6A - 32A)");
            }
        }
    }

    
    // ─── 3. COMMANDE DE MISE À JOUR LOGICIELLE CLOUD (OTA) ───────────────────
    else {
        const char* action = doc["action"];
        if (action != nullptr && strcmp(action, "ota_update") == 0) {
            String url = doc["url"].as<String>();
            String type = doc["type"].as<String>(); // "firmware" ou "filesystem"
            
            if (url.length() > 0 && type.length() > 0) {
                _logger.warn("ServerManager : Routage de la demande de mise à jour vers l'OtaManager.");
                _otaManager.updateFromCloud(url, type); 
            } else {
                _logger.error("ServerManager : Commande ota_update reçue mais 'url' ou 'type' manquant.");
            }
        }
    }
}