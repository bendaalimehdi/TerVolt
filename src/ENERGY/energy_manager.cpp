#include "energy_manager.h"

EnergyManager::EnergyManager(Logger& logger, ConfigManager& config)
    : _logger(logger), _config(config) {}

void EnergyManager::begin() {
    SPI.begin(
        _config.data.pins.spi_sck,
        _config.data.pins.spi_miso,
        _config.data.pins.spi_mosi,
        _config.data.pins.energy_cs
    );

    // Valeurs de départ à calibrer selon ton pont résistif et tes CT.
    const unsigned short lineFreq = 50;
    const unsigned short pgaGain = 1;

    const unsigned short voltageGain = 7305;
    const unsigned short currentGainA = 27961;
    const unsigned short currentGainB = 27961;
    const unsigned short currentGainC = 27961;

    _atm90.begin(
        _config.data.pins.energy_cs,
        lineFreq,
        pgaGain,
        voltageGain,
        currentGainA,
        currentGainB,
        currentGainC
    );

    _logger.success("EnergyManager : ATM90E32 initialisé sur CS " + String(_config.data.pins.energy_cs));
}

void EnergyManager::update() {
    if (millis() - _lastRead <= 1000) {
        return;
    }

    _voltageA = _atm90.GetLineVoltageA();
    _voltageB = _atm90.GetLineVoltageB();
    _voltageC = _atm90.GetLineVoltageC();

    _currentA = _atm90.GetLineCurrentA();
    _currentB = _atm90.GetLineCurrentB();
    _currentC = _atm90.GetLineCurrentC();

    _activePowerA = _atm90.GetActivePowerA();
    _activePowerB = _atm90.GetActivePowerB();
    _activePowerC = _atm90.GetActivePowerC();
    _activePowerTotal = _atm90.GetTotalActivePower();

    

    // if (_config.data.debugMode) {
    //     _logger.info(
    //         "ATM90E32 | "
    //         "VA=" + String(_voltageA) + "V "
    //         "VB=" + String(_voltageB) + "V "
    //         "VC=" + String(_voltageC) + "V | "
    //         "IA=" + String(_currentA) + "A "
    //         "IB=" + String(_currentB) + "A "
    //         "IC=" + String(_currentC) + "A | "
    //         "P=" + String(_activePowerTotal) + "W"
    //     );
    // }
    session.update(_activePowerTotal);
    _lastRead = millis();
}