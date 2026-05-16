#include "config_manager.h"

ConfigManager::ConfigManager() {}

bool ConfigManager::begin() {
    if (!LittleFS.begin(true)) return false;

    File file = LittleFS.open(_path, "r");
    if (!file) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    if (error) {
        file.close();
        return false;
    }

    // --- System & Network ---
    data.deviceId = doc["system"]["device_id"].as<String>();
    data.customer_name = doc["system"]["customer_name"].as<String>(); 
    data.location = doc["system"]["location"].as<String>();
    data.ssid = doc["network"]["wifi_ssid"].as<String>();
    data.password = doc["network"]["wifi_pass"].as<String>();
    data.mqttServer = doc["network"]["mqtt_server"].as<String>();
    
    // --- Electrical ---
    data.maxAmps = doc["electrical"]["max_current_amps"].as<int>();
    data.debugMode = doc["system"]["debug_mode"].as<bool>();
    data.num_leds = doc["hardware"]["num_leds"].as<int>();

    // --- Mapping Hardware Pins ---
    data.pins.cp_pwm = doc["hardware"]["pin_cp_pwm"].as<int>();
    data.pins.cp_adc = doc["hardware"]["pin_cp_adc"].as<int>();
    data.pins.pp_adc = doc["hardware"]["pin_pp_adc"].as<int>();
    data.pins.relay = doc["hardware"]["pin_relay"].as<int>();
    data.pins.precharge = doc["hardware"]["pin_precharge"].as<int>();
    
    data.pins.spi_sck = doc["hardware"]["pin_spi_sck"].as<int>();
    data.pins.spi_miso = doc["hardware"]["pin_spi_miso"].as<int>();
    data.pins.spi_mosi = doc["hardware"]["pin_spi_mosi"].as<int>();
    
    data.pins.rfid_ss = doc["hardware"]["pin_rfid_ss"].as<int>();
    data.pins.rfid_rst = doc["hardware"]["pin_rfid_rst"].as<int>();
    data.pins.energy_cs = doc["hardware"]["pin_energy_cs"].as<int>();
    
    data.pins.led_rgb = doc["hardware"]["pin_led_rgb"].as<int>();
    data.pins.btn_config = doc["hardware"]["pin_btn_config"].as<int>();
    data.pins.rcm_fault = doc["hardware"]["pin_rcm_fault"].as<int>();
    data.pins.rcm_test = doc["hardware"]["pin_rcm_test"].as<int>();

    file.close();
    return true;
}