#include "server_manager.h"

ServerManager::ServerManager(Logger& logger, ConfigManager& config, EnergyManager& energy, ChargingManager& charger)
    : _logger(logger), _config(config), _energy(energy), _charger(charger), _client() {}

void ServerManager::begin(WiFiClient& espClient) {
    _client.setClient(espClient);
    _client.setServer(_config.data.mqttServer.c_str(), 1883);
    _client.setCallback([this](char* topic, byte* payload, unsigned int length) {
        this->handleCallback(topic, payload, length);
    });
}

void ServerManager::reconnect() {
    if (!_client.connected()) {
        if (_client.connect(_config.data.deviceId.c_str())) {
            _logger.success("MQTT Connecté : " + _config.data.deviceId);
            
            // Topic spécifique : tervolt/TERVOLT-00X
            String topic = "tervolt/" + _config.data.deviceId;
            _client.subscribe(topic.c_str()); 
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
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) return;

    // Vérification du destinataire dans le JSON
    String target = doc["target_id"].as<String>();
    if (target != "" && target != _config.data.deviceId) {
        return; // Ce message ne m'est pas destiné
    }

    // Analyse de la commande
    String cmd = doc["cmd"].as<String>();
    if (cmd == "set_current") {
        float val = doc["value"].as<float>();
        _charger.setMaxCurrent(val); // Modifie le PWM en temps réel
        _logger.success("MQTT: Courant ajusté à " + String(val) + "A par le serveur");
    }
}