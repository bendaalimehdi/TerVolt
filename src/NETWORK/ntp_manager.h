#ifndef NTP_MANAGER_H
#define NTP_MANAGER_H

#include <Arduino.h>
#include <time.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"

class NtpManager {
public:
    NtpManager(Logger& logger, ConfigManager& config);

    // Initialise les paramètres de fuseau horaire et lance la requete NTP
    void begin();

    // À appeler périodiquement dans la tâche réseau (Cœur 0) pour vérifier le statut
    void maintain();

    // Renvoie true si l'heure est correctement synchronisée avec le serveur
    bool isSynced() const { return _isSynced; }

    // Retourne une chaîne formatée "YYYY-MM-DD HH:MM:SS" ou un fallback d'uptime
    String getFormattedTime();

private:
    Logger&        _logger;
    ConfigManager& _config;

    bool          _isSynced;
    unsigned long _lastCheckTime;
    unsigned long _checkInterval;
    int           _syncRetryCount;

    // Constante de validation (Timestamp Unix minimum pour l'année 2026)
    static constexpr time_t MIN_VALID_TIMESTAMP = 1767225600; // 2026-01-01 00:00:00
};

#endif