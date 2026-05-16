#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

class OtaManager {
public:
    // Le constructeur s'appuie sur le logger et la configuration globale
    OtaManager(Logger& logger, ConfigManager& config);
    
    // Initialise le service d'écoute OTA local (Port 8266)
    void begin();
    
    // À appeler en boucle dans la tâche réseau pour maintenir l'OTA local
    void handle();

    // Lance la mise à jour asynchrone depuis un serveur cloud distant
    void updateFromCloud(const String& url, const String& type);

private:
    Logger& _logger;
    ConfigManager& _config;
};

#endif