#include "config_manager.h"

ConfigManager::ConfigManager() {
    // Initialisation par défaut si LittleFS échoue
    data.deviceId = "TERVOLT-ESP32-GENERIC";
    data.maxAmps = 16;
    data.debugMode = false;
    data.num_leds = 1;
    data.temp_max_celsius = 75;
    
    // Initialisation des adresses de sondes vides
    data.probes.pcb_esp = "";
    data.probes.pcb_energy = "";
    data.probes.contacteur = "";
}

bool ConfigManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("❌ [ConfigManager] Erreur d'initialisation de LittleFS");
        return false;
    }

    if (!LittleFS.exists(_path)) {
        Serial.println("⚠️ [ConfigManager] Le fichier config.json n'existe pas. Création par défaut...");
        return save(); // Crée un fichier de base
    }

    File file = LittleFS.open(_path, "r");
    if (!file) {
        Serial.println("❌ [ConfigManager] Impossible d'ouvrir le fichier config.json en lecture");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.print("❌ [ConfigManager] Erreur de parsing du JSON: ");
        Serial.println(error.c_str());
        return false;
    }

    // --- PARSING SECTION : SYSTEM & NETWORK ---
    data.deviceId      = doc["system"]["device_id"] | "TERVOLT-ESP32-GENERIC";
    data.customer_name = doc["system"]["customer_name"] | "Inconnu";
    data.location      = doc["system"]["location"] | "Non localisé";
    data.debugMode     = doc["system"]["debug_mode"] | false;

    data.ssid          = doc["network"]["wifi_ssid"] | "";
    data.password      = doc["network"]["wifi_pass"] | "";
    data.ap_password   = doc["network"]["ap_password"] | "123456789";
    data.mqttServer    = doc["network"]["mqtt_server"] | "192.168.1.30";
    data.ota_password  = doc["network"]["ota_password"] | "admin";
    data.ntpServer     = doc["network"]["ntp_server"] | "pool.ntp.org";
    data.overcurrentThreshold = doc["network"]["overcurrent_threshold"] | 1.1;

    // --- PARSING SECTION : HARDWARE (PINS) ---
    data.pins.cp_pwm         = doc["hardware"]["pin_cp_pwm"] | -1;
    data.pins.cp_adc         = doc["hardware"]["pin_cp_adc"] | -1;
    data.pins.pp_adc         = doc["hardware"]["pin_pp_adc"] | -1;
    data.pins.relay          = doc["hardware"]["pin_relay"] | -1;
    data.pins.precharge      = doc["hardware"]["pin_precharge"] | -1;
    data.pins.feedback_relay = doc["hardware"]["pin_feedback_relay"] | -1;
    data.pins.temp_sensors   = doc["hardware"]["pin_temp_sensors"] | 18;
    data.pins.spi_sck        = doc["hardware"]["pin_spi_sck"] | -1;
    data.pins.spi_miso       = doc["hardware"]["pin_spi_miso"] | -1;
    data.pins.spi_mosi       = doc["hardware"]["pin_spi_mosi"] | -1;
    data.pins.rfid_ss        = doc["hardware"]["pin_rfid_ss"] | -1;
    data.pins.rfid_rst       = doc["hardware"]["pin_rfid_rst"] | -1;
    data.pins.energy_cs      = doc["hardware"]["pin_energy_cs"] | -1; // Fix: aligné avec le JSON
    data.pins.led_rgb        = doc["hardware"]["pin_led_rgb"] | -1;   // Fix: aligné avec le JSON
    data.pins.btn_config     = doc["hardware"]["pin_btn_config"] | -1;
    data.pins.pin_rcm_fault  = doc["hardware"]["pin_rcm_fault"] | 38;
    data.pins.pin_rcm_test   = doc["hardware"]["pin_rcm_test"] | 39;
    data.num_leds            = doc["hardware"]["num_leds"] | 6;       // Fix: aligné avec le JSON

    // --- PARSING SECTION : SAFETY & ELECTRICAL ---
    data.temp_max_celsius    = doc["safety"]["temp_max_celsius"] | 75;
    data.maxAmps             = doc["electrical"]["max_current_amps"] | 32; // Fix: déplacé de safety vers electrical
    data.cpDividerRatio      = doc["electrical"]["cpDividerRatio"] | 4.0;
    data.ventilationAvailable = doc["electrical"]["ventilationAvailable"] | true;

    // --- PARSING SECTION : PROBES (AUTO-APPRENTISSAGE) ---
    data.probes.pcb_esp    = doc["probes"]["pcb_esp"].as<String>();
    data.probes.pcb_energy = doc["probes"]["pcb_energy"].as<String>();
    data.probes.contacteur = doc["probes"]["contacteur"].as<String>();

    Serial.println("💾 [ConfigManager] Fichier config.json chargé avec succès.");
    return true;
}

bool ConfigManager::save() {
    File file = LittleFS.open(_path, "w");
    if (!file) {
        Serial.println("❌ [ConfigManager] Impossible d'ouvrir le fichier config.json en écriture");
        return false;
    }

    JsonDocument doc;

    // --- SÉRIALISATION : SYSTEM ---
    doc["system"]["device_id"]         = data.deviceId;
    doc["system"]["customer_name"]     = data.customer_name;
    doc["system"]["location"]          = data.location;
    doc["system"]["debug_mode"]        = data.debugMode;
    doc["system"]["version"]           = "1.0.0";
    doc["system"]["customer_id"]       = "001";
    doc["system"]["customer_category"] = "business";
    doc["system"]["customer_type"]     = "hotel";

    // --- SÉRIALISATION : NETWORK ---
    doc["network"]["wifi_ssid"]    = data.ssid;
    doc["network"]["wifi_pass"]    = data.password;
    doc["network"]["ap_password"]  = data.ap_password;
    doc["network"]["mqtt_server"]  = data.mqttServer;
    doc["network"]["mqtt_port"]    = 1883;
    doc["network"]["mqtt_user"]    = "tervolt_admin";
    doc["network"]["mqtt_pass"]    = "MqttAuthPwd";
    doc["network"]["ntp_server"]   = "pool.ntp.org";
    doc["network"]["ota_password"] = data.ota_password;

    // --- SÉRIALISATION : HARDWARE ---
    doc["hardware"]["pin_cp_pwm"]         = data.pins.cp_pwm;
    doc["hardware"]["pin_cp_adc"]         = data.pins.cp_adc;
    doc["hardware"]["pin_pp_adc"]         = data.pins.pp_adc;
    doc["hardware"]["pin_relay"]          = data.pins.relay;
    doc["hardware"]["pin_precharge"]      = data.pins.precharge;
    doc["hardware"]["pin_feedback_relay"] = data.pins.feedback_relay;
    doc["hardware"]["pin_temp_sensors"]   = data.pins.temp_sensors;
    doc["hardware"]["pin_spi_sck"]        = data.pins.spi_sck;
    doc["hardware"]["pin_spi_miso"]       = data.pins.spi_miso;
    doc["hardware"]["pin_spi_mosi"]       = data.pins.spi_mosi;
    doc["hardware"]["pin_rfid_ss"]        = data.pins.rfid_ss;
    doc["hardware"]["pin_rfid_rst"]       = data.pins.rfid_rst;
    doc["hardware"]["pin_energy_cs"]      = data.pins.energy_cs; 
    doc["hardware"]["pin_led_rgb"]        = data.pins.led_rgb;   
    doc["hardware"]["pin_btn_config"]     = data.pins.btn_config;
    doc["hardware"]["pin_rcm_fault"]      = data.pins.pin_rcm_fault;
    doc["hardware"]["pin_rcm_test"]       = data.pins.pin_rcm_test;
    doc["hardware"]["num_leds"]           = data.num_leds;

    // --- SÉRIALISATION : ELECTRICAL ---
    doc["electrical"]["max_current_amps"]     = data.maxAmps; // Fix: Stocké dans electrical
    doc["electrical"]["min_current_amps"]     = 6;
    doc["electrical"]["nominal_voltage"]      = 400;
    doc["electrical"]["phase_count"]          = 3;
    doc["electrical"]["ct_ratio"]             = 2000;
    doc["electrical"]["overcurrent_threshold"] = (float)data.maxAmps + 3.0f;
    doc["electrical"]["cpDividerRatio"]      = data.cpDividerRatio;
    doc["electrical"]["ventilationAvailable"] = data.ventilationAvailable;

    // --- SÉRIALISATION : SAFETY ---
    doc["safety"]["temp_max_celsius"]        = data.temp_max_celsius;
    doc["safety"]["ground_check"]            = true;
    doc["safety"]["lock_cable_during_charge"] = true;

    // --- SÉRIALISATION : PROBES ---
    doc["probes"]["pcb_esp"]    = data.probes.pcb_esp;
    doc["probes"]["pcb_energy"] = data.probes.pcb_energy;
    doc["probes"]["contacteur"] = data.probes.contacteur;

    if (serializeJson(doc, file) == 0) {
        Serial.println("❌ [ConfigManager] Échec de la sérialisation du JSON");
        file.close();
        return false;
    }

    file.close();
    Serial.println("💾 [ConfigManager] Fichier config.json sauvegardé sur LittleFS.");
    return true;
}