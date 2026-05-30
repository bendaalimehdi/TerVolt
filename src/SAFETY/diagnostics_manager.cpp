#include "diagnostics_manager.h"
#include <LittleFS.h>
#include <esp_system.h>

DiagnosticsManager::DiagnosticsManager(Logger& logger, ConfigManager& config)
    : _logger(logger), _config(config), _mutex(NULL) {}

void DiagnosticsManager::begin() {
    _mutex = xSemaphoreCreateMutex();
    loadFromFlash();
    _logger.success("DiagnosticsManager : Système de monitoring et Ring Buffer initialisés.");
}

void DiagnosticsManager::incrementWifiReconnect() {
    if (_mutex != NULL && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _counters.wifi_reconnections++;
        xSemaphoreGive(_mutex);
    }
}

void DiagnosticsManager::incrementMqttReconnect() {
    if (_mutex != NULL && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _counters.mqtt_reconnections++;
        xSemaphoreGive(_mutex);
    }
}

void DiagnosticsManager::logFault(String type, String description, String timestamp) {
    if (_mutex != NULL && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        // 1. Incrémentation du compteur typé
        if (type == "RCM") _counters.rcm_faults++;
        else if (type == "OVERCURRENT") _counters.overcurrent_faults++;
        else if (type == "OVERHEATING") _counters.overheating_faults++;

        // 2. Écriture dans le Ring Buffer
        _history[_historyHead] = {timestamp, type, description};
        _historyHead = (_historyHead + 1) % MAX_FAULT_HISTORY;
        if (_historyCount < MAX_FAULT_HISTORY) _historyCount++;

        xSemaphoreGive(_mutex);
        
        // 3. Sauvegarde immédiate en Flash pour ne rien perdre en cas de coupure STEG
        saveToFlash();
    }
}

String DiagnosticsManager::getDiagnosticsJson() {
    JsonDocument doc; // Modèle d'allocation automatique ArduinoJson 7
    
    if (_mutex != NULL && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        
        // --- 1. MÉTRIQUES SYSTÈME ---
        JsonObject sys = doc["system"].to<JsonObject>();
        sys["uptime_sec"] = millis() / 1000;
        sys["free_heap_bytes"] = ESP.getFreeHeap();
        sys["min_free_heap_bytes"] = ESP.getMinFreeHeap();
        sys["reset_reason"] = (int)esp_reset_reason();
        
        // --- 2. COMPTEURS STATISTIQUES ---
        JsonObject cnt = doc["counters"].to<JsonObject>();
        cnt["wifi_reconnections"] = _counters.wifi_reconnections;
        cnt["mqtt_reconnections"] = _counters.mqtt_reconnections;
        cnt["rcm_faults"]         = _counters.rcm_faults;
        cnt["overcurrent_faults"] = _counters.overcurrent_faults;
        cnt["overheating_faults"] = _counters.overheating_faults;

        // --- 3. HISTORIQUE DÉPLIÉ DU TAMPON ---
        JsonArray hist = doc["fault_history"].to<JsonArray>();
        
        // Détermination de l'indice de départ du plus ancien au plus récent
        int index = (_historyCount == MAX_FAULT_HISTORY) ? _historyHead : 0;
        for (int i = 0; i < _historyCount; i++) {
            JsonObject entry = hist.add<JsonObject>();
            entry["timestamp"]   = _history[index].timestamp;
            entry["type"]        = _history[index].type;
            entry["description"] = _history[index].description;
            index = (index + 1) % MAX_FAULT_HISTORY;
        }

        xSemaphoreGive(_mutex);
    }

    String response;
    serializeJson(doc, response);
    return response;
}

void DiagnosticsManager::saveToFlash() {
    File file = LittleFS.open(_path, "w");
    if (!file) return;

    JsonDocument doc;
    doc["wifi_reconnections"] = _counters.wifi_reconnections;
    doc["mqtt_reconnections"] = _counters.mqtt_reconnections;
    doc["rcm_faults"]         = _counters.rcm_faults;
    doc["overcurrent_faults"] = _counters.overcurrent_faults;
    doc["overheating_faults"] = _counters.overheating_faults;

    serializeJson(doc, file);
    file.close();
}

void DiagnosticsManager::loadFromFlash() {
    if (!LittleFS.exists(_path)) return;

    File file = LittleFS.open(_path, "r");
    if (!file) return;

    JsonDocument doc;
    if (deserializeJson(doc, file) == DeserializationError::Ok) {
        _counters.wifi_reconnections = doc["wifi_reconnections"] | 0;
        _counters.mqtt_reconnections = doc["mqtt_reconnections"] | 0;
        _counters.rcm_faults         = doc["rcm_faults"] | 0;
        _counters.overcurrent_faults = doc["overcurrent_faults"] | 0;
        _counters.overheating_faults = doc["overheating_faults"] | 0;
    }
    file.close();
}