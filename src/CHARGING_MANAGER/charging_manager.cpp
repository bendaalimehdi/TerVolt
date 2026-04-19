#include "charging_manager.h"

const int PWM_FREQ = 1000; 
const int PWM_RES = 10; 

ChargingManager::ChargingManager(Logger& logger, ConfigManager& config) 
    : _logger(logger), _config(config) {
    _pwmPin = _config.data.pins.cp_pwm;   
    _adcPin = _config.data.pins.cp_adc;   
    _relayPin = _config.data.pins.relay;
}

void ChargingManager::begin() {
    pinMode(_relayPin, OUTPUT);
    digitalWrite(_relayPin, LOW);

    // API ESP32 v3.0
    if (!ledcAttach(_pwmPin, PWM_FREQ, PWM_RES)) {
        _logger.error("Échec initialisation LEDC (PWM)");
    }
    ledcWrite(_pwmPin, 1023); 
    _logger.success("ChargingManager initialisé");
}

void ChargingManager::setMaxCurrent(float amps) {
    _targetAmps = amps;
    float dutyPercent = _targetAmps / 0.6;
    setPWM(dutyPercent);
}

void ChargingManager::setPWM(float dutyCyclePercent) {
    int dutyValue = (dutyCyclePercent / 100.0) * 1023;
    // CORRECTION : On utilise la pin directement, pas le channel
    ledcWrite(_pwmPin, dutyValue);
}

float ChargingManager::readPilotVoltage() {
    int raw = analogRead(_adcPin);
    float voltage = (raw / 4095.0) * 3.3 * 4.0; 
    return voltage;
}

bool ChargingManager::isVehicleConnected() {
    return (readPilotVoltage() < 10.5);
}

bool ChargingManager::isVehicleRequestingCharge() {
    float vcp = readPilotVoltage();
    return (vcp > 5.0 && vcp < 7.5);
}

bool ChargingManager::isAuthorized() const {
    // Pour l'instant on simule une autorisation permanente
    return true; 
}

bool ChargingManager::isFault() const {
    return false;
}

bool ChargingManager::isCharging() {
    return isVehicleRequestingCharge() && isAuthorized();
}

float ChargingManager::getDutyCycle() const {
    float vcp = const_cast<ChargingManager*>(this)->readPilotVoltage();

    // État A : Pas de voiture -> 12V fixe -> 100% Duty Cycle
    if (vcp > 10.5) {
        return 100.0;
    }
    
    // État B/C : On calcule selon l'ampérage configuré
    // Si _targetAmps n'est pas encore défini, on retourne 100% par sécurité
    if (_targetAmps <= 0) return 100.0;

    float duty = _targetAmps / 0.6;
    if (duty > 100.0) duty = 100.0;
    return duty;
}

void ChargingManager::update() {
    float vcp = readPilotVoltage();

    if (vcp > 10.5) { // État A
        if (digitalRead(_relayPin)) {
            digitalWrite(_relayPin, LOW);
            _logger.info("Véhicule débranché.");
        }
        ledcWrite(_pwmPin, 1023); 
    } 
    else if (vcp > 8.0 && vcp < 10.0) { // État B
        _logger.info("Véhicule détecté (État B).");
        setMaxCurrent(_config.data.maxAmps);
    }
    else if (vcp > 5.0 && vcp < 7.5) { // État C
        if (isAuthorized() && !digitalRead(_relayPin)) {
            _logger.success("Démarrage de la charge !");
            digitalWrite(_relayPin, HIGH);
        }
    }
}