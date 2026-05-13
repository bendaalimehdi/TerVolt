#ifndef CHARGE_SESSION_H
#define CHARGE_SESSION_H

#include <Arduino.h>
#include <time.h>

class ChargeSession {
public:
    ChargeSession() : _isActive(false) {}

    String getFormattedTime() {
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            return "--:--:--";
        }
        char timeStringBuff[20];
        strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
        return String(timeStringBuff);
    }

    void start() {
        _startTime = millis();
        _energyAccumulatedWh = 0;
        _isActive = true;
        _startClockTime = getFormattedTime();
    }

    void stop() {
        _endTime = millis();
        _isActive = false;
        _endClockTime = getFormattedTime();
    }

    void update(float powerWatts) {
        if (!_isActive) return;

        // Calcul de l'énergie en Watt-heure (Wh)
        // powerWatts * (temps écoulé en heures)
        unsigned long now = millis();
        float elapsedHours = (now - _lastUpdate) / 3600000.0;
        
        if (_lastUpdate > 0) {
            _energyAccumulatedWh += powerWatts * elapsedHours;
        }
        _lastUpdate = now;
        _instantPowerKw = powerWatts / 1000.0;
    }

    // Getters pour le temps réel
    bool isActive() const { return _isActive; }
    float getInstantPowerKw() const { return _instantPowerKw; }
    float getSessionEnergyKwh() const { return _energyAccumulatedWh / 1000.0; }
    unsigned long getDurationSec() const { return _isActive ? (millis() - _startTime) / 1000 : (_endTime - _startTime) / 1000; }
    String getStartTime() const { return _startClockTime; }
    String getEndTime() const { return _endClockTime; }

private:
    bool _isActive;
    String _startClockTime;
    String _endClockTime;
    unsigned long _startTime, _endTime, _lastUpdate;
    float _energyAccumulatedWh;
    float _instantPowerKw;
    String _startClockTime, _endClockTime;
};

#endif