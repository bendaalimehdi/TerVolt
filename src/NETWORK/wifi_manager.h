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
    void maintain(); // À appeler dans la loop
    bool isConnected();
    String getIP();
    int getSignalStrength(); 
    void startAP(); 
    void stopAP();
    void setupTime(); 

private:
    Logger& _logger;
    ConfigManager& _config;
    unsigned long _lastReconnectAttempt = 0;
    const unsigned long _reconnectInterval = 10000; // 10 secondes

    IPAddress _apIP = IPAddress(192, 168, 4, 1);
    IPAddress _gateway = IPAddress(192, 168, 4, 1);
    IPAddress _subnet = IPAddress(255, 255, 255, 0);
    bool _apMode = false; // Indique si on est en mode AP
};

#endif