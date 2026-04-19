#include "wifi_manager.h"

WifiManager::WifiManager(Logger& logger) : _logger(logger) {}

void WifiManager::connect(String ssid, String pass) {
    _ssid = ssid;
    _pass = pass;

    _logger.info("Tentative de connexion initiale au WiFi : " + _ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid.c_str(), _pass.c_str());

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
            WiFi.begin(_ssid.c_str(), _pass.c_str());
        }
    }

    if (!isConnected()) {
        _logger.error("\nImpossible de se connecter au WiFi après plusieurs essais.");
        _logger.info("Le système passera en mode reconnexion automatique (Background).");
    }
}

void WifiManager::maintain() {
    if (!isConnected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = now;
            _logger.warn("Reconnexion WiFi en cours...");
            WiFi.disconnect();
            WiFi.begin(_ssid.c_str(), _pass.c_str());
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