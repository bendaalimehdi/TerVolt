#pragma once
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

class WatchdogManager {
public:
    WatchdogManager(Logger& logger, ConfigManager& config);
    
    // Initialise le Watchdog global avec un timeout (ex: 5 secondes)
    void begin(uint32_t timeoutSeconds = 5);
    
    // Enregistre la tâche FreeRTOS courante auprès du Watchdog
    void registerCurrentTask();
    
    // Réinitialise le compteur (Nourrit le Watchdog pour la tâche courante)
    void reset();

private:
    Logger&        _logger;
    ConfigManager& _config;
    uint32_t       _timeout;
};