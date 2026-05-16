#ifndef CHARGING_MANAGER_H
#define CHARGING_MANAGER_H

#include <Arduino.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

enum class ChargingState {
    STATE_A,    // Pas de véhicule       — CP ~12V
    STATE_B,    // Véhicule connecté     — CP ~9V
    STATE_C,    // Charge en cours       — CP ~6V
    STATE_D,    // Ventilation requise   — CP ~3V (non supporté)
    STATE_E,    // Erreur / court-circuit
    STATE_F     // Fault EVSE
};

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
    void forceEmergencyStop();

    // ON GARDE JUSTE LA DÉCLARATION (on enlève le { return ... })
    bool isAuthorized() const;
    bool isFault() const; 
    float getDutyCycle() const;
    ChargingState getState() const { return _state; }
    String getStateString() const;

private:
    Logger& _logger;
    ConfigManager& _config;
    
    int _pwmPin;
    int _adcPin;
    int _relayPin;
    int _prechargePin;
    int _feedbackRelayPin;

    bool checkRelayGlueFault();
    
    float _targetAmps;
    bool _isAuthorized = false;
    ChargingState _state = ChargingState::STATE_A;
    
    void setPWM(float dutyCycle); 
    float readPilotVoltage();     
};

#endif