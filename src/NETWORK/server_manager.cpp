#include "server_manager.h"
#include <LittleFS.h>

ServerManager::ServerManager(Logger& logger, ConfigManager& config, EnergyManager& energy, ChargingManager& charger)
    : _logger(logger), _config(config), _energy(energy), _charger(charger), _client() {}

void ServerManager::begin(WiFiClient& espClient) {
    _client.setClient(espClient);
    _client.setServer(_config.data.mqttServer.c_str(), 1883);
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
    if (!_client.connected()) return;

    JsonDocument doc;
    doc["from"] = _config.data.deviceId; // Indique quelle borne parle
    doc["type"] = "status";
    doc["state"] = _charger.getStateString();
    
    // Mesures temps réel
    doc["v_a"] = _energy.getVoltageA();
    doc["i_a"] = _energy.getCurrentA();
    doc["v_b"] = _energy.getVoltageB();
    doc["i_b"] = _energy.getCurrentB();
    doc["v_c"] = _energy.getVoltageC();
    doc["i_c"] = _energy.getCurrentC(); 

    doc["p"] = _energy.activePowerTotal();
    

    // Données de session en cours
    if (_energy.session.isActive()) {
        doc["session_kwh"] = _energy.session.getSessionEnergyKwh();
        doc["duration"] = _energy.session.getDurationSec();
    }

    String buffer;
    serializeJson(doc, buffer);
    String topic = "tervolt/" + _config.data.deviceId;
    _client.publish(topic.c_str(), buffer.c_str());
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
}