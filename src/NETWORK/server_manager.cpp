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
    if (!isConnected()) {
        _logger.warn("MQTT : Impossible de publier, client déconnecté.");
        return;
    }

    JsonDocument doc;

    // 1. Base
    doc["device_id"] = _config.data.deviceId;
    doc["from"] = _config.data.deviceId; 
    doc["uptime_s"] = millis() / 1000;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();

    // 2. Énergie Triphasée ( ATM90E32 ) - Utilise le même format que ton WebPortal
    doc["voltage_a"] = _energy.getVoltageA();
    doc["current_a"] = _energy.getCurrentA();
    doc["voltage_b"] = _energy.getVoltageB();
    doc["current_b"] = _energy.getCurrentB();
    doc["voltage_c"] = _energy.getVoltageC();
    doc["current_c"] = _energy.getCurrentC();
    doc["power"] = _energy.activePowerTotal();
    
    // Énergie de la session en cours
    doc["session_kwh"] = _energy.session.getSessionEnergyKwh();

    // 3. Logique de charge J1772
    doc["charge_state"] = _charger.getStateString();
    // Lecture physique de la pin du relais à la place de la méthode manquante
    doc["relay_status"] = digitalRead(_config.data.pins.relay) ? "ON" : "OFF";
    doc["max_allocated_amps"] = _config.data.maxAmps;

    // 4. Sécurité Thermique
    doc["temperature_esp"] = _tempManager.getInternalESPTemp();
    doc["temp_l1"] = _tempManager.getTerminalTemp(1);
    doc["temp_l2"] = _tempManager.getTerminalTemp(2);
    doc["temp_l3"] = _tempManager.getTerminalTemp(3);
    doc["overheating"] = _tempManager.isOverheating();

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

    if (doc["cmd"] == "session_ack" && doc["status"] == "ok") {
        String sid = doc["session_id"].as<String>();
        String path = "/sessions/" + sid + ".json";
        if (LittleFS.remove(path)) {
            _logger.success("[OK] ACK reçu. Session supprimée de LittleFS : " + sid);
        }
    }
    // À l'intérieur de votre méthode de traitement de commandes MQTT dans server_manager.cpp
    const char* action = doc["action"];
    if (action != nullptr && strcmp(action, "ota_update") == 0) {
        String url = doc["url"].as<String>();
        String type = doc["type"].as<String>(); // "firmware" ou "filesystem"
        
        if (url.length() > 0 && type.length() > 0) {
            _logger.warn("ServerManager : Routage de la demande de mise à jour vers l'OtaManager.");
            // Utilisation de la variable membre privée avec le "_"
            _otaManager.updateFromCloud(url, type); 
        } else {
            _logger.error("ServerManager : Commande ota_update reçue mais 'url' ou 'type' manquant.");
        }
    }
}