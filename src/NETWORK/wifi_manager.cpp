#include "wifi_manager.h"

WifiManager::WifiManager(Logger& logger, ConfigManager& config) 
    : _logger(logger), _config(config) {}

// begin() lance la connexion SANS bloquer.
// La connexion effective est gérée par maintain() dans la boucle de la tâche.
void WifiManager::begin() {
    _logger.info("Tentative de connexion initiale au WiFi : " + _config.data.ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(_config.data.ssid.c_str(), _config.data.password.c_str());
    _lastReconnectAttempt = millis();
    _connectStartTime = millis();
    _isConnecting = true;
}

void WifiManager::maintain() {
    if (_apMode) return;

    // Phase de connexion initiale : on attend jusqu'à CONNECTION_TIMEOUT_MS
    if (_isConnecting) {
        if (isConnected()) {
            _isConnecting = false;
            _retryCount = 0;
            _logger.success("WiFi Connecté ! IP : " + getIP());
            return;
        }

        // Timeout de connexion initiale
        if (millis() - _connectStartTime > CONNECTION_TIMEOUT_MS) {
            _isConnecting = false;
            _retryCount++;
            _logger.warn("WiFi : Timeout connexion (tentative " + String(_retryCount) + "/" + String(MAX_RETRIES) + ")");

            if (_retryCount >= MAX_RETRIES) {
                _logger.error("WiFi : Impossible de se connecter après " + String(MAX_RETRIES) + " tentatives. Mode AP activé.");
                startAP();
                return;
            }

            // Nouvelle tentative non-bloquante
            WiFi.disconnect();
            WiFi.begin(_config.data.ssid.c_str(), _config.data.password.c_str());
            _connectStartTime = millis();
            _isConnecting = true;
        }
        return;
    }

    // Phase de maintien : reconnexion périodique si déconnecté
    if (!isConnected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = now;
            _logger.warn("WiFi : Reconnexion en arrière-plan...");
            WiFi.disconnect();
            WiFi.begin(_config.data.ssid.c_str(), _config.data.password.c_str());
            _connectStartTime = millis();
            _isConnecting = true;
        }
    }
}

bool WifiManager::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

String WifiManager::getIP() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    if (_apMode || (WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA)) {
        return WiFi.softAPIP().toString();
    }
    return "0.0.0.0";
}

int WifiManager::getSignalStrength() {
    if (isConnected()) return WiFi.RSSI();
    return 0;
}

void WifiManager::startAP() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(_apIP, _gateway, _subnet);

    String apName     = "TerVolt-Config-" + _config.data.deviceId;
    String apPassword = _config.data.ap_password;

    if (apPassword.length() < 8) {
        apPassword = "AdminTerVolt2026!";
        _logger.warn("Mot de passe AP invalide. Utilisation du mot de passe de secours.");
    }

    WiFi.softAP(apName.c_str(), apPassword.c_str());
    _apMode = true;
    _logger.success("Mode AP activé. SSID: " + apName + " | IP: 192.168.4.1");

    // Tentative de connexion STA en parallèle
    WiFi.begin(_config.data.ssid.c_str(), _config.data.password.c_str());
}

void WifiManager::stopAP() {
    WiFi.softAPdisconnect(true);
    _apMode = false;
}