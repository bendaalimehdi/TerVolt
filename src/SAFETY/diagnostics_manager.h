#ifndef DIAGNOSTICS_MANAGER_H
#define DIAGNOSTICS_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

struct DiagnosticCounters {
    uint32_t wifi_reconnections = 0;
    uint32_t mqtt_reconnections = 0;
    uint32_t rcm_faults = 0;
    uint32_t overcurrent_faults = 0;
    uint32_t overheating_faults = 0;
};

struct FaultEvent {
    String timestamp;
    String type;
    String description;
};

class DiagnosticsManager {
public:
    DiagnosticsManager(Logger& logger, ConfigManager& config);
    void begin();
    
    // Événements réseau (Incrémentation depuis le Cœur 0)
    void incrementWifiReconnect();
    void incrementMqttReconnect();
    
    // Événements de pannes (Capturés depuis le Cœur 1)
    void logFault(String type, String description, String timestamp);

    // Extraction sous forme de chaîne JSON pour l'API Web et MQTT
    String getDiagnosticsJson();
    
    // Persistance sur la mémoire Flash
    void saveToFlash();
    void loadFromFlash();

private:
    Logger& _logger;
    ConfigManager& _config;
    
    DiagnosticCounters _counters;
    
    // Paramètres du tampon circulaire d'historique des fautes
    static const int MAX_FAULT_HISTORY = 10;
    FaultEvent _history[MAX_FAULT_HISTORY];
    int _historyCount = 0;
    int _historyHead = 0;

    SemaphoreHandle_t _mutex; // Protection de la mémoire Cross-Core
    const char* _path = "/diagnostics.json";
};

#endif