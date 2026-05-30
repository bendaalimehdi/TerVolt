#ifndef LOG_H
#define LOG_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

class Logger {
public:
    Logger();
    void begin(unsigned long baud = 115200);
    
    void info(String message);
    void warn(String message);
    void error(String message);
    void success(String message);
    void critical(String message);
    
    // Le getter perd son attribut "const" pour permettre la manipulation sécurisée du Mutex
    String getLatestLog();  

private:
    String getTimestamp();
    void _log(String level, String message);

    String _latestLogStr;
    SemaphoreHandle_t _logMutex; // Mutex de protection Cross-Core
};

#endif