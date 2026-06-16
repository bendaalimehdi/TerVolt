#include "wifi_manager.h"

WifiManager::WifiManager(Logger& logger, ConfigManager& config) 
    : _logger(logger), _config(config), _apMode(false), _isConnecting(false), _retryCount(0) {}

void WifiManager::begin() {
    _logger.info("Démarrage du Wi-Fi TerVolt en mode Hybride (AP + STA)");
    
    // 📡 FIX : On démarre en AP_STA dès le début pour s'assurer que le point d'accès 
    // de configuration est prêt à accueillir le portail captif immédiatement.
    WiFi.mode(WIFI_AP_STA);
    
    // Initialisation par défaut du Point d'Accès de secours en parallèle
    startAP();

    _logger.info("Tentative de connexion en tâche de fond au réseau : " + _config.data.ssid);
    WiFi.begin(_config.data.ssid.c_str(), _config.data.password.c_str());
    _lastReconnectAttempt = millis();
    _connectStartTime = millis();
    _isConnecting = true;
}

void WifiManager::maintain() {
    // Si l'AP est actif, on laisse s'exécuter la tâche (le DNS est traité par webPortal dans main.cpp)
    // On ne bloque pas avec un "return;" si _apMode est vrai, afin de permettre au mode Station (STA)
    // de continuer à chercher le réseau de l'hôtel en arrière-plan.

    // Phase de connexion initiale : on attend jusqu'à CONNECTION_TIMEOUT_MS
    if (_isConnecting) {
        if (isConnected()) {
            _isConnecting = false;
            _retryCount = 0;
            _logger.success("WiFi de l'infrastructure connecté ! IP : " + getIP());
            return;
        }

        // Timeout de la tentative de connexion courante
        if (millis() - _connectStartTime > CONNECTION_TIMEOUT_MS) {
            _isConnecting = false;
            _retryCount++;
            _logger.warn("WiFi : Timeout connexion (tentative " + String(_retryCount) + "/" + String(MAX_RETRIES) + ")");

            if (_retryCount >= MAX_RETRIES) {
                _logger.warn("WiFi : Impossible de joindre l'infrastructure. Le mode AP reste le seul actif.");
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

    // Phase de maintien : reconnexion périodique automatique si déconnecté de l'hôtel
    if (!isConnected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = now;
            _logger.warn("WiFi : Tentative de reconnexion automatique à l'infrastructure...");
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
    return WiFi.softAPIP().toString();
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
    }

    WiFi.softAP(apName.c_str(), apPassword.c_str());
    _apMode = true;
    _logger.success("Réseau AP Local Émis — SSID: " + apName + " | IP Directe: " + WiFi.softAPIP().toString());
}

void WifiManager::stopAP() {
    WiFi.softAPdisconnect(true);
    _apMode = false;
}