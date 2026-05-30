#include "log.h"

Logger::Logger() : _latestLogStr("Système opérationnel"), _logMutex(NULL) {
    // ⚠️ NE PAS créer le mutex ici — le constructeur s'exécute avant FreeRTOS
}

void Logger::begin(unsigned long baud) {
    Serial.begin(baud);
    delay(200);

    // ✅ Création du mutex ici, dans setup(), quand FreeRTOS est prêt
    _logMutex = xSemaphoreCreateMutex();

    if (_logMutex == NULL) {
        Serial.println("🚨 [CRITICAL] Échec de création du Mutex du Logger !");
    } else {
        _log("[SYSTEM]", "Logger et Mutex initialisés avec succès.");
    }
}

String Logger::getTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 0)) {
        return "UPTIME-" + String(millis() / 1000) + "s";
    }
    char timeStringBuff[30];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}

void Logger::_log(String level, String message) {
    String timestamp = getTimestamp();
    String formattedLog = "[" + timestamp + "] " + level + " " + message;
    Serial.println(formattedLog);

    if (_logMutex != NULL && xSemaphoreTake(_logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        _latestLogStr = level + " " + message + " (at " + timestamp + ")";
        xSemaphoreGive(_logMutex);
    }
}

String Logger::getLatestLog() {
    String currentLog = "Aucun log disponible";
    if (_logMutex != NULL && xSemaphoreTake(_logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        currentLog = _latestLogStr;
        xSemaphoreGive(_logMutex);
    }
    return currentLog;
}

void Logger::info(String message)     { _log("[INFO]", message); }
void Logger::warn(String message)     { _log("[WARN] /!\\", message); }
void Logger::error(String message)    { _log("[ERROR] !!!", message); }
void Logger::critical(String message) { _log("[CRITICAL] !!!", message); }
void Logger::success(String message)  { _log("[OK]", message); }