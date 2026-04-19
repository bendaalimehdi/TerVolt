#ifndef CHARGING_MANAGER_H
#define CHARGING_MANAGER_H

#include <Arduino.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

class ChargingManager {
public:
    ChargingManager(Logger& logger, ConfigManager& config);
    void begin();
    void update(); 
    
    void setMaxCurrent(float amps);
    void enableCharge(bool enable);
    bool isVehicleConnected();
    bool isVehicleRequestingCharge();
    bool isCharging();

    // ON GARDE JUSTE LA DÉCLARATION (on enlève le { return ... })
    bool isAuthorized() const;
    bool isFault() const; 
    float getDutyCycle() const;

private:
    Logger& _logger;
    ConfigManager& _config;
    
    int _pwmPin;
    int _adcPin;
    int _relayPin;
    
    float _targetAmps;
    bool _isAuthorized = false; // Initialisation par défaut
    
    void setPWM(float dutyCycle); 
    float readPilotVoltage();     
};

#endif