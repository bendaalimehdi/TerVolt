#ifndef TEMPERATURE_MANAGER_H
#define TEMPERATURE_MANAGER_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "driver/temperature_sensor.h"
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

class TemperatureManager {
public:
    TemperatureManager(Logger& logger, ConfigManager& config);
    ~TemperatureManager();

    void begin();
    void update();

    // Accesseurs mis à jour avec tes nouveaux emplacements physiques
    float getPcbEspTemp();
    float getPcbEnergyTemp();
    float getContacteurTemp();
    float getInternalSiliconTemp();

    bool isOverheating();

private:
    Logger& _logger;
    ConfigManager& _config;

    int _oneWirePin;
    OneWire* _oneWire;
    DallasTemperature* _sensors;

    // Températures lues
    float _tempPcbEsp;
    float _tempPcbEnergy;
    float _tempContacteur;
    float _tempESP;

    unsigned long _lastReadTime;
    unsigned long _readInterval;

    temperature_sensor_handle_t _espTempSensor;

    // Méthodes privées utilitaires
    float readInternalSiliconTemp();
    String addressToString(DeviceAddress deviceAddress);
    bool stringToAddress(const String& addressStr, DeviceAddress deviceAddress);
};

#endif