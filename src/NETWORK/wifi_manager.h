#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"
#include <time.h>

class WifiManager {
public:
    WifiManager(Logger& logger, ConfigManager& config);
    void begin();
    void maintain();
    bool isConnected();
    String getIP();
    int getSignalStrength();
    void startAP();
    void stopAP();

private:
    Logger& _logger;
    ConfigManager& _config;

    // Reconnexion en arrière-plan
    unsigned long _lastReconnectAttempt = 0;
    const unsigned long _reconnectInterval = 10000; // 10s entre deux tentatives de maintien

    // Connexion initiale non-bloquante
    static constexpr unsigned long CONNECTION_TIMEOUT_MS = 5000; // 5s par tentative
    static constexpr int MAX_RETRIES = 3;
    bool _isConnecting = false;
    unsigned long _connectStartTime = 0;
    int _retryCount = 0;

    // Mode AP
    IPAddress _apIP     = IPAddress(192, 168, 4, 1);
    IPAddress _gateway  = IPAddress(192, 168, 4, 1);
    IPAddress _subnet   = IPAddress(255, 255, 255, 0);
    bool _apMode = false;
};

#endif