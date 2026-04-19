#include "config_manager.h"

ConfigManager::ConfigManager() {}

bool ConfigManager::begin() {
    if (!LittleFS.begin(true)) return false;

    File file = LittleFS.open(_path, "r");
    if (!file) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) return false;

    // Mapping des données (exemple sur quelques champs)
    data.deviceId = doc["system"]["device_id"].as<String>();
    data.customer_name = doc["system"]["customer_name"].as<String>(); 
    data.location = doc["system"]["location"].as<String>();
    data.ssid = doc["network"]["wifi_ssid"].as<String>();
    data.password = doc["network"]["wifi_pass"].as<String>();
    data.mqttServer = doc["network"]["mqtt_server"].as<String>();
    data.maxAmps = doc["electrical"]["max_current_amps"].as<int>();
    data.debugMode = doc["system"]["debug_mode"].as<bool>();

    // --- Mapping Hardware Pins ---
    data.pins.cp_pwm = doc["hardware"]["pin_cp_pwm"].as<int>();
    data.pins.cp_adc = doc["hardware"]["pin_cp_adc"].as<int>();
    data.pins.relay = doc["hardware"]["pin_relay"].as<int>();
    data.pins.led_red = doc["hardware"]["pin_led_red"].as<int>();
    data.pins.led_green = doc["hardware"]["pin_led_green"].as<int>();
    data.pins.rfid_ss = doc["hardware"]["pin_rfid_ss"].as<int>();
    data.pins.rfid_rst = doc["hardware"]["pin_rfid_rst"].as<int>();

    file.close();
    return true;
}