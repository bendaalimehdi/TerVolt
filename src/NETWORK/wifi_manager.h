#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "../LOG/log.h"

class WifiManager {
public:
    WifiManager(Logger& logger);
    void connect(String ssid, String pass);
    void maintain(); // À appeler dans la loop
    bool isConnected();
    String getIP();
    int getSignalStrength(); 

private:
    Logger& _logger;
    String _ssid;
    String _pass;
    unsigned long _lastReconnectAttempt = 0;
    const unsigned long _reconnectInterval = 10000; // 10 secondes
};

#endif