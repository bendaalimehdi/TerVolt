#include "log.h"

void Logger::begin(unsigned long baud) {
    Serial.begin(baud);
    delay(500);
    Serial.println("\n[SYSTEM] Logger Initialisé.");
}

void Logger::info(String message) { 
    Serial.println("[INFO] " + message); 
}

// C'EST CETTE PARTIE QUI MANQUAIT :
void Logger::warn(String message) { 
    Serial.println("[WARN] /!\\ " + message); 
}

void Logger::error(String message) { 
    Serial.println("[ERROR] !!! " + message + " !!!"); 
}

void Logger::success(String message) { 
    Serial.println("[OK] " + message); 
}