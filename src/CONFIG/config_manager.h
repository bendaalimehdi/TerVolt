#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

struct ProbeConfig {
    String pcb_esp;
    String pcb_energy;
    String contacteur;
};

struct PinConfig {
    int cp_pwm;
    int cp_adc;
    int pp_adc;      
    int relay;
    int precharge;
    int feedback_relay;
    int temp_sensors;
    int spi_sck;     
    int spi_miso;    
    int spi_mosi;    
    int rfid_ss;
    int rfid_rst;
    int energy_cs;   
    int led_rgb;
    int btn_config;
    int pin_rcm_fault;
    int pin_rcm_test;
};

struct ConfigData {
    String deviceId;
    String customer_name; 
    String location;
    String ssid;
    String password;
    String ap_password;
    String ota_password;
    String mqttServer;
    String ntpServer;
    int maxAmps;
    float overcurrentThreshold;
    bool debugMode;
    int num_leds;
    int temp_max_celsius;
    ProbeConfig probes;
    PinConfig pins;
    float cpDividerRatio;        // Ratio réel du pont diviseur CP (ex: 4.0)
    bool  ventilationAvailable;  // La borne est-elle installée dans un local ventilé ?
};



class ConfigManager {
public:
    ConfigManager();
    bool begin();
    bool save();
    ConfigData data;
private:
    const char* _path = "/config.json";
};

#endif