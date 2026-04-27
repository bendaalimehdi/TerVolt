#include "wifi_manager.h"

WifiManager::WifiManager(Logger& logger, ConfigManager& config) 
    : _logger(logger), _config(config) {}

void WifiManager::begin() {
    String ssid = _config.data.ssid;
    String pass = _config.data.password;

    _logger.info("Tentative de connexion initiale au WiFi : " + ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(_config.data.ssid.c_str(), _config.data.password.c_str());

    int maxRetries = 3;  // Nombre de cycles de tentatives
    int attemptsPerRetry = 15; // Tentatives de 500ms par cycle

    for (int r = 0; r < maxRetries; r++) {
        _logger.info("Cycle de connexion " + String(r + 1) + "/" + String(maxRetries));
        
        for (int a = 0; a < attemptsPerRetry; a++) {
            if (isConnected()) {
                _logger.success("WiFi Connecté avec succès ! IP : " + getIP());
                return; // On sort dès que c'est bon
            }
            delay(500);
            Serial.print(".");
        }
        
        if (r < maxRetries - 1) {
            _logger.warn("\nÉchec du cycle, nouvelle tentative dans 2s...");
            WiFi.disconnect();
            delay(2000);
            WiFi.begin(_config.data.ssid.c_str(), _config.data.password.c_str());
        }
    }

    if (!isConnected()) {
        startAP(); // Passe en mode AP si échec après tous les cycles
        _apMode = true;
        _logger.error("\nImpossible de se connecter au WiFi après plusieurs essais.");
        _logger.info("Le système passera en mode reconnexion automatique (Background).");
    }
}


void WifiManager::maintain() {
    if (_apMode) return;
    if (!isConnected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = now;
            _logger.warn("Reconnexion WiFi en cours...");
            WiFi.disconnect();
            WiFi.begin(_config.data.ssid.c_str(), _config.data.password.c_str());

        }
    }
}

bool WifiManager::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

String WifiManager::getIP() {
    return WiFi.localIP().toString();
}

int WifiManager::getSignalStrength() {
    if (isConnected()) {
        
        return WiFi.RSSI(); // Retourne la valeur en dBm
    }
    return 0;
}

// Dans wifi_manager.cpp
void WifiManager::startAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(_apIP, _gateway, _subnet);
    // Le nom du réseau inclut l'ID de la borne pour la reconnaître
    String apName = "TerVolt-Config-" + WiFi.macAddress().substring(12);
    WiFi.softAP(apName.c_str(), "admin1234"); // Mot de passe par défaut
    _apMode = true;
    _logger.success("Mode AP activé. SSID: " + apName);
    _logger.info("IP Statique : 192.168.4.1");
}

void WifiManager::stopAP() {
    WiFi.softAPdisconnect(true);
}