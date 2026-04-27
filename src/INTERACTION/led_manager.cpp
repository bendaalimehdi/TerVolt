#include "led_manager.h"

LedManager::LedManager(Logger& logger, ConfigManager& config) 
    : _logger(logger), _config(config) {
    // Initialisation dynamique basée sur le config.json 
    _strip = new Adafruit_NeoPixel(
        _config.data.num_leds, 
        _config.data.pins.led_rgb, 
        NEO_GRB + NEO_KHZ800
    );
}

void LedManager::begin() {
    if (_strip) {
        _strip->begin();
        _strip->setBrightness(150); // Luminosité ajustable (0-255)
        _strip->show(); 
        _logger.success("LedManager : Ruban WS2811 initialise");
    }
}

void LedManager::update() {
    // Gère le timing du défilement (50ms pour une animation fluide)
    if (millis() - _lastAnimationUpdate > 50) {
        _lastAnimationUpdate = millis();
        _animationStep++;

        // Applique l'animation selon l'état actuel
        switch (_currentState) {
            case AVAILABLE:
                renderScrollWithSillage(0, 0, 255); // Sillage Bleu
                break;
            case CHARGING:
                renderScrollWithSillage(0, 255, 0); // Sillage Vert
                break;
            case ERROR:
                // Rouge clignotant simple
                if ((_animationStep / 10) % 2 == 0) renderScrollWithSillage(255, 0, 0);
                else renderScrollWithSillage(0, 0, 0);
                break;
            case CONFIG_AP:
                // Violet fixe pour la configuration
                for(int i=0; i<_strip->numPixels(); i++) _strip->setPixelColor(i, _strip->Color(255, 0, 255));
                _strip->show();
                break;
        }
    }
}

void LedManager::renderScrollWithSillage(uint8_t r, uint8_t g, uint8_t b) {
    int numLeds = _strip->numPixels();
    int head = _animationStep % numLeds;

    for (int i = 0; i < numLeds; i++) {
        if (i == head) {
            // La "tête" du défilement est à pleine intensité
            _strip->setPixelColor(i, _strip->Color(r, g, b));
        } else {
            // Calcul de la traînée (sillage)
            // Plus la LED est loin derrière la tête, plus elle est sombre
            int distance = (head - i + numLeds) % numLeds;
            
            // On divise l'intensité par 2 à chaque position d'écart (Bitwise shift)
            uint8_t nr = r >> (distance + 1);
            uint8_t ng = g >> (distance + 1);
            uint8_t nb = b >> (distance + 1);
            
            _strip->setPixelColor(i, _strip->Color(nr, ng, nb));
        }
    }
    _strip->show();
}

// Méthodes de changement d'état
void LedManager::setStatusAvailable() { _currentState = AVAILABLE; }
void LedManager::setStatusCharging()  { _currentState = CHARGING; }
void LedManager::setStatusError()     { _currentState = ERROR; }
void LedManager::setStatusConfig()    { _currentState = CONFIG_AP; }