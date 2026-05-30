#ifndef OVERCURRENT_MANAGER_H
#define OVERCURRENT_MANAGER_H

#include <Arduino.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

class OvercurrentManager {
public:
    OvercurrentManager(Logger& logger, ConfigManager& config);
    
    void begin();
    
    // Analyse l'intensité des 3 phases et retourne true en cas de défaut avéré
    bool check(float currentA, float currentB, float currentC);
    
    void reset();
    bool isFaultTriggered() const { return _faultTriggered; }

private:
    Logger&        _logger;
    ConfigManager& _config;

    bool          _faultTriggered;
    bool          _isThresholdExceeded;
    unsigned long _overcurrentStartTime;

    // Constantes de sécurité (Marge et Tolérance)
    static constexpr float SUSTAINED_MARGIN_AMPS   = 3.0f;   // Surcharge tolérée (+3A)
    static constexpr float CRITICAL_MARGIN_AMPS    = 10.0f;  // Pic critique direct (+10A)
    static constexpr unsigned long DEBOUNCE_MS     = 2000;   // Fenêtre de tolérance temporelle (2s)
};

#endif