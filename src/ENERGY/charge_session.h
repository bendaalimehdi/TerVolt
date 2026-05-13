#ifndef CHARGE_SESSION_H
#define CHARGE_SESSION_H

#include <Arduino.h>
#include <time.h>

class ChargeSession {
public:
    ChargeSession();

    String getFormattedTime();

    void start();
    void stop();
    void update(float powerWatts);

    bool isActive() const;
    float getInstantPowerKw() const;
    float getSessionEnergyKwh() const;
    unsigned long getDurationSec() const;

    String getStartTime() const { return _startClockTime; }
    String getEndTime() const { return _endClockTime; }

private:
    bool _isActive;
    unsigned long _startTime;
    unsigned long _endTime;
    unsigned long _lastUpdate;

    float _energyAccumulatedWh;
    float _instantPowerKw;

    String _startClockTime;
    String _endClockTime;
};

#endif