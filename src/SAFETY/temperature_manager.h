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

    float getTerminalTemp(int phase);
    float getInternalESPTemp();

    bool isOverheating();

private:
    Logger& _logger;
    ConfigManager& _config;

    int _oneWirePin;
    OneWire* _oneWire;
    DallasTemperature* _sensors;

    float _tempL1;
    float _tempL2;
    float _tempL3;
    float _tempESP;

    unsigned long _lastReadTime;
    unsigned long _readInterval;

    temperature_sensor_handle_t _espTempSensor;

    float readInternalSiliconTemp();
};

#endif