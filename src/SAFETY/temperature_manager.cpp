#include "temperature_manager.h"
#include "driver/temperature_sensor.h"
#include <math.h>

TemperatureManager::TemperatureManager(Logger& logger, ConfigManager& config)
    : _logger(logger),
      _config(config),
      _oneWire(nullptr),
      _sensors(nullptr),
      _espTempSensor(NULL),
      _oneWirePin(18),
      _tempL1(NAN),
      _tempL2(NAN),
      _tempL3(NAN),
      _tempESP(NAN),
      _lastReadTime(0),
      _readInterval(2000) {
}

TemperatureManager::~TemperatureManager() {
    if (_espTempSensor) {
        temperature_sensor_disable(_espTempSensor);
        temperature_sensor_uninstall(_espTempSensor);
        _espTempSensor = NULL;
    }

    if (_sensors) {
        delete _sensors;
        _sensors = nullptr;
    }

    if (_oneWire) {
        delete _oneWire;
        _oneWire = nullptr;
    }
}

void TemperatureManager::begin() {
    _oneWire = new OneWire(_oneWirePin);
    _sensors = new DallasTemperature(_oneWire);
    _sensors->begin();

    int sensorCount = _sensors->getDeviceCount();

    if (sensorCount > 0) {
        _logger.success("TemperatureManager : Bus OneWire initialisé. Sondes détectées : " + String(sensorCount));
    } else {
        _logger.error("TemperatureManager : aucune sonde DS18B20 détectée.");
    }

    temperature_sensor_config_t temp_sensor_config =
        TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);

    esp_err_t err = temperature_sensor_install(&temp_sensor_config, &_espTempSensor);

    if (err == ESP_OK) {
        err = temperature_sensor_enable(_espTempSensor);

        if (err == ESP_OK) {
            _logger.success("Capteur thermique interne ESP32 initialisé.");
        } else {
            _logger.error("Échec activation capteur thermique ESP32.");
            temperature_sensor_uninstall(_espTempSensor);
            _espTempSensor = NULL;
        }
    } else {
        _logger.error("Échec installation capteur thermique ESP32.");
        _espTempSensor = NULL;
    }
}

void TemperatureManager::update() {
    if (millis() - _lastReadTime < _readInterval) {
        return;
    }

    _lastReadTime = millis();

    if (_sensors) {
        _sensors->requestTemperatures();

        float t1 = _sensors->getTempCByIndex(0);
        float t2 = _sensors->getTempCByIndex(1);
        float t3 = _sensors->getTempCByIndex(2);

        _tempL1 = (t1 != DEVICE_DISCONNECTED_C) ? t1 : NAN;
        _tempL2 = (t2 != DEVICE_DISCONNECTED_C) ? t2 : NAN;
        _tempL3 = (t3 != DEVICE_DISCONNECTED_C) ? t3 : NAN;
    }

    _tempESP = readInternalSiliconTemp();
}

float TemperatureManager::getTerminalTemp(int phase) {
    if (phase == 1) return _tempL1;
    if (phase == 2) return _tempL2;
    if (phase == 3) return _tempL3;
    return NAN;
}

float TemperatureManager::getInternalESPTemp() {
    return _tempESP;
}

float TemperatureManager::readInternalSiliconTemp() {
    if (_espTempSensor == NULL) {
        return NAN;
    }

    float tsens_out = NAN;

    if (temperature_sensor_get_celsius(_espTempSensor, &tsens_out) == ESP_OK) {
        return tsens_out;
    }

    return NAN;
}

bool TemperatureManager::isOverheating() {
    float maxEspTemp = _config.data.temp_max_celsius;
    float maxTerminalTemp = 80.0f;

    if (isnan(_tempESP)) {
        _logger.error("Erreur capteur température interne ESP32.");
        return true;
    }

    if (isnan(_tempL1) || isnan(_tempL2) || isnan(_tempL3)) {
        //_logger.error("Erreur sonde température puissance déconnectée.");
        return true;
    }

    if (_tempESP > maxEspTemp) {
        _logger.error("SURCHAUFFE CRITIQUE : ESP32 à " + String(_tempESP) + "°C");
        return true;
    }

    if (_tempL1 > maxTerminalTemp || _tempL2 > maxTerminalTemp || _tempL3 > maxTerminalTemp) {
        _logger.error(
            "SURCHAUFFE CRITIQUE BORNIERS : L1=" + String(_tempL1) +
            "°C, L2=" + String(_tempL2) +
            "°C, L3=" + String(_tempL3) + "°C"
        );
        return true;
    }

    return false;
}