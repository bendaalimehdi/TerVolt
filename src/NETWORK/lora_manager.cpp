#include "lora_manager.h"
#include <sys/time.h>

LoraManager::LoraManager(Logger& logger, ConfigManager& config, ChargingManager& charger, EnergyManager& energy)
    : _logger(logger), _config(config), _charger(charger), _energy(energy), _lastTransmissionTime(0) {}

void LoraManager::begin() {
    if (!_config.data.loraEnabled) return;
    Serial1.begin(9600, SERIAL_8N1, _config.data.pins.pin_lora_rx, _config.data.pins.pin_lora_tx);
    Serial1.setTimeout(50);
}

void LoraManager::maintain() {
    if (!_config.data.loraEnabled) return;

    unsigned long now = millis();
    if (now - _lastTransmissionTime >= _transmissionInterval) {
        _lastTransmissionTime = now;
        sendTelemetry();
    }
    checkForAck();
}

void LoraManager::sendTelemetry() {
    // ─── 1. COMPRESSION DES PINS EN UN SEUL BITFIELD (1 OCTET) ───
    uint8_t pinBitfield = 0;
    
    // Bit 0 : Relay Main (0 = OFF / 1 = ON)
    if (_config.data.pins.relay != -1 && digitalRead(_config.data.pins.relay)) pinBitfield |= (1 << 0);
    // Bit 1 : Relay Precharge
    if (_config.data.pins.precharge != -1 && digitalRead(_config.data.pins.precharge)) pinBitfield |= (1 << 1);
    // Bit 2 : Feedback Relay Glued (0 = OK / 1 = FAULT)
    if (_config.data.pins.feedback_relay != -1 && digitalRead(_config.data.pins.feedback_relay) == LOW) pinBitfield |= (1 << 2);
    // Bit 3 : Config Button
    if (_config.data.pins.btn_config != -1 && digitalRead(_config.data.pins.btn_config) == LOW) pinBitfield |= (1 << 3);

    // ─── 2. RÉCUPÉRATION DES MESURES ANALOGIQUES (ADC) ───
    int cpRaw = (_config.data.pins.cp_adc != -1) ? analogRead(_config.data.pins.cp_adc) : -1;
    float cpVolt = _charger.getLatestPilotVoltage();
    
    int ppRaw = (_config.data.pins.pp_adc != -1) ? analogRead(_config.data.pins.pp_adc) : -1;
    float ppRes = (_config.data.pins.pp_adc != -1) ? _charger.calculatePpResistance(ppRaw) : -1.0f;

    // ─── 3. INDEXATION DU DERNIER LOG ───
    // Au lieu du texte, on extrait un code numérique basé sur le niveau du log
    int logCode = 0; 
    String latestLog = _logger.getLatestLog();
    if (latestLog.indexOf("[CRITICAL]") != -1) logCode = 4;
    else if (latestLog.indexOf("[ERROR]") != -1) logCode = 3;
    else if (latestLog.indexOf("[WARN]") != -1)  logCode = 2;
    else if (latestLog.indexOf("[OK]") != -1)    logCode = 1;

    // ─── 4. CONSTRUCTION DE LA TRAME POSITIONNELLE COMPACTE ───
    // Ordre strict : TV_DATA | ID | TempUnix | Bitfield | CpRaw | CpVolt | PpRaw | PpRes | MaxAmps | FreeHeap | MinHeap | ResetReason | LogCode | SecretKey
    String payload = "TV_DATA|" + _config.data.deviceId +
                     "|" + String((uint32_t)time(nullptr)) +
                     "|" + String(pinBitfield) +
                     "|" + String(cpRaw) +
                     "|" + String(cpVolt, 2) +
                     "|" + String(ppRaw) +
                     "|" + String(ppRes, 0) +
                     "|" + String(_config.data.maxAmps) +
                     "|" + String(ESP.getFreeHeap()) +
                     "|" + String(ESP.getMinFreeHeap()) +
                     "|" + String((int)esp_reset_reason()) +
                     "|" + String(logCode) +
                     "|" + _config.data.loraSecretKey;

    // Envoi radio direct
    Serial1.print(payload);
    _logger.info("LoRa Télémétrie Complète Émise (" + String(payload.length()) + " octets)");
}

void LoraManager::checkForAck() {
    if (Serial1.available()) {
        String response = Serial1.readString();
        response.trim();

        int firstPipe  = response.indexOf('|');
        int secondPipe = response.indexOf('|', firstPipe + 1);
        int thirdPipe  = response.indexOf('|', secondPipe + 1);

        if (firstPipe == -1 || secondPipe == -1 || thirdPipe == -1) return;

        String prefix    = response.substring(0, firstPipe);
        String targetId  = response.substring(firstPipe + 1, secondPipe);
        String timestamp = response.substring(secondPipe + 1, thirdPipe);
        String token     = response.substring(thirdPipe + 1);

        if (prefix == "ACK" && targetId == _config.data.deviceId && token == _config.data.loraSecretKey) {
            time_t gatewayTimestamp = (time_t)timestamp.toInt();
            if (gatewayTimestamp > 1767225600) {
                struct timeval tv;
                tv.tv_sec = gatewayTimestamp;
                tv.tv_usec = 0;
                settimeofday(&tv, NULL);
                _logger.success("🔒 ACK Télémétrie validé. Horloge mise à jour.");
            }
        }
    }
}