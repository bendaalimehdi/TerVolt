#ifndef CHARGING_MANAGER_H
#define CHARGING_MANAGER_H

#include <Arduino.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

enum class ChargingState {
    STATE_A,  // Libre         — Vcp > 10.5 V
    STATE_B,  // Connecté      — 8.0 < Vcp < 10.5 V
    STATE_C,  // Charge active — 5.0 < Vcp < 8.0 V
    STATE_D,  // Ventilation   — 2.0 < Vcp < 5.0 V
    STATE_E,  // Erreur
    STATE_F   // Fault EVSE (Panne matérielle interne)
};
 
enum class PrechargeStep {
    IDLE,
    WAITING_PRECHARGE,   // Attend PRECHARGE_DELAY_MS (condensateurs voiture)
    WAITING_RELAY_CLOSE, // Attend RELAY_SETTLE_MS (enclenchement mécanique)
    DONE
};
 
class ChargingManager {
public:
    ChargingManager(Logger& logger, ConfigManager& config);
    void begin();
    void update();
 
    void  setMaxCurrent(float amps);
    float getDutyCycle()   const;
    bool  isVehicleConnected();
    bool  isVehicleRequestingCharge();
    bool  isAuthorized()   const;
    void  setAuthorized(bool auth);
    bool  isFault()        const;
    bool  isCharging();
    bool  checkRelayGlueFault(bool forceLiveRead = false);
    void  forceEmergencyStop();
    void  resetFault();
    String getStateString() const;
    ChargingState getState() const { return _state; }
    float getLatestPilotVoltage();
    float calculatePpResistance(int adcRaw);
 
private:
    Logger&        _logger;
    ConfigManager& _config;
 
    uint8_t _pwmPin;
    uint8_t _adcPin;
    uint8_t _relayPin;
    uint8_t _prechargePin;
    uint8_t _feedbackRelayPin;
 
    float         _targetAmps     = 0.0f;
    ChargingState _state          = ChargingState::STATE_A;
 
    ChargingState  _lastRawState;
    unsigned long  _lastChangeTime;
 
    PrechargeStep  _prechargeSubState;
    unsigned long  _prechargeStartTime;
    unsigned long  _relayCloseTime;
 
    // Constantes de timing (en ms)
    static constexpr unsigned long CONFIRM_DELAY_MS   = 150;  // Debounce CP
    static constexpr unsigned long PRECHARGE_DELAY_MS = 500;  // Condensateurs voiture

    void  setPWM(float dutyCyclePercent);
    float readPilotVoltage();
    void  tickPrecharge(bool requiresVentilation);
    void  _enterFault(ChargingState faultState); // Signature unifiée avec paramètre
    
    bool  _cachedRelayCommandedOn = false;
    bool  _cachedRelayPhysicallyClosed = false;
    float _latestVcp = 12.0f; 
    bool  _authorized = false;
};

#endif