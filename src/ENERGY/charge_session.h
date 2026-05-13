#ifndef CHARGE_SESSION_H
#define CHARGE_SESSION_H

#include <Arduino.h>
#include <time.h>

class ChargeSession {
public:
    ChargeSession();

    // Gestion du cycle de vie
    void start(String deviceId);
    void stop();
    void update(float powerWatts);

    // Getters de données
    bool isActive() const { return _isActive; }
    String getSessionId() const { return _sessionId; }
    String getStartTime() const { return _startClockTime; }
    String getEndTime() const { return _endClockTime; }
    float getEnergyKwh() const { return _energyAccumulatedWh / 1000.0f; }
    unsigned long getDurationSec() const { return _durationSec; }
    float getSessionEnergyKwh() const;

private:
    bool _isActive;
    unsigned long _startTime;
    unsigned long _endTime;
    unsigned long _lastUpdate;
    unsigned long _durationSec;

    float _energyAccumulatedWh;
    float _instantPowerKw;

    String _sessionId;
    String _startClockTime;
    String _endClockTime;

    String getFormattedTime();
};

#endif