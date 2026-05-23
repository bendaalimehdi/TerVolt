#include "charging_manager.h"

const int PWM_FREQ = 1000;
const int PWM_RES  = 10;

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / begin
// ─────────────────────────────────────────────────────────────────────────────

ChargingManager::ChargingManager(Logger& logger, ConfigManager& config)
    : _logger(logger), _config(config),
      _lastRawState(ChargingState::STATE_A),   // FIX #2 : plus de static dans update()
      _lastChangeTime(0),
      _prechargeStartTime(0),
      _relayCloseTime(0),
      _prechargeSubState(PrechargeStep::IDLE)  // FIX #3 : FSM précharge sans delay()
{
    _pwmPin          = _config.data.pins.cp_pwm;
    _adcPin          = _config.data.pins.cp_adc;
    _relayPin        = _config.data.pins.relay;
    _prechargePin    = _config.data.pins.precharge;
    _feedbackRelayPin = _config.data.pins.feedback_relay;
}

void ChargingManager::begin() {
    _pwmPin           = _config.data.pins.cp_pwm;
    _adcPin           = _config.data.pins.cp_adc;
    _relayPin         = _config.data.pins.relay;
    _prechargePin     = _config.data.pins.precharge;
    _feedbackRelayPin = _config.data.pins.feedback_relay;

    pinMode(_adcPin, INPUT);
    pinMode(_prechargePin, OUTPUT);
    pinMode(_feedbackRelayPin, INPUT_PULLUP);

    if (_relayPin != 0) {
        pinMode(_relayPin, OUTPUT);
        digitalWrite(_relayPin, LOW);
    }

    // API ESP32 v3.0
    if (!ledcAttach(_pwmPin, PWM_FREQ, PWM_RES)) {
        _logger.error("Échec initialisation LEDC (PWM)");
    }
    ledcWrite(_pwmPin, 1023);  // 12 V constant au repos
    _logger.success("ChargingManager initialisé");
}

// ─────────────────────────────────────────────────────────────────────────────
// Sécurité contacteur
// ─────────────────────────────────────────────────────────────────────────────

