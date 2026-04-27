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
    _pwmPin = _config.data.pins.cp_pwm;   
    _adcPin = _config.data.pins.cp_adc;   
    _relayPin = _config.data.pins.relay;

    if (_relayPin != 0) { // Sécurité
        pinMode(_relayPin, OUTPUT);
        digitalWrite(_relayPin, LOW);
    }

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

String ChargingManager::getStateString() const {
    switch (_state) {
        case ChargingState::STATE_A: return "A - Libre";
        case ChargingState::STATE_B: return "B - Véhicule connecté";
        case ChargingState::STATE_C: return "C - Charge active";
        case ChargingState::STATE_D: return "D - Ventilation requise";
        case ChargingState::STATE_E: return "E - Erreur";
        case ChargingState::STATE_F: return "F - Fault EVSE";
        default:                     return "Inconnu";
    }
}

void ChargingManager::update() {
    float vcp = readPilotVoltage();
    ChargingState newState;

    // Détermination de l'état selon la tension CP (J1772)
    if      (vcp > 10.5)               newState = ChargingState::STATE_A;
    else if (vcp > 8.0  && vcp < 10.5) newState = ChargingState::STATE_B;
    else if (vcp > 5.0  && vcp < 8.0)  newState = ChargingState::STATE_C;
    else if (vcp > 2.0  && vcp < 5.0)  newState = ChargingState::STATE_D;
    else                                newState = ChargingState::STATE_E;

    // Log uniquement lors d'un changement d'état
    if (newState != _state) {
        _logger.info("Transition état : " + getStateString() + " → ...");
        _state = newState;

        switch (_state) {
            case ChargingState::STATE_A:
                digitalWrite(_relayPin, LOW);
                ledcWrite(_pwmPin, 1023); // Signal fixe 12V (pas de PWM)
                _logger.info("[État A] Véhicule débranché. Relais ouvert.");
                break;

            case ChargingState::STATE_B:
                digitalWrite(_relayPin, LOW); // Sécurité : relais ouvert
                setMaxCurrent(_config.data.maxAmps);
                _logger.info("[État B] Véhicule détecté. PWM actif à " + String(_config.data.maxAmps) + "A");
                break;

            case ChargingState::STATE_C:
                if (isAuthorized()) {
                    digitalWrite(_relayPin, HIGH);
                    _logger.success("[État C] Charge démarrée !");
                } else {
                    _logger.warn("[État C] Véhicule demande la charge mais non autorisé.");
                }
                break;

            case ChargingState::STATE_D:
                digitalWrite(_relayPin, LOW);
                _logger.warn("[État D] Ventilation requise — non supporté. Relais ouvert.");
                break;

            case ChargingState::STATE_E:
            case ChargingState::STATE_F:
                digitalWrite(_relayPin, LOW);
                ledcWrite(_pwmPin, 0); // Coupe le signal CP
                _logger.error("[État E/F] Fault détecté ! Relais ouvert, CP coupé.");
                break;

            default:
                break;
        }
    }

    // Surveillance continue en état C : si le véhicule débranche sans passer par B
    if (_state == ChargingState::STATE_C && !isAuthorized()) {
        digitalWrite(_relayPin, LOW);
        _state = ChargingState::STATE_B;
        _logger.warn("Autorisation perdue en cours de charge. Relais ouvert.");
    }
}