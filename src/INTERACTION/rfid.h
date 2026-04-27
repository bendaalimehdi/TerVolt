#ifndef RFID_MANAGER_H
#define RFID_MANAGER_H

#include <Arduino.h>
#include <MFRC522.h>
#include <SPI.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

class RfidManager {
public:
    RfidManager(Logger& logger, ConfigManager& config);
    void begin();
    String update(); // Retourne l'UID si un badge est lu, sinon ""
    bool isCardPresent(); // Nouvelle méthode
    String readUID();     // Nouvelle méthode

private:
    Logger& _logger;
    ConfigManager& _config;
    MFRC522 _mfrc522;
};

#endif