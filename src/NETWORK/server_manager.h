#ifndef SERVER_MANAGER_H
#define SERVER_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"
#include  <esp_wifi.h>
#include "../ENERGY/energy_manager.h"
#include "../CHARGING_MANAGER/charging_manager.h"

class ServerManager {
public:
    ServerManager(Logger& logger, ConfigManager& config, EnergyManager& energy, ChargingManager& charger);
    void begin(WiFiClient& espClient);
    void maintain();
    void initStorage();
    void saveSessionLocally(ChargeSession& s);
    void syncPendingSessions();
    void publishFullStatus();
    bool isConnected();
    

private:
    Logger& _logger;
    ConfigManager& _config;
    EnergyManager& _energy;
    ChargingManager& _charger;
    PubSubClient _client;
    unsigned long _lastReconnectAttempt = 0;

    void reconnect();
    void handleCallback(char* topic, byte* payload, unsigned int length);
};

#endif