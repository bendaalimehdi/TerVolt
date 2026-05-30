#include "charging_manager.h"

const int PWM_FREQ = 1000;
const int PWM_RES  = 10;

ChargingManager::ChargingManager(Logger& logger, ConfigManager& config)
    : _logger(logger), _config(config),
      _lastRawState(ChargingState::STATE_A),
      _lastChangeTime(0),
      _prechargeStartTime(0),
      _relayCloseTime(0),
      _prechargeSubState(PrechargeStep::IDLE),
      _latestVcp(12.0f),               
      _authorized(false)               
{
    _pwmPin           = _config.data.pins.cp_pwm;
    _adcPin           = _config.data.pins.cp_adc;
    _relayPin         = _config.data.pins.relay;
    _prechargePin     = _config.data.pins.precharge;
    _feedbackRelayPin = _config.data.pins.feedback_relay;
}

void ChargingManager::begin() {
    pinMode(_adcPin, INPUT);
    pinMode(_prechargePin, OUTPUT);
    pinMode(_feedbackRelayPin, INPUT_PULLUP);

    if (_relayPin != -1) {
        pinMode(_relayPin, OUTPUT);
        digitalWrite(_relayPin, LOW);
    }

    if (!ledcAttach(_pwmPin, PWM_FREQ, PWM_RES)) {
        _logger.error("Échec initialisation LEDC (PWM)");
    }
    ledcWrite(_pwmPin, 1023);  // 12 V constant au repos
    _logger.success("ChargingManager initialisé");
}

// ─────────────────────────────────────────────────────────────────────────────
// 🔒 SYSTEM DE LECTURE DU CAPTEUR DU SEUIL PILOTE (CP)
// ─────────────────────────────────────────────────────────────────────────────
float ChargingManager::readPilotVoltage() {
    int peak = 0;
    const int samples = 25;

    for (int i = 0; i < samples; i++) {
        int val = analogRead(_adcPin);
        if (val > peak) peak = val;
        delayMicroseconds(50);
    }

    // Rapport du pont diviseur dynamique configuré
    float voltage = (peak / 4095.0f) * 3.3f * _config.data.cpDividerRatio;
    return voltage;
}

bool ChargingManager::isVehicleConnected() {
    return (_latestVcp < 10.5f); 
}

bool ChargingManager::isVehicleRequestingCharge() {
    return (_latestVcp > 5.0f && _latestVcp < 7.5f); 
}

bool ChargingManager::isCharging() {
    return (_state == ChargingState::STATE_C || _state == ChargingState::STATE_D) 
           && _prechargeSubState == PrechargeStep::DONE;
}

bool ChargingManager::isAuthorized() const {
    return _authorized; 
}

void ChargingManager::setAuthorized(bool auth) {
    _authorized = auth;
}

bool ChargingManager::isFault() const {
    return (_state == ChargingState::STATE_E || _state == ChargingState::STATE_F);
}

float ChargingManager::getLatestPilotVoltage() {
    return _latestVcp;
}

// ─────────────────────────────────────────────────────────────────────────────
// 🔒 PROTECTION MATÉRIELLE CONTRE LE COLLAGE ET LES SOUDEURES DE RELAIS
// ─────────────────────────────────────────────────────────────────────────────
bool ChargingManager::checkRelayGlueFault(bool forceLiveRead) {
    bool relayCommandedOn;
    bool relayPhysicallyClosed;

    if (forceLiveRead) {
        relayCommandedOn      = (_relayPin != -1 && digitalRead(_relayPin) == HIGH);
        relayPhysicallyClosed = (_feedbackRelayPin != -1 && digitalRead(_feedbackRelayPin) == LOW);
    } else {
        relayCommandedOn      = _cachedRelayCommandedOn;
        relayPhysicallyClosed = _cachedRelayPhysicallyClosed;
    }

    // Cas 1 : Contact collé / soudé alors qu'on demande l'ouverture
    if (!relayCommandedOn && relayPhysicallyClosed) {
        return true;
    }
    // Cas 2 : Échec d'enclenchement mécanique ou bobine coupée
    if (relayCommandedOn && !relayPhysicallyClosed) {
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// 🔒 FSM DE PRÉCHARGE ASYNCHRONE ET SÉCURISÉE
// ─────────────────────────────────────────────────────────────────────────────
void ChargingManager::tickPrecharge(bool requiresVentilation) {
    switch (_prechargeSubState) {

        case PrechargeStep::IDLE:
            if (checkRelayGlueFault(true)) {
                _logger.error("[PRÉ-CHARGE] Refus de démarrer : contacteur collé détecté en direct !");
                _enterFault(ChargingState::STATE_F); 
                return;
            }
            _logger.info("[PRÉ-CHARGE] Validation OK. Activation de la précharge...");
            digitalWrite(_prechargePin, HIGH);
            _prechargeStartTime = millis();
            _prechargeSubState  = PrechargeStep::WAITING_PRECHARGE;
            break;

        case PrechargeStep::WAITING_PRECHARGE:
            if (millis() - _prechargeStartTime >= PRECHARGE_DELAY_MS) {
                digitalWrite(_relayPin, HIGH);
                _relayCloseTime   = millis();
                _prechargeSubState = PrechargeStep::WAITING_RELAY_CLOSE;
            }
            break;

        case PrechargeStep::WAITING_RELAY_CLOSE:
            // Anti-blocage infini si le retour auxiliaire est défectueux (Timeout 500ms)
            if (millis() - _relayCloseTime > 500) { 
                _logger.error("[PRÉ-CHARGE] Échec : Timeout de commutation du contacteur !");
                digitalWrite(_relayPin, LOW);
                digitalWrite(_prechargePin, LOW);
                _enterFault(ChargingState::STATE_F);
                return;
            }

            if (checkRelayGlueFault(true)) {
                _logger.error("[PRÉ-CHARGE] Échec critique : le contacteur refuse de s'enclencher !");
                digitalWrite(_relayPin, LOW);
                digitalWrite(_prechargePin, LOW);
                _enterFault(ChargingState::STATE_F);
                return;
            }
            
            digitalWrite(_prechargePin, LOW);
            _prechargeSubState = PrechargeStep::DONE;

            if (requiresVentilation) {
                _logger.warn("[État D] Sécurités validées. Ventilation enclenchée — charge active.");
            } else {
                _logger.success("[État C] Sécurités validées. Contacteur ON. Charge active !");
            }
            break;

        case PrechargeStep::DONE:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GESTION ET COMPATIBILITÉ DES INTENSITÉS J1772
// ─────────────────────────────────────────────────────────────────────────────
void ChargingManager::setMaxCurrent(float amps) {
    if (amps < 6.0f) amps = 6.0f;
    if (amps > _config.data.maxAmps) amps = _config.data.maxAmps;
    _targetAmps = amps;
    
    setPWM(getDutyCycle());
}

float ChargingManager::getDutyCycle() const {
    if (_state == ChargingState::STATE_A || _targetAmps <= 0) return 100.0f;

    float duty;
    if (_targetAmps <= 51.0f) {
        duty = _targetAmps / 0.6f;        
    } else {
        duty = (_targetAmps + 64.0f) / 2.5f; 
    }

    if (duty > 100.0f) duty = 100.0f;
    return duty;
}

void ChargingManager::setPWM(float dutyCyclePercent) {
    int dutyValue = (dutyCyclePercent / 100.0f) * 1023;
    ledcWrite(_pwmPin, dutyValue);
}

float ChargingManager::calculatePpResistance(int adcRaw) {
    if (adcRaw <= 0 || adcRaw >= 4095) return -1.0f;
    float v = (adcRaw / 4095.0f) * 3.3f;
    if (v >= 3.25f) return -1.0f; 
    return (1000.0f * v) / (3.3f - v); // Hypothèse R_pullup = 1kOhm
}

String ChargingManager::getStateString() const {
    switch (_state) {
        case ChargingState::STATE_A: return "A - Disponible";
        case ChargingState::STATE_B: return "B - Connecte";
        case ChargingState::STATE_C: return "C - En Charge";
        case ChargingState::STATE_D: return "D - Ventilation";
        case ChargingState::STATE_E: return "E - Erreur VE";
        case ChargingState::STATE_F: return "F - Panne EVSE";
        default:                     return "INCONNU";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GESTION DES FAUTES ET RESET DE SÉCURITÉ SÉCURISÉS
// ─────────────────────────────────────────────────────────────────────────────
void ChargingManager::_enterFault(ChargingState faultState) {
    _state = faultState; 
    _prechargeSubState = PrechargeStep::IDLE;
    if (_relayPin != -1) digitalWrite(_relayPin, LOW);
    digitalWrite(_prechargePin, LOW);
    ledcWrite(_pwmPin, 0); // O V pour notifier une erreur irréversible au VE
}

void ChargingManager::forceEmergencyStop() {
    if (isFault()) return;
    _logger.error("[ARRÊT D'URGENCE] Interruption immédiate de l'alimentation.");
    _enterFault(ChargingState::STATE_E);
}

void ChargingManager::resetFault() {
    _logger.warn("[SYSTEM] Demande de déverrouillage de la borne...");

    if (checkRelayGlueFault(true)) {
        _logger.error("DÉBLOCAGE REJETÉ : Le contacteur est soudé mécaniquement !");
        return;
    }

    _state = ChargingState::STATE_A;
    _prechargeSubState = PrechargeStep::IDLE;
    _authorized = false; 
    ledcWrite(_pwmPin, 1023);
    
    _logger.success("[SYSTEM] Borne réinitialisée. Retour à l'état disponible.");
}

// ─────────────────────────────────────────────────────────────────────────────
// BOUCLE PRINCIPALE D'EXECUTION (OPTIMISÉE À 1 SEULE LECTURE ADC)
// ─────────────────────────────────────────────────────────────────────────────
void ChargingManager::update() {
    _cachedRelayCommandedOn      = (_relayPin != -1 && digitalRead(_relayPin) == HIGH);
    _cachedRelayPhysicallyClosed = (_feedbackRelayPin != -1 && digitalRead(_feedbackRelayPin) == LOW);

    if (checkRelayGlueFault(false)) {
        if (!isFault()) {
            _logger.error("ALERTE : Décohérence contacteur détectée en routine !");
            _enterFault(ChargingState::STATE_E);
        }
        return;
    }

    // Cache unique du signal CP
    _latestVcp = readPilotVoltage();
    
    ChargingState detectedState;
    if      (_latestVcp > 10.5f)                detectedState = ChargingState::STATE_A;
    else if (_latestVcp > 8.0f  && _latestVcp < 10.5f) detectedState = ChargingState::STATE_B;
    else if (_latestVcp > 5.0f  && _latestVcp < 8.0f)  detectedState = ChargingState::STATE_C;
    else if (_latestVcp > 2.0f  && _latestVcp < 5.0f)  detectedState = ChargingState::STATE_D;
    else                                                detectedState = ChargingState::STATE_E;

    if (detectedState != _lastRawState) {
        _lastChangeTime = millis();
        _lastRawState   = detectedState;
    }

    if (detectedState != _state && (millis() - _lastChangeTime > CONFIRM_DELAY_MS)) {
        _state = detectedState;
        _prechargeSubState = PrechargeStep::IDLE;

        _logger.info("Changement d'état J1772 : " + getStateString());

        switch (_state) {
            case ChargingState::STATE_A:
                if (_relayPin != -1) digitalWrite(_relayPin, LOW);
                digitalWrite(_prechargePin, LOW);
                ledcWrite(_pwmPin, 1023);
                break;

            case ChargingState::STATE_B:
                if (_relayPin != -1) digitalWrite(_relayPin, LOW);
                digitalWrite(_prechargePin, LOW);
                setMaxCurrent(_config.data.maxAmps);
                break;

            case ChargingState::STATE_C:
                if (isAuthorized()) {
                    _logger.info("[État C] Demande d'énergie autorisée.");
                } else {
                    _state = ChargingState::STATE_B;
                    _logger.warn("[État C] Véhicule connecté non autorisé.");
                }
                break;

            case ChargingState::STATE_D:
                if (isAuthorized()) {
                    if (_config.data.ventilationAvailable) {
                        _logger.warn("[État D] Ventilation confirmée dispo. Lancement charge.");
                    } else {
                        _logger.critical("[État D] Ventilation requise mais non installée ! Repli de sécurité.");
                        _state = ChargingState::STATE_B; 
                    }
                } else {
                    _state = ChargingState::STATE_B;
                }
                break;

            case ChargingState::STATE_E:
            case ChargingState::STATE_F:
                _enterFault(ChargingState::STATE_E);
                break;
        }
    }

    if (_state == ChargingState::STATE_C && _prechargeSubState != PrechargeStep::DONE) {
        tickPrecharge(false);
    }
    if (_state == ChargingState::STATE_D && _prechargeSubState != PrechargeStep::DONE) {
        tickPrecharge(true);
    }

    if ((_state == ChargingState::STATE_C || _state == ChargingState::STATE_D)
        && _prechargeSubState == PrechargeStep::DONE
        && !isAuthorized())
    {
        if (_relayPin != -1) digitalWrite(_relayPin, LOW);
        _state = ChargingState::STATE_B;
        _prechargeSubState = PrechargeStep::IDLE;
        _logger.warn("Session révoquée. Relais ouverts.");
    }
}