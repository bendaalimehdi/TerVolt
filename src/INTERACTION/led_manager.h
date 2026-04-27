#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "../CONFIG/config_manager.h"
#include "../LOG/log.h"

class LedManager {
public:
    // Constructeur : reçoit les références aux gestionnaires de log et config 
    LedManager(Logger& logger, ConfigManager& config);
    
    // Initialisation du ruban 
    void begin();
    
    // À appeler dans la loop() pour faire avancer l'animation 
    void update();
    
    // Méthodes d'état pour changer la couleur/animation
    void setStatusAvailable();          // Bleu avec sillage
    void setStatusCharging();           // Vert avec sillage
    void setStatusError();              // Rouge clignotant (alerte)
    void setStatusConfig();             // Violet fixe (Mode AP)

private:
    Logger& _logger;
    ConfigManager& _config;
    Adafruit_NeoPixel* _strip = nullptr;
    
    unsigned long _lastAnimationUpdate = 0;
    int _animationStep = 0;
    
    // Type d'état actuel pour l'animation
    enum State { AVAILABLE, CHARGING, ERROR, CONFIG_AP };
    State _currentState = AVAILABLE;

    // Fonction interne pour générer l'effet de sillage
    void renderScrollWithSillage(uint8_t r, uint8_t g, uint8_t b);
};

#endif