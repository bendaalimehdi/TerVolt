#include "charging_manager.h"

const int PWM_FREQ = 1000; 
const int PWM_RES = 10; 

ChargingManager::ChargingManager(Logger& logger, ConfigManager& config) 
    : _logger(logger), _config(config) {
    _pwmPin = _config.data.pins.cp_pwm;   
    _adcPin = _config.data.pins.cp_adc;   
    _relayPin = _config.data.pins.relay;
    _prechargePin = _config.data.pins.precharge;
    _feedbackRelayPin = _config.data.pins.feedback_relay;
}

void ChargingManager::begin() {
    _pwmPin = _config.data.pins.cp_pwm;   
    _adcPin = _config.data.pins.cp_adc;   
    _relayPin = _config.data.pins.relay;
    _prechargePin = _config.data.pins.precharge;
    _feedbackRelayPin = _config.data.pins.feedback_relay;
    
        // Configuration des pins
        pinMode(_adcPin, INPUT);
        pinMode(_prechargePin, OUTPUT);
        pinMode(_feedbackRelayPin, INPUT_PULLUP); 

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

bool ChargingManager::checkRelayGlueFault() {
    bool relayCommandedOn = (digitalRead(_relayPin) == HIGH);
    // Ici, LOW = Contact auxiliaire fermé (physiquement passant)
    bool relayPhysicallyClosed = (digitalRead(_feedbackRelayPin) == LOW); 

    // SENS 1 : ERREUR DE COLLAGE (Le relais devrait être ouvert/OFF mais il est fermé)
    if (!relayCommandedOn && relayPhysicallyClosed) {
        _logger.error("DÉFAUT SÉCURITÉ : Contacteur collé ou soudé !");
        return true; 
    }

    // SENS 2 : ERREUR D'ENCLENCHEMENT (Le relais devrait être fermé/ON mais il reste ouvert)
    if (relayCommandedOn && !relayPhysicallyClosed) {
        _logger.error("DÉFAUT MATÉRIEL : Échec d'enclenchement du contacteur principal !");
        return true;
    }

    return false; // Tout est parfaitement cohérent
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
    // --- SÉCURITÉ CRITIQUE PRIORITAIRE ---
    if (checkRelayGlueFault()) {
        if (_state != ChargingState::STATE_E) {
            _state = ChargingState::STATE_E;
            digitalWrite(_relayPin, LOW);
            digitalWrite(_prechargePin, LOW);
            ledcWrite(_pwmPin, 0); // On coupe complètement le signal CP (0V)
            _logger.error("ALERTE CRITIQUE : CONTACTEUR COLLÉ / SOUDÉ ! Arrêt d'urgence matériel.");
        }
        return; 
    }

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
                digitalWrite(_prechargePin, LOW);
                ledcWrite(_pwmPin, 1023); // Repos 12V constant
                
                // Vérification post-charge immédiate : Les contacts se sont-ils bien ouverts ?
                delay(50); // Petit temps de relaxation mécanique du contacteur
                if (checkRelayGlueFault()) {
                    _state = ChargingState::STATE_E;
                    ledcWrite(_pwmPin, 0); // Coupure d'urgence du signal CP (0V)
                    _logger.error("[POST-CHARGE] Alerte : Le véhicule s'est débranché mais le contacteur est resté collé !");
                } else {
                    _logger.info("[État A] Véhicule débranché. Sécurité contacteur validée (Ouvert).");
                }
                break;

            case ChargingState::STATE_B:
                digitalWrite(_relayPin, LOW);
                digitalWrite(_prechargePin, LOW);
                setMaxCurrent(_config.data.maxAmps);
                
                // Vérification post-charge si la voiture interrompt la charge mais reste branchée
                delay(50);
                if (checkRelayGlueFault()) {
                    _state = ChargingState::STATE_E;
                    ledcWrite(_pwmPin, 0);
                    _logger.error("[POST-CHARGE] La charge s'est arrêtée mais le contacteur refuse de s'ouvrir !");
                } else {
                    _logger.info("[État B] Véhicule connecté en attente. Sécurité contacteur validée (Ouvert).");
                }
                break;

            case ChargingState::STATE_C:
                if (isAuthorized()) {
                    // VERIFICATION AVANT CHARGE : Le contacteur est-il bien ouvert au repos ?
                    // On s'assure qu'aucun court-circuit ou problème mécanique n'est présent AVANT d'envoyer la sauce.
                    if (checkRelayGlueFault()) {
                        _state = ChargingState::STATE_E;
                        ledcWrite(_pwmPin, 0);
                        _logger.error("[PRÉ-CHARGE] Refus de démarrer : Cohérence contacteur invalide au repos !");
                        break; 
                    }

                    _logger.info("[État C] Validation contacteur OK. Activation de la précharge...");
                    
                    // Séquence de démarrage / soft-start
                    digitalWrite(_prechargePin, HIGH); 
                    delay(500); // Temps de précharge des condensateurs de la voiture
                    
                    digitalWrite(_relayPin, HIGH); // Fermeture du contacteur principal
                    delay(100); // Attente de l'enclenchement mécanique
                    
                    // VERIFICATION PENDANT CHARGE : Est-ce que le contacteur s'est bien fermé ?
                    if (checkRelayGlueFault()) {
                        // Si checkRelayGlueFault renvoie true alors qu'on a mis _relayPin à HIGH, 
                        // c'est que le contact auxiliaire dit qu'il est resté OUVERT (Erreur d'enclenchement)
                        digitalWrite(_relayPin, LOW);
                        digitalWrite(_prechargePin, LOW);
                        _state = ChargingState::STATE_E;
                        ledcWrite(_pwmPin, 0);
                        _logger.error("[PRÉ-CHARGE] Échec critique : Le contacteur principal n'a pas réussi à se fermer !");
                        break;
                    }

                    digitalWrite(_prechargePin, LOW); // Tout est OK, on coupe la précharge
                    _logger.success("[État C] Sécurités validées. Contacteur principal ON. Charge active !");
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




void ChargingManager::forceEmergencyStop() {
    // Si on est déjà en état d'erreur, pas besoin de réappliquer les coupures
    if (_state == ChargingState::STATE_E) {
        return;
    }

    _state = ChargingState::STATE_E;
    
    // Coupure matérielle immédiate des GPIO de puissance
    digitalWrite(_relayPin, LOW);
    digitalWrite(_prechargePin, LOW);
    
    // Arrêt complet du signal PWM (0V permanent au véhicule)
    ledcWrite(_pwmPin, 0); 
    
    _logger.error("[ARRÊT D'URGENCE] Tous les relais sont coupés. Borne verrouillée.");
}