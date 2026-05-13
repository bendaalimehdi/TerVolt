#include "charge_session.h"
#include <Arduino.h>

/**
 * Constructeur : Initialise une session vierge.
 */
ChargeSession::ChargeSession() {
    _isActive = false;
    _startTime = 0;
    _endTime = 0;
    _lastUpdate = 0;
    _energyAccumulatedWh = 0.0f;
    _instantPowerKw = 0.0f;
    _durationSec = 0;
    _sessionId = "";
    _startClockTime = "";
    _endClockTime = "";
}

/**
 * Démarre une nouvelle session de charge.
 * @param deviceId ID de la borne pour générer le session_id unique.
 */
void ChargeSession::start(String deviceId) {
    _startTime = millis();
    _lastUpdate = _startTime;
    _energyAccumulatedWh = 0.0f;
    _durationSec = 0;
    _isActive = true;
    
    // Génération du session_id : ID_BORNE-TIMESTAMP
    // Note: Utilise le temps Unix si NTP est prêt, sinon millis
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &timeinfo);
        _sessionId = deviceId + "-" + String(buf);
        _startClockTime = getFormattedTime();
    } else {
        _sessionId = deviceId + "-" + String(_startTime);
        _startClockTime = "NTP_NON_SYNC";
    }
}

/**
 * Arrête la session actuelle et fige les données.
 */
void ChargeSession::stop() {
    if (!_isActive) return;
    
    _endTime = millis();
    _durationSec = (_endTime - _startTime) / 1000;
    _endClockTime = getFormattedTime();
    _isActive = false;
}

/**
 * Intègre la puissance mesurée pour calculer l'énergie consommée.
 * @param powerWatts Puissance totale instantanée lue par l'ATM90E32.
 */
void ChargeSession::update(float powerWatts) {
    if (!_isActive) {
        _instantPowerKw = 0.0f;
        return;
    }

    unsigned long now = millis();
    // Calcul de l'intervalle de temps depuis la dernière mise à jour (en heures)
    float elapsedHours = (float)(now - _lastUpdate) / 3600000.0f;

    // Ajout de l'énergie (W * h)
    if (powerWatts > 0) {
        _energyAccumulatedWh += (powerWatts * elapsedHours);
    }

    _instantPowerKw = powerWatts / 1000.0f;
    _durationSec = (now - _startTime) / 1000;
    _lastUpdate = now;
}

float ChargeSession::getSessionEnergyKwh() const {
    return _energyAccumulatedWh / 1000.0f;
}

/**
 * Retourne l'heure actuelle formatée via le système NTP de l'ESP32.
 */
String ChargeSession::getFormattedTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "2026-05-13 00:00:00"; // Fallback
    }

    char timeStringBuff[25];
    // Format attendu par le serveur : YYYY-MM-DD HH:MM:SS
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}