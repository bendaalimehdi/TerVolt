#ifndef SERVER_MANAGER_H
#define SERVER_MANAGER_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"
#include  <esp_wifi.h>
#include  <TimeLib.h>
#include "../ENERGY/energy_manager.h"
#include "../CHARGING_MANAGER/charging_manager.h"
#include "../SAFETY/temperature_manager.h"
#include "ota_manager.h"
#include <rom/rtc.h>

class ServerManager {
public:
    ServerManager(Logger& logger, ConfigManager& config, EnergyManager& energy, ChargingManager& charger, OtaManager& otaManager,TemperatureManager& tempManager);
    void begin(WiFiClient& espClient);
    void maintain();
    void initStorage();
    void saveSessionLocally(ChargeSession& s);
    void syncPendingSessions();
    bool publishFullStatus();
    bool isConnected();
    

private:
    Logger& _logger;
    ConfigManager& _config;
    EnergyManager& _energy;
    ChargingManager& _charger;
    TemperatureManager& _tempManager;
    OtaManager& _otaManager;
    PubSubClient _client;
    unsigned long _lastReconnectAttempt = 0;

    void reconnect();
    void handleCallback(char* topic, byte* payload, unsigned int length);
};

#endif