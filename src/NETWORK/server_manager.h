#ifndef SERVER_MANAGER_H
#define SERVER_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"
#include  <esp_wifi.h>

class ServerManager {
public:
    ServerManager(Logger& logger, ConfigManager& config);
    void begin(WiFiClient& espClient);
    void maintain();
    void publishStatus(String payload);
    bool isConnected();
    

private:
    Logger& _logger;
    ConfigManager& _config;
    PubSubClient _client;
    unsigned long _lastReconnectAttempt = 0;

    void reconnect();
    static void callback(char* topic, byte* payload, unsigned int length);
};

#endif