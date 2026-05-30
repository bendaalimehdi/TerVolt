#include "ntp_manager.h"
#include <WiFi.h>

NtpManager::NtpManager(Logger& logger, ConfigManager& config)
    : _logger(logger), _config(config), _isSynced(false),
      _lastCheckTime(0), _checkInterval(5000), _syncRetryCount(0) {}

void NtpManager::begin() {
    // Règle de fuseau horaire par défaut pour l'Europe/Tunis (CET/CEST)
    // "CET-1CEST,M3.5.0,M10.5.0/3" gère automatiquement le passage heure d'été/hiver
    String ntpServer = _config.data.ntpServer; // Utilise la config ou pool.ntp.org par défaut
    
    _logger.info("NTP : Configuration du fuseau horaire local (Tunis/Paris)...");
    
    // Initialisation du service d'arrière-plan de l'ESP-IDF
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
    
    _isSynced = false;
    _lastCheckTime = millis();
}

void NtpManager::maintain() {
    // Vérification périodique du statut (toutes les 5 secondes au début, puis espacé)
    if (millis() - _lastCheckTime < _checkInterval) {
        return;
    }
    _lastCheckTime = millis();

    if (WiFi.status() != WL_CONNECTED) {
        return; // Pas de réseau, inutile de vérifier
    }

    time_t nowTime = time(nullptr);

    if (nowTime > MIN_VALID_TIMESTAMP) {
        if (!_isSynced) {
            _isSynced = true;
            _checkInterval = 60000; // Une fois synchronisé, on ne vérifie que toutes les minutes
            _logger.success("✓ NTP : Horloge système synchronisée avec succès ! Heure actuelle : " + getFormattedTime());
        }
    } else {
        if (_isSynced) {
            _isSynced = false;
            _checkInterval = 5000;
            _logger.error("🚨 NTP : Perte de synchronisation temporelle détectée !");
        } else {
            _syncRetryCount++;
            if (_syncRetryCount % 6 == 0) { // Log léger toutes les 30 secondes d'attente
                _logger.warn("NTP : En attente de réponse des serveurs de temps...");
            }
        }
    }
}

String NtpManager::getFormattedTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        // Fallback propre si non synchronisé : donne l'uptime de la borne
        return "UPTIME-" + String(millis() / 1000) + "s";
    }

    char timeStringBuff[25];
    // Format standard SQL / Backend : YYYY-MM-DD HH:MM:SS
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}