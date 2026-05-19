#ifndef RCM_MANAGER_H
#define RCM_MANAGER_H

#include <Arduino.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

class RcmManager {
public:
    RcmManager(Logger& logger, ConfigManager& config);
    
    // Initialise la pin et attache l'interruption
    void begin();
    
    // Retourne si un défaut de fuite est actuellement détecté
    bool isFaultTriggered() const;
    
    // Permet de réinitialiser le défaut après correction
    void resetFault();

    // Lance un test d'auto-contrôle du module RCM
    bool triggerSelfTest();

    // Fonction statique obligatoire pour le callback de l'interruption ISR
    static void IRAM_ATTR handleRcmInterrupt();

private:
    Logger& _logger;
    ConfigManager& _config;
    
    int _rcmPin;
    int _testPin;
    // volatile est crucial car cette variable est modifiée à l'intérieur de l'interruption (ISR)
    static volatile bool _faultTriggered; 
};

#endif