bool ChargingManager::checkRelayGlueFault() {
    bool relayCommandedOn    = (digitalRead(_relayPin) == HIGH);
    bool relayPhysicallyClosed = (digitalRead(_feedbackRelayPin) == LOW); // LOW = contact auxiliaire fermé

    // Cas 1 : Collage — relais commandé OFF mais physiquement fermé
    if (!relayCommandedOn && relayPhysicallyClosed) {
        _logger.error("DÉFAUT SÉCURITÉ : Contacteur collé ou soudé !");
        return true;
    }

    // Cas 2 : Échec d'enclenchement — relais commandé ON mais physiquement ouvert
    if (relayCommandedOn && !relayPhysicallyClosed) {
        _logger.error("DÉFAUT MATÉRIEL : Échec d'enclenchement du contacteur principal !");
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Courant / PWM
// ─────────────────────────────────────────────────────────────────────────────

void ChargingManager::setMaxCurrent(float amps) {
    _targetAmps = amps;
    float dutyPercent = _targetAmps / 0.6f;
    setPWM(dutyPercent);
}

void ChargingManager::setPWM(float dutyCyclePercent) {
    int dutyValue = (dutyCyclePercent / 100.0f) * 1023;
    ledcWrite(_pwmPin, dutyValue);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lecture tension CP (J1772 : crête positive)
// FIX : on prend le maximum des échantillons plutôt que la moyenne,
//       conformément à la norme J1772 qui spécifie la lecture de crête.
// ─────────────────────────────────────────────────────────────────────────────

float ChargingManager::readPilotVoltage() {
    int   peak    = 0;
    const int samples = 25;

    for (int i = 0; i < samples; i++) {
        int val = analogRead(_adcPin);
        if (val > peak) peak = val;
        delayMicroseconds(50);
    }

    float voltage = (peak / 4095.0f) * 3.3f * 4.0f;  // Ratio pont diviseur
    return voltage;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers d'état
// ─────────────────────────────────────────────────────────────────────────────

bool ChargingManager::isVehicleConnected() {
    return (readPilotVoltage() < 10.5f);
}

bool ChargingManager::isVehicleRequestingCharge() {
    float vcp = readPilotVoltage();
    return (vcp > 5.0f && vcp < 7.5f);
}

bool ChargingManager::isAuthorized() const {
    return true;  // À remplacer par la logique OCPP / RFID
}

bool ChargingManager::isFault() const {
    return (_state == ChargingState::STATE_E || _state == ChargingState::STATE_F);
}

bool ChargingManager::isCharging() {
    return isVehicleRequestingCharge() && isAuthorized();
}

// FIX #6 : calcul du duty cycle conforme J1772 sur toute la plage (6–80 A)
float ChargingManager::getDutyCycle() const {
    if (_state == ChargingState::STATE_A) return 100.0f;
    if (_targetAmps <= 0) return 100.0f;

    float duty;
    if (_targetAmps <= 51.0f) {
        duty = _targetAmps / 0.6f;        // 6–51 A  → 10 %–85 %
    } else {
        duty = (_targetAmps + 64.0f) / 2.5f; // 51–80 A → 46 %–58 % (plage haute J1772)
    }

    if (duty > 100.0f) duty = 100.0f;
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

// ─────────────────────────────────────────────────────────────────────────────
// FSM de précharge non-bloquante
// FIX #3 : remplace les delay(500) / delay(100) dans update()
//          Appelée à chaque cycle depuis update() quand STATE_C ou STATE_D est actif.
// ─────────────────────────────────────────────────────────────────────────────

void ChargingManager::tickPrecharge(bool requiresVentilation) {
    switch (_prechargeSubState) {

        case PrechargeStep::IDLE:
            // Vérification pré-charge : contacteur bien ouvert au repos ?
            if (checkRelayGlueFault()) {
                _logger.error("[PRÉ-CHARGE] Refus de démarrer : cohérence contacteur invalide au repos !");
                _enterFault();
                return;
            }
            _logger.info("[PRÉ-CHARGE] Validation OK. Activation de la précharge...");
            digitalWrite(_prechargePin, HIGH);
            _prechargeStartTime = millis();
            _prechargeSubState  = PrechargeStep::WAITING_PRECHARGE;
            break;

        case PrechargeStep::WAITING_PRECHARGE:
            if (millis() - _prechargeStartTime >= PRECHARGE_DELAY_MS) {
                // Fermeture du contacteur principal
                digitalWrite(_relayPin, HIGH);
                _relayCloseTime   = millis();
                _prechargeSubState = PrechargeStep::WAITING_RELAY_CLOSE;
            }
            break;

        case PrechargeStep::WAITING_RELAY_CLOSE:
            if (millis() - _relayCloseTime >= RELAY_SETTLE_MS) {
                // FIX #5 (état D) : même vérification que C après fermeture
                if (checkRelayGlueFault()) {
                    _logger.error("[PRÉ-CHARGE] Échec critique : le contacteur n'a pas réussi à se fermer !");
                    digitalWrite(_relayPin, LOW);
                    digitalWrite(_prechargePin, LOW);
                    _enterFault();
                    return;
                }
                digitalWrite(_prechargePin, LOW);  // Précharge terminée, tout est OK
                _prechargeSubState = PrechargeStep::DONE;

                if (requiresVentilation) {
                    _logger.warn("[État D] Sécurités validées. Contacteur ON. Ventilation requise — charge activée.");
                } else {
                    _logger.success("[État C] Sécurités validées. Contacteur principal ON. Charge active !");
                }
            }
            break;

        case PrechargeStep::DONE:
            // Rien à faire : la charge est en cours, surveille depuis update()
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Entrée en fault (helper interne)
// ─────────────────────────────────────────────────────────────────────────────

void ChargingManager::_enterFault() {
    _state = ChargingState::STATE_E;
    _prechargeSubState = PrechargeStep::IDLE;
    digitalWrite(_relayPin, LOW);
    digitalWrite(_prechargePin, LOW);
    ledcWrite(_pwmPin, 0);
}

float ChargingManager::getLatestPilotVoltage() {
    return readPilotVoltage(); // Retourne le calcul déjà existant dans ton fichier
}

float ChargingManager::calculatePpResistance(int adcRaw) {
    if (adcRaw == 0) return 0.0f;
    // Formule classique de pont diviseur : Vout = Vcc * R2 / (R1 + R2)
    // À ajuster selon les valeurs exactes de tes résistances de tirage (ex: Pull-up de 1kOhm sous 3.3V)
    float vOut = (adcRaw * 3.3f) / 4095.0f;
    if (vOut >= 3.2f) return 999999.0f; // Circuit ouvert (Pas de câble)
    return (1000.0f * vOut) / (3.3f - vOut); 
}

// ─────────────────────────────────────────────────────────────────────────────
// update() — boucle principale
// ─────────────────────────────────────────────────────────────────────────────

void ChargingManager::update() {

    // ── 1. Sécurité prioritaire : contacteur collé ────────────────────────────
    if (checkRelayGlueFault()) {
        if (_state != ChargingState::STATE_E) {
            _logger.error("ALERTE CRITIQUE : CONTACTEUR COLLÉ / SOUDÉ ! Arrêt d'urgence matériel.");
            _enterFault();
        }
        return;
    }

    // ── 2. Lecture CP et détection d'état brut ───────────────────────────────
    float vcp = readPilotVoltage();
    ChargingState detectedState;

    if      (vcp > 10.5f)                detectedState = ChargingState::STATE_A;
    else if (vcp > 8.0f  && vcp < 10.5f) detectedState = ChargingState::STATE_B;
    else if (vcp > 5.0f  && vcp < 8.0f)  detectedState = ChargingState::STATE_C;
    else if (vcp > 2.0f  && vcp < 5.0f)  detectedState = ChargingState::STATE_D;
    else                                  detectedState = ChargingState::STATE_E;

    // ── 3. Anti-rebond ───────────────────────────────────────────────────────
    // FIX #2 : _lastRawState et _lastChangeTime sont membres de classe, plus static
    if (detectedState != _lastRawState) {
        _lastChangeTime = millis();
        _lastRawState   = detectedState;
    }

    // ── 4. Transition d'état (si stable pendant CONFIRM_DELAY) ───────────────
    if (detectedState != _state && (millis() - _lastChangeTime > CONFIRM_DELAY_MS)) {

        // FIX #1 : sauvegarder previousState AVANT de modifier _state
        ChargingState previousState = _state;
        _state = detectedState;
        _prechargeSubState = PrechargeStep::IDLE;  // Réinitialise la FSM de précharge

        _logger.info("Changement d'état confirmé : " + getStateString());

        switch (_state) {

            case ChargingState::STATE_A:
                digitalWrite(_relayPin, LOW);
                digitalWrite(_prechargePin, LOW);
                ledcWrite(_pwmPin, 1023);  // 12 V constant

                // Post-charge : vérification que le contacteur s'est bien ouvert
                // FIX #4 : plus de delay(50) bloquant → on relit l'état GPIO directement
                //          (le contacteur a déjà eu le temps de s'ouvrir depuis la commande LOW)
                if (checkRelayGlueFault()) {
                    _logger.error("[POST-CHARGE] Contacteur resté collé après débranchement !");
                    _enterFault();
                } else {
                    _logger.info("[État A] Véhicule débranché. Sécurité contacteur validée (ouvert).");
                }
                break;

            case ChargingState::STATE_B:
                digitalWrite(_relayPin, LOW);
                digitalWrite(_prechargePin, LOW);
                setMaxCurrent(_config.data.maxAmps);

                // Post-charge si la voiture interrompt la charge (C → B)
                if (previousState == ChargingState::STATE_C || previousState == ChargingState::STATE_D) {
                    if (checkRelayGlueFault()) {
                        _logger.error("[POST-CHARGE] La charge s'est arrêtée mais le contacteur refuse de s'ouvrir !");
                        _enterFault();
                    } else {
                        _logger.info("[État B] Charge interrompue. Sécurité contacteur validée (ouvert).");
                    }
                } else {
                    _logger.info("[État B] Véhicule connecté en attente.");
                }
                break;

            case ChargingState::STATE_C:
                if (isAuthorized()) {
                    // La séquence de précharge est maintenant gérée par tickPrecharge()
                    // appelée ci-dessous dans la section "état courant"
                    _logger.info("[État C] Démarrage séquence de précharge...");
                } else {
                    _state = ChargingState::STATE_B;
                    _logger.warn("[État C] Véhicule demande la charge mais non autorisé.");
                }
                break;

            case ChargingState::STATE_D:
                if (isAuthorized()) {
                    // FIX #5 : même séquence sécurisée que C via tickPrecharge()
                    _logger.warn("[État D] Démarrage séquence précharge (ventilation requise)...");
                } else {
                    _state = ChargingState::STATE_B;
                    _logger.warn("[État D] Non autorisé.");
                }
                break;

            case ChargingState::STATE_E:
            case ChargingState::STATE_F:
                _enterFault();
                _logger.error("[État E/F] Fault détecté ! Relais ouvert, CP coupé.");
                break;

            default:
                break;
        }
    }

    // ── 5. Tick de la FSM de précharge (non-bloquant) ────────────────────────
    if (_state == ChargingState::STATE_C && _prechargeSubState != PrechargeStep::DONE) {
        tickPrecharge(false);
    }
    if (_state == ChargingState::STATE_D && _prechargeSubState != PrechargeStep::DONE) {
        tickPrecharge(true);
    }

    // ── 6. Surveillance continue en charge ───────────────────────────────────
    if ((_state == ChargingState::STATE_C || _state == ChargingState::STATE_D)
        && _prechargeSubState == PrechargeStep::DONE
        && !isAuthorized())
    {
        digitalWrite(_relayPin, LOW);
        _state = ChargingState::STATE_B;
        _prechargeSubState = PrechargeStep::IDLE;
        _logger.warn("Autorisation perdue en cours de charge. Relais ouvert.");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Arrêt d'urgence
// ─────────────────────────────────────────────────────────────────────────────

void ChargingManager::forceEmergencyStop() {
    if (_state == ChargingState::STATE_E) return;  // Idempotent
    _logger.error("[ARRÊT D'URGENCE] Tous les relais sont coupés. Borne verrouillée.");
    _enterFault();
}

void ChargingManager::resetFault() {
    if (_state != ChargingState::STATE_E && _state != ChargingState::STATE_F) {
        return; // Pas besoin de réinitialiser si la borne n'est pas en défaut
    }

    _logger.warn("[SYSTEM] Tentative de réinitialisation après un arrêt d'urgence...");

    // 🛡️ VÉRIFICATION SÉCURITÉ CRITIQUE : Le contacteur est-il toujours soudé ?
    if (checkRelayGlueFault()) {
        _logger.error("DÉBLOCAGE REFUSÉ : Le contacteur principal est physiquement bloqué ou collé !");
        return;
    }

    // Si tout est OK, on réinitialise l'état pour forcer un nouveau scan du Control Pilot
    _state = ChargingState::STATE_A;
    _prechargeSubState = PrechargeStep::IDLE;
    
    // On remet le signal CP à 12V constant pour détecter si un véhicule est branché
    ledcWrite(_pwmPin, 1023);
    
    _logger.success("[SYSTEM] Borne déverrouillée avec succès. Retour à l'état A (Libre).");
}