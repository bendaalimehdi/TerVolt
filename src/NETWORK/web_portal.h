#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"
#include "../CHARGING_MANAGER/charging_manager.h"
#include "../NETWORK/wifi_manager.h"
#include "../ENERGY/energy_manager.h"
#include "../ENERGY/charge_session.h"
#include "../SAFETY/temperature_manager.h"
#include "../SAFETY/diagnostics_manager.h"


class WebPortal {
public:
    WebPortal(Logger& logger, ConfigManager& config, ChargingManager& charger, WifiManager& wifi, EnergyManager& energy, TemperatureManager& tempManager);
    void begin();
    void handleDNS();

private:
    AsyncWebServer _server;
    DNSServer _dnsServer;
    bool _dnsStarted = false;
    IPAddress _dnsIP; 
    Logger& _logger;
    ConfigManager& _config;
    ChargingManager& _charger;
    WifiManager& _wifi;
    EnergyManager& _energy;
    TemperatureManager& _tempManager;
    String generateDashboard(); // Génère le HTML dynamique
    
};

#endif