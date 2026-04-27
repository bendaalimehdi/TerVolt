#include "server_manager.h"

ServerManager::ServerManager(Logger& logger, ConfigManager& config) 
    : _logger(logger), _config(config), _client() {}

void ServerManager::begin(WiFiClient& espClient) {
    _client.setClient(espClient);
    _client.setServer(_config.data.mqttServer.c_str(), 1883);
    _client.setCallback(this->callback);
}

void ServerManager::reconnect() {
    if (!_client.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > 5000) {
            _lastReconnectAttempt = now;
            _logger.info("Tentative de connexion MQTT...");
            
            if (_client.connect(_config.data.deviceId.c_str())) {
                _logger.success("MQTT Connecté !");
                // On s'abonne au topic de commande spécifique à cette borne
                String topic = "tervolt/" + _config.data.deviceId + "/cmd";
                _client.subscribe(topic.c_str());
            } else {
                _logger.error("Échec MQTT, rc=" + String(_client.state()));
            }
        }
    }
}

void ServerManager::maintain() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!_client.connected()) reconnect();
    _client.loop();
}

void ServerManager::publishStatus(String payload) {
    String topic = "tervolt/" + _config.data.deviceId + "/status";
    _client.publish(topic.c_str(), payload.c_str());
}

bool ServerManager::isConnected() {
    return _client.connected();
}

// Gestion des messages reçus du serveur Python
void ServerManager::callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("[MQTT RECU] Topic: ");
    Serial.println(topic);
    // Ici, nous ajouterons plus tard la logique pour parser le JSON de commande
}