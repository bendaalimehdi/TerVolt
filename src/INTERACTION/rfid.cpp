#include "rfid.h"

RfidManager::RfidManager(Logger& logger, ConfigManager& config) 
    : _logger(logger), _config(config), 
      _mfrc522(config.data.pins.rfid_ss, config.data.pins.rfid_rst) {}

void RfidManager::begin() {
    SPI.begin();
    _mfrc522.PCD_Init();
    _logger.success("Lecteur RFID initialisé.");
}

String RfidManager::update() {
    // Vérifier si une nouvelle carte est présente
    if (!_mfrc522.PICC_IsNewCardPresent() || !_mfrc522.PICC_ReadCardSerial()) {
        return "";
    }

    String uid = "";
    for (byte i = 0; i < _mfrc522.uid.size; i++) {
        uid += String(_mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        uid += String(_mfrc522.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();

    _logger.info("Badge détecté : " + uid);
    
    // Halt PICC
    _mfrc522.PICC_HaltA();
    return uid;
}