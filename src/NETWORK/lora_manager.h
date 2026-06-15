#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include <Arduino.h>
#include "../LOG/log.h"
#include "../CONFIG/config_manager.h"
#include "../CHARGING_MANAGER/charging_manager.h"
#include "../ENERGY/energy_manager.h"

class LoraManager {
public:
    // Le constructeur reçoit les références des composants nécessaires (pattern identique à ServerManager)
    LoraManager(Logger& logger, ConfigManager& config, ChargingManager& charger, EnergyManager& energy);

    // Initialise le port série matériel (Serial1) si LoRa est activé
    void begin();

    // À appeler en boucle dans la TaskNetwork pour gérer l'envoi cyclique de 10s et l'ACK
    void maintain();

private:
    Logger&          _logger;
    ConfigManager&   _config;
    ChargingManager& _charger;
    EnergyManager&   _energy;

    unsigned long    _lastTransmissionTime;
    const unsigned long _transmissionInterval = 10000; // Fréquence d'émission : 10 secondes

    void sendTelemetry();
    void checkForAck();
};

#endif