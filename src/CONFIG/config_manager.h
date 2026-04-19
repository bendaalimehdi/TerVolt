#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

struct PinConfig {
    int cp_pwm;
    int cp_adc;
    int relay;
    int led_red;
    int led_green;
    int rfid_ss;
    int rfid_rst;
};

struct ConfigData {
    String deviceId;
    String customer_name; 
    String location;
    String ssid;
    String password;
    String mqttServer;
    int maxAmps;
    bool debugMode;
    PinConfig pins;
};



class ConfigManager {
public:
    ConfigManager();
    bool begin();
    ConfigData data;
private:
    const char* _path = "/config.json";
};

#endif