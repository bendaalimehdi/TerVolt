#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"
#include "../CHARGING_MANAGER/charging_manager.h"
#include "../ENERGY/energy_manager.h"
#include "../SAFETY/temperature_manager.h"

class ScreenManager {
public:
    ScreenManager(Logger& logger, ConfigManager& config, ChargingManager& charger, EnergyManager& energy, TemperatureManager& temp);
    
    void begin();
    void update(); 

private:
    Logger&             _logger;
    ConfigManager&      _config;
    ChargingManager&    _charger;
    EnergyManager&      _energy;
    TemperatureManager& _tempManager;

    // Instance pour un écran 1602 (16 colonnes, 2 lignes)
    // 0x27 est l'adresse I2C d'usine standard du PCF8574 (parfois 0x3F)
    LiquidCrystal_I2C* _lcd = nullptr; 

    unsigned long _lastDisplayUpdate = 0;
    const unsigned long _displayInterval = 1000; 

    // Compteur pour alterner les informations sur seulement 2 lignes disponibles
    uint8_t _displayCycle = 0;

    void drawDashboard();
    void drawEmergencyScreen(const String& reason);
};