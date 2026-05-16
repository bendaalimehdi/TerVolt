#include "ota_manager.h"

OtaManager::OtaManager(Logger& logger, ConfigManager& config) 
    : _logger(logger), _config(config) {}

void OtaManager::begin() {
    // 1. Configuration de l'OTA Local (Via réseau local / PlatformIO)
    ArduinoOTA.setPort(8266);
    ArduinoOTA.setHostname(_config.data.deviceId.c_str());

    if (_config.data.ota_password.length() >= 8) {
        ArduinoOTA.setPassword(_config.data.ota_password.c_str());
    } else {
        ArduinoOTA.setPassword("TerVoltFallbackAP");
        _logger.warn("OtaManager : Mot de passe absent dans config.json. Utilisation du fallback.");
    }

    // Callbacks de l'OTA local
    ArduinoOTA.onStart([this]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        _logger.warn("OtaManager : Début de flashage local (" + type + ").");
    });

    ArduinoOTA.onEnd([this]() {
        _logger.success("OtaManager : Écriture locale terminée ! Redémarrage...");
    });

    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA Local] Progression : %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([this](ota_error_t error) {
        _logger.error("OtaManager : Erreur locale de type [" + String(error) + "]");
    });

    ArduinoOTA.begin();
    _logger.success("OtaManager : Service d'écoute local initialisé (Port 8266).");
}

void OtaManager::handle() {
    // Maintenance continue du protocole ArduinoOTA
    ArduinoOTA.handle();
}

void OtaManager::updateFromCloud(const String& url, const String& type) {
    if (WiFi.status() != WL_CONNECTED) {
        _logger.error("OtaManager Cloud : Annulation, la borne n'a pas accès à Internet.");
        return;
    }

    int otaCommand = U_FLASH; 
    if (type == "filesystem" || type == "littlefs") {
        otaCommand = U_SPIFFS; // Sélectionne la table de partition LittleFS
        _logger.warn("OtaManager Cloud : Flashage ciblé sur le FILESYSTEM...");
    } else {
        _logger.warn("OtaManager Cloud : Flashage ciblé sur le FIRMWARE...");
    }

    _logger.info("OtaManager Cloud : Connexion au dépôt d'artefacts -> " + url);

    HTTPClient http;
    http.begin(url);
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        _logger.error("OtaManager Cloud : Serveur distant introuvable (HTTP " + String(httpCode) + ")");
        http.end();
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        _logger.error("OtaManager Cloud : Fichier binaire corrompu ou vide.");
        http.end();
        return;
    }

    // Allocation de la partition flash
    if (!Update.begin(contentLength, otaCommand)) {
        _logger.error("OtaManager Cloud : Mémoire Flash insuffisante pour accueillir l'image.");
        http.end();
        return;
    }

    _logger.success("OtaManager Cloud : Allocation OK. Écriture du flux réseau en cours...");

    // Streaming direct vers la mémoire flash
    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);

    if (written == contentLength) {
        _logger.success("OtaManager Cloud : Intégrité physique validée (" + String(written) + " octets écrits).");
    } else {
        _logger.error("OtaManager Cloud : Écriture asynchrone incomplète.");
    }

    // Clôture et validation des registres de boot
    if (Update.end()) {
        if (Update.isFinished()) {
            _logger.success("OtaManager Cloud : Mise à jour validée ! Reboot matériel...");
            delay(1500);
            ESP.restart();
        } else {
            _logger.error("OtaManager Cloud : Échec de l'homologation finale de la partition.");
        }
    } else {
        _logger.error("OtaManager Cloud : Erreur logicielle interne. Code : " + String(Update.getError()));
    }

    http.end();
}