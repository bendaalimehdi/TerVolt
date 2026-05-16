#include "charging_manager.h"

const int PWM_FREQ = 1000; 
const int PWM_RES = 10; 

ChargingManager::ChargingManager(Logger& logger, ConfigManager& config) 
    : _logger(logger), _config(config) {
    _pwmPin = _config.data.pins.cp_pwm;   
    _adcPin = _config.data.pins.cp_adc;   
    _relayPin = _config.data.pins.relay;
    _prechargePin = _config.data.pins.precharge;
}

void ChargingManager::begin() {
    _pwmPin = _config.data.pins.cp_pwm;   
    _adcPin = _config.data.pins.cp_adc;   
    _relayPin = _config.data.pins.relay;
    _prechargePin = _config.data.pins.precharge;
    
        // Configuration des pins
        pinMode(_adcPin, INPUT);
        pinMode(_prechargePin, OUTPUT);

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
    // Lissage par moyenne pour éliminer le bruit quand rien n'est branché
    long sum = 0;
    const int samples = 25; 
    for(int i = 0; i < samples; i++) {
        sum += analogRead(_adcPin);
        delayMicroseconds(50); // Petit délai pour laisser l'ADC se stabiliser
    }
    float raw = sum / (float)samples;
    
    // Ton calcul de pont diviseur (ajuste le ratio 4.0 si nécessaire)
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
    ChargingState detectedState;

    // Détermination de l'état selon la tension CP (J1772)
    if      (vcp > 10.5)               detectedState = ChargingState::STATE_A;
    else if (vcp > 8.0  && vcp < 10.5) detectedState = ChargingState::STATE_B;
    else if (vcp > 5.0  && vcp < 8.0)  detectedState = ChargingState::STATE_C;
    else if (vcp > 2.0  && vcp < 5.0)  detectedState = ChargingState::STATE_D;
    else                               detectedState = ChargingState::STATE_E;

    // --- LOGIQUE ANTI-REBOND (DEBOUNCE) ---
    static ChargingState lastRawState = _state;
    static unsigned long lastChangeTime = 0;
    const unsigned long CONFIRM_DELAY = 150; // 150ms de stabilité requis

    if (detectedState != lastRawState) {
        lastChangeTime = millis();
        lastRawState = detectedState;
    }

    // On ne change l'état réel que si le nouvel état est stable
    if (detectedState != _state && (millis() - lastChangeTime > CONFIRM_DELAY)) {
       _state = detectedState;
       _logger.info("Changement d'état confirmé : " + getStateString());

        ChargingState previousState = _state;
        _state = detectedState;

        switch (_state) {
            case ChargingState::STATE_A:
                digitalWrite(_relayPin, LOW);
                ledcWrite(_pwmPin, 1023);
                _logger.info("[État A] Véhicule débranché. Relais ouvert.");
                break;

            case ChargingState::STATE_B:
                digitalWrite(_relayPin, LOW);
                setMaxCurrent(_config.data.maxAmps);
                _logger.info("[État B] Véhicule détecté. PWM actif à " + String(_config.data.maxAmps) + "A");
                break;

            case ChargingState::STATE_C:
                if (isAuthorized()) {
                    // --- Séquence de Précharge ---
                    _logger.info("[État C] Activation de la précharge...");
                    digitalWrite(_prechargePin, HIGH); // On ferme le petit relais
                    
                    delay(500); // On laisse 500ms aux condensateurs de l'OBC pour se charger
                    
                    digitalWrite(_relayPin, HIGH);    // On ferme le contacteur principal (sans arc)
                    _logger.success("[État C] Charge démarrée (Contacteur principal ON) !");
                    
                    delay(100); // Petit recouvrement de sécurité
                    digitalWrite(_prechargePin, LOW); // On peut couper la précharge maintenant
                } else {
                    digitalWrite(_relayPin, LOW);
                    digitalWrite(_prechargePin, LOW);
                    _logger.warn("[État C] Véhicule demande la charge mais non autorisé.");
                }
                break;

            case ChargingState::STATE_D:
                // Même séquence si ventilation requise
                if (isAuthorized()) {
                    digitalWrite(_prechargePin, HIGH);
                    delay(500);
                    digitalWrite(_relayPin, HIGH);
                    delay(100);
                    digitalWrite(_prechargePin, LOW);
                    _logger.warn("[État D] Ventilation requise — Charge activée.");
                }
                break;

            case ChargingState::STATE_E:
            case ChargingState::STATE_F:
                digitalWrite(_relayPin, LOW);
                ledcWrite(_pwmPin, 0);
                _logger.error("[État E/F] Fault détecté ! Relais ouvert, CP coupé.");
                break;

            default:
                break;
        }
    }

    // Surveillance continue en état C
    if (_state == ChargingState::STATE_C && !isAuthorized()) {
        digitalWrite(_relayPin, LOW);
        _state = ChargingState::STATE_B;
        _logger.warn("Autorisation perdue en cours de charge. Relais ouvert.");
    }
}