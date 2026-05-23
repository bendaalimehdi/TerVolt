#ifndef ENERGY_MANAGER_H
#define ENERGY_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <ATM90E32.h>
#include "charge_session.h"

#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

class EnergyManager {
public:
    EnergyManager(Logger& logger, ConfigManager& config);

    void begin();
    void update();

    void calibrate(float referenceVoltage, float referenceCurrent);

    float activePowerTotal() const { return _activePowerTotal; }

    float getVoltageA() const { return _voltageA; }
    float getVoltageB() const { return _voltageB; }
    float getVoltageC() const { return _voltageC; }

    float getCurrentA() const { return _currentA; }
    float getCurrentB() const { return _currentB; }
    float getCurrentC() const { return _currentC; }

    float getActivePowerA() const { return _activePowerA; }
    float getActivePowerB() const { return _activePowerB; }
    float getActivePowerC() const { return _activePowerC; }

    ChargeSession session;

private:
    Logger& _logger;
    ConfigManager& _config;
    ATM90E32 _atm90;

    float _voltageA = 0.0f;
    float _voltageB = 0.0f;
    float _voltageC = 0.0f;

    float _currentA = 0.0f;
    float _currentB = 0.0f;
    float _currentC = 0.0f;

    float _activePowerA = 0.0f;
    float _activePowerB = 0.0f;
    float _activePowerC = 0.0f;
    float _activePowerTotal = 0.0f;

    unsigned long _lastRead = 0;
};

#endif