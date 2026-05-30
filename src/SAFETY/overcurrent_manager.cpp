#include "overcurrent_manager.h"

OvercurrentManager::OvercurrentManager(Logger& logger, ConfigManager& config)
    : _logger(logger), _config(config), _faultTriggered(false), 
      _isThresholdExceeded(false), _overcurrentStartTime(0) {}

void OvercurrentManager::begin() {
    reset();
    _logger.success("OvercurrentManager : Protection contre les surintensités triphasées initialisée.");
}

void OvercurrentManager::reset() {
    _faultTriggered = false;
    _isThresholdExceeded = false;
    _overcurrentStartTime = 0;
}

bool OvercurrentManager::check(float currentA, float currentB, float currentC) {
    // Si le système est déjà verrouillé en sécurité, on maintient le signal d'arrêt
    if (_faultTriggered) {
        return true;
    }

    float maxAllowed = (float)_config.data.maxAmps;
    float sustainedThreshold = _config.data.overcurrentThreshold;
    float criticalThreshold  = maxAllowed + CRITICAL_MARGIN_AMPS;

    // ─── BARRIÈRE 1 : PIC CRITIQUE INSTANTANÉ (Sécurité matérielle directe) ───
    if (currentA >= criticalThreshold || currentB >= criticalThreshold || currentC >= criticalThreshold) {
        _faultTriggered = true;
        _logger.critical("🚨 SURINTENSITÉ CRITIQUE : Seuil instantané dépassé ! Arrêt matériel immédiat ! "
                         "(L1: " + String(currentA) + "A, L2: " + String(currentB) + "A, L3: " + String(currentC) + "A)");
        return true;
    }

    // ─── BARRIÈRE 2 : SURCHARGE PROLONGÉE (Tolérance temporelle anti-parasite) ───
    bool exceedsSustained = (currentA > sustainedThreshold || 
                             currentB > sustainedThreshold || 
                             currentC > sustainedThreshold);

    if (exceedsSustained) {
        if (!_isThresholdExceeded) {
            // Premier dépassement détecté : on arme le chronomètre interne
            _isThresholdExceeded = true;
            _overcurrentStartTime = millis();
            _logger.warn("[SURCHARGE DETECTÉE] Intensité hors limite nominale. Début du compte à rebours de sécurité (2s).");
        } else {
            // La surcharge persiste : on vérifie si l'on a franchi le cap des 2 secondes
            if (millis() - _overcurrentStartTime >= DEBOUNCE_MS) {
                _faultTriggered = true;
                _logger.critical("🚨 SURINTENSITÉ PROLONGÉE : Courant excessif maintenu pendant 2s ! Déclenchement sécurité. "
                                 "(L1: " + String(currentA) + "A, L2: " + String(currentB) + "A, L3: " + String(currentC) + "A)");
                return true;
            }
        }
    } else {
        // Le courant est redescendu sous le seuil d'alerte : réinitialisation transparente de la fenêtre
        if (_isThresholdExceeded) {
            _isThresholdExceeded = false;
            _overcurrentStartTime = 0;
            _logger.info("[SÉCURITÉ] Courant triphasé stabilisé. Fenêtre d'alerte effacée.");
        }
    }

    return _faultTriggered;
}