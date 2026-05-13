#include "charge_session.h"

ChargeSession::ChargeSession() {
    _isActive = false;
    _startTime = 0;
    _endTime = 0;
    _lastUpdate = 0;
    _energyAccumulatedWh = 0.0;
    _instantPowerKw = 0.0;
    _startClockTime = "--:--";
    _endClockTime = "--:--";
}

void ChargeSession::start() {
    _startTime = millis();
    _lastUpdate = _startTime;
    _energyAccumulatedWh = 0.0;
    _isActive = true;
    _instantPowerKw = 0.0;
    
    // Note : Pour l'heure réelle, il faudra appeler une fonction NTP 
    // ou récupérer l'heure système de l'ESP32
    _startClockTime = "Session Démarrée"; 
}

void ChargeSession::stop() {
    if (!_isActive) return;
    
    _endTime = millis();
    _isActive = false;
    _endClockTime = "Session Terminée";
}

void ChargeSession::update(float powerWatts) {
    if (!_isActive) {
        _instantPowerKw = 0.0;
        return;
    }

    unsigned long now = millis();
    
    // Calcul de l'intervalle de temps écoulé en heures
    // (now - _lastUpdate) est en millisecondes
    // 3 600 000 ms = 1 heure
    float elapsedHours = (float)(now - _lastUpdate) / 3600000.0f;

    // Mise à jour de l'énergie accumulée (Puissance * Temps)
    if (powerWatts > 0) {
        _energyAccumulatedWh += (powerWatts * elapsedHours);
    }

    _instantPowerKw = powerWatts / 1000.0f;
    _lastUpdate = now;
}

float ChargeSession::getSessionEnergyKwh() const {
    return _energyAccumulatedWh / 1000.0f;
}

unsigned long ChargeSession::getDurationSec() const {
    if (!_isActive) {
        if (_startTime == 0) return 0;
        return (_endTime - _startTime) / 1000;
    }
    return (millis() - _startTime) / 1000;
}

float ChargeSession::getInstantPowerKw() const {
    return _instantPowerKw;
}

bool ChargeSession::isActive() const {
    return _isActive;
}

String ChargeSession::getFormattedTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "--:--:--";
    }

    char timeStringBuff[20];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}