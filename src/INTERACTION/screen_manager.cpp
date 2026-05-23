#include "screen_manager.h"

ScreenManager::ScreenManager(Logger& logger, ConfigManager& config, ChargingManager& charger, EnergyManager& energy, TemperatureManager& temp)
    : _logger(logger), _config(config), _charger(charger), _energy(energy), _tempManager(temp) {}

void ScreenManager::begin() {
    _logger.info("Initialisation de l'écran LCD1602 I2C...");

    // Broches de ton connecteur JST 4P
    int hardwareSda = 8; 
    int hardwareScl = 9;

    // Initialisation du bus I2C matériel
    Wire.begin(hardwareSda, hardwareScl, 100000); // 100 kHz (standard pour le PCF8574)

    // Allocation de l'écran (Adresse standard 0x27, 16 colonnes, 2 lignes)
    _lcd = new LiquidCrystal_I2C(0x27, 16, 2);
    
    if (_lcd) {
        _lcd->init();
        _lcd->backlight(); // Allume le rétroéclairage bleu/vert
        
        // Message d'accueil TerVolt
        _lcd->clear();
        _lcd->setCursor(4, 0);
        _lcd->print("TERVOLT");
        _lcd->setCursor(3, 1);
        _lcd->print("V1.0.0 PROD");
        delay(1500);
        _lcd->clear();
        
        _logger.success("✓ ScreenManager initialisé pour LCD1602 (SDA:8, SCL:9)");
    } else {
        _logger.error("❌ Échec d'allocation du LCD1602.");
    }
}

void ScreenManager::update() {
    if (millis() - _lastDisplayUpdate < _displayInterval) {
        return;
    }
    _lastDisplayUpdate = millis();

    if (!_lcd) return;

    if (_charger.isFault()) {
        drawEmergencyScreen(_charger.getStateString());
        return;
    }

    drawDashboard();
}

void ScreenManager::drawDashboard() {
    // ---- LIGNE 1 : ÉTAT DE LA BORNE (Fixe) ----
    _lcd->setCursor(0, 0);
    // Exemple : "STAT: A - Libre " (on remplit de blancs pour effacer les anciens caractères)
    String stateStr = "STAT:" + _charger.getStateString().substring(0, 11);
    while (stateStr.length() < 16) stateStr += " "; 
    _lcd->print(stateStr.c_str());

    // ---- LIGNE 2 : ALTERNANCE DES DONNÉES (Énergie / Puissance / Temp) ----
    _lcd->setCursor(0, 1);
    String line2 = "";

    switch (_displayCycle) {
        case 0: {
            float kw = _energy.activePowerTotal() / 1000.0f;
            line2 = "POW : " + String(kw, 2) + " kW";
            _displayCycle = 1;
            break;
        }
        case 1: {
            float kwh = _energy.session.getSessionEnergyKwh();
            line2 = "ENRG: " + String(kwh, 2) + " kWh";
            _displayCycle = 2;
            break;
        }
        case 2: {
            int temp = (int)_tempManager.getContacteurTemp();
            line2 = "TEMP: " + String(temp) + " C";
            _displayCycle = 0; // Reset cycle
            break;
        }
    }

    // Pad de fin pour nettoyer la ligne
    while (line2.length() < 16) line2 += " ";
    _lcd->print(line2.c_str());
}

void ScreenManager::drawEmergencyScreen(const String& reason) {
    _lcd->setCursor(0, 0);
    _lcd->print("!! ARRET URGE !!");
    
    _lcd->setCursor(0, 1);
    String errorMsg = reason.substring(0, 16);
    while (errorMsg.length() < 16) errorMsg += " ";
    _lcd->print(errorMsg.c_str());
}