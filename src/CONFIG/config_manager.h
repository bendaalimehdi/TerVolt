#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

struct PinConfig {
    int cp_pwm;
    int cp_adc;
    int relay;
    int rfid_ss;
    int rfid_rst;
    int rcm_fault;
    int rcm_test;
    int led_rgb;
    int btn_config;
    int pzem_rx;
    int pzem_tx;
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
    int num_leds;
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