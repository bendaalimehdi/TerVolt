#include "rcm_manager.h"

volatile bool RcmManager::_faultTriggered = false;

RcmManager::RcmManager(Logger& logger, ConfigManager& config) 
    : _logger(logger), _config(config), _rcmPin(-1), _testPin(-1) {}

void RcmManager::begin() {
    // Récupération des vraies pins depuis ton config.json
    _rcmPin = _config.data.pins.pin_rcm_fault; // GPIO 38
    _testPin = _config.data.pins.pin_rcm_test;   // GPIO 39
    
    if (_rcmPin == -1 || _testPin == -1) {
        _logger.error("RcmManager : Broches pin_rcm_fault ou pin_rcm_test non configurées.");
        return;
    }

    // 1. Configuration de la pin de défaut (Entrée)
    pinMode(_rcmPin, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(_rcmPin), RcmManager::handleRcmInterrupt, RISING);
    
    // 2. Configuration de la pin de Test (Sortie)
    pinMode(_testPin, OUTPUT);
    digitalWrite(_testPin, LOW); // Maintenu à l'état bas en fonctionnement normal
    
    _logger.success("RcmManager : Surveillance RCM active (Fault: GPIO " + String(_rcmPin) + ", Test: GPIO " + String(_testPin) + ")");
}

void IRAM_ATTR RcmManager::handleRcmInterrupt() {
    _faultTriggered = true;
    // Coupure matérielle d'urgence directe si nécessaire
}

bool RcmManager::isFaultTriggered() const {
    return _faultTriggered;
}

// Nouvelle méthode : Permet à l'ESP32 de tester le module RCM au démarrage
bool RcmManager::triggerSelfTest() {
    _logger.warn("RcmManager : Lancement de l'auto-test du capteur de fuite...");
    
    // On s'assure qu'aucun défaut n'est présent avant le test
    _faultTriggered = false; 
    
    // Envoi d'une impulsion sur la pin Test pour simuler une fuite de courant interne au RCM
    digitalWrite(_testPin, HIGH);
    delay(50); // Le module a généralement besoin de 20 à 40ms pour réagir
    digitalWrite(_testPin, LOW);
    
    // On attend un court instant pour laisser l'interruption se déclencher
    delay(20);
    
    if (_faultTriggered) {
        _logger.success("RcmManager : Auto-test RÉUSSI. Le capteur a bien détecté la fausse fuite.");
        _faultTriggered = false; // On nettoie le faux défaut généré par le test
        return true;
    } else {
        _logger.critical("RcmManager : DANGER ! L'auto-test a ÉCHOUÉ. Le capteur ne répond pas.");
        return false;
    }
}

void RcmManager::resetFault() {
    if (digitalRead(_rcmPin) == LOW) {
        _faultTriggered = false;
        _logger.success("RcmManager : Défaut RCM réinitialisé.");
    } else {
        _logger.error("RcmManager : Impossible de réinitialiser, la fuite est toujours là.");
    }
}