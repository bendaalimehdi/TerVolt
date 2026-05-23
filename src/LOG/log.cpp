#include "log.h"

void Logger::begin(unsigned long baud) {
    Serial.begin(baud);
    delay(500);
    Serial.println("\n[SYSTEM] Logger Initialisé.");
}

void Logger::info(String message) { 
    _latestLogStr = "[INFO] " + message;
    Serial.println("[INFO] " + message); 
}

// C'EST CETTE PARTIE QUI MANQUAIT :
void Logger::warn(String message) { 
    _latestLogStr = "[WARN] /!\\ " + message;
    Serial.println("[WARN] /!\\ " + message); 
}

void Logger::error(String message) { 
    _latestLogStr = "[ERROR] !!! " + message + " !!!";
    Serial.println("[ERROR] !!! " + message + " !!!"); 
}

void Logger::critical(String message) { 
    _latestLogStr = "[CRITICAL] !!! " + message + " !!!";
    Serial.println("[CRITICAL] !!! " + message + " !!!"); 
}

void Logger::success(String message) { 
    _latestLogStr = "[OK] " + message;
    Serial.println("[OK] " + message); 
}