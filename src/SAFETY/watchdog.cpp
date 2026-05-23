#include "watchdog.h"

WatchdogManager::WatchdogManager(Logger& logger, ConfigManager& config)
    : _logger(logger), _config(config), _timeout(5) {}

void WatchdogManager::begin(uint32_t timeoutSeconds) {
    _timeout = timeoutSeconds;

    if (_config.data.debugMode) {
        _logger.warn("[WATCHDOG] Mode Debug actif : Watchdog configuré de manière passive (pas de reset physique).");
    }

    // Configuration de la structure d'initialisation du TWDT (API ESP-IDF v5 utilisée par Arduino ESP32 v3.0)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = _timeout * 1000,
        .idle_core_mask = (1 << 0) | (1 << 1), // Surveille aussi les tâches IDLE des cœurs 0 et 1
        .trigger_panic = !_config.data.debugMode // Déclenche un panic/reset uniquement si on n'est pas en debug
    };

    // Initialisation ou reconfiguration du Watchdog
    esp_err_t err = esp_task_wdt_reconfigure(&wdt_config);
    
    if (err == ESP_OK) {
        _logger.success("[WATCHDOG] Initialisé avec succès (Timeout: " + String(_timeout) + "s)");
    } else {
        _logger.error("[WATCHDOG] Échec de la configuration. Code erreur: " + String(err));
    }
}

void WatchdogManager::registerCurrentTask() {
    // Récupère le handle de la tâche FreeRTOS qui appelle cette fonction
    TaskHandle_t currentTaskHandle = xTaskGetCurrentTaskHandle();
    
    esp_err_t err = esp_task_wdt_add(currentTaskHandle);
    
    if (err == ESP_OK) {
        _logger.info("[WATCHDOG] Tâche '" + String(pcTaskGetName(currentTaskHandle)) + "' enregistrée sous surveillance.");
    } else if (err == ESP_ERR_INVALID_ARG) {
        // La tâche est déjà enregistrée, rien à faire
    } else {
        _logger.error("[WATCHDOG] Impossible d'enregistrer la tâche. Erreur: " + String(err));
    }
}

void WatchdogManager::reset() {
    // Nourrit le chien de garde pour la tâche qui effectue l'appel
    esp_task_wdt_reset();
}