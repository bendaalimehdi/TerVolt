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
void ServerManager::syncPendingSessions() {
    if (!_client.connected()) return;

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
    if (!_client.connected()) reconnect();
    _client.loop();
}

void ServerManager::publishFullStatus() {
    if (!isConnected()) return;

    JsonDocument doc;
    doc["device_id"] = _config.data.deviceId;
    doc["timestamp_unix"] = now(); // Si tu as synchronisé une RTC ou le NTP

    // 1. État binaire des Pins & Feedback matériels
    JsonObject hwPins = doc["hardware_status"]["pins_state"].to<JsonObject>();
    hwPins["relay_main"]             = digitalRead(_config.data.pins.relay) ? "ON" : "OFF";
    hwPins["relay_precharge"]        = digitalRead(_config.data.pins.precharge) ? "ON" : "OFF";
    hwPins["feedback_relay_glued"]   = digitalRead(_config.data.pins.feedback_relay) == LOW ? "FAULT" : "OK";
    hwPins["button_config_pressed"]  = digitalRead(_config.data.pins.btn_config) == LOW ? "PRESSED" : "RELEASED";

    // 2. Valeurs électriques brutes (ADC)
    JsonObject hwAnalog = doc["hardware_status"]["analog_measurements"].to<JsonObject>();
    
    // Control Pilot (CP)
    int cpRaw = analogRead(_config.data.pins.cp_adc);
    hwAnalog["cp_adc_raw"]           = cpRaw;
    hwAnalog["cp_voltage_peak"]      = _charger.getLatestPilotVoltage(); // Ta méthode de lecture

    // Proximity Pilot (PP)
    int ppRaw = analogRead(_config.data.pins.pp_adc);
    hwAnalog["pp_adc_raw"]           = ppRaw;
    hwAnalog["pp_resistance_ohm"]    = _charger.calculatePpResistance(ppRaw); // Calcul selon ton pont diviseur
    hwAnalog["calculated_cable_max_amps"] = _config.data.maxAmps;

    // 3. Santé de l'ESP32 (Diagnostic Watchdog / RAM)
    JsonObject sysHealth = doc["system_health"].to<JsonObject>();
    sysHealth["free_heap_bytes"]     = ESP.getFreeHeap();
    sysHealth["min_free_heap_bytes"] = ESP.getMinFreeHeap();
    sysHealth["wifi_rssi_dbm"]       = WiFi.RSSI();
    sysHealth["reset_reason"] = String(rtc_get_reset_reason(0));

    // 4. Extraction du dernier Log système
    doc["latest_log"] = _logger.getLatestLog(); // Nécessite d'ajouter un getter de chaîne dans ta classe Logger

    String payload;
    serializeJson(doc, payload);
 

    String topic = "tervolt/" + _config.data.deviceId;

    if (_client.publish(topic.c_str(), payload.c_str())) {
        _logger.success("MQTT Télémétrie envoyée !");
    } else {
        _logger.error("MQTT Télémétrie : Échec de l'envoi.");
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