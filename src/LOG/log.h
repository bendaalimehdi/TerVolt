#ifndef LOG_H
#define LOG_H

#include <Arduino.h>

class Logger {
public:
    void begin(unsigned long baud = 115200);
    void info(String message);
    void warn(String message);
    void error(String message);
    void success(String message);
    void critical(String message);
    String getLatestLog() const { return _latestLogStr; }  
private:
    String getTimestamp(); // Pourrait utiliser le NTP plus tard
    String _latestLogStr = "Système opérationnel";
};

#endif