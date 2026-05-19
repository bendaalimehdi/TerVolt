#include "web_portal.h"

WebPortal::WebPortal(Logger& logger, ConfigManager& config, ChargingManager& charger, WifiManager& wifi, EnergyManager& energy, TemperatureManager& tempManager)
    : _server(80), _logger(logger), _config(config), _charger(charger), _wifi(wifi), _energy(energy), _tempManager(tempManager) {}

void WebPortal::begin() {
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
        request->send(200, "text/html", generateDashboard());
    });

// --- ROUTE : API Status (Données en temps réel) ---
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;
        
        // État de la borne
        doc["status"] = _charger.getStateString();
        
        // Mesures électriques réelles (ATM90E32)
        doc["voltage_a"] = _energy.getVoltageA();
        doc["current_a"] = _energy.getCurrentA();
        doc["voltage_b"] = _energy.getVoltageB();
        doc["current_b"] = _energy.getCurrentB();
        doc["voltage_c"] = _energy.getVoltageC();
        doc["current_c"] = _energy.getCurrentC();
        doc["power"] = _energy.activePowerTotal();

        // SÉCURISATION : Extraction passive des températures (Pas de collision inter-cœurs)
        doc["temp_l1"] = _tempManager.getPcbEspTemp();         // Température ambiante carte mère
        doc["temp_l2"] = _tempManager.getPcbEnergyTemp();      // Température PCB ATM90
        doc["temp_l3"] = _tempManager.getContacteurTemp();
        doc["temp_esp"] = _tempManager.getInternalSiliconTemp(); // S'assurer que cette méthode renvoie _tempESP dans le .cpp
        doc["overheating"] = _tempManager.isOverheating();
        
        // Infos système
        doc["device_id"] = _config.data.deviceId;
        doc["uptime"] = millis() / 1000;
        doc["ip"] = _wifi.getIP();
        doc["rssi"] = _wifi.getSignalStrength();

        doc["session_active"] = _energy.session.isActive();
        doc["session_kwh"] = _energy.session.getSessionEnergyKwh();
        doc["session_duration"] = _energy.session.getDurationSec();

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _server.begin();
    _logger.success("Portail Web de production lancé sur le port 80");
}

String WebPortal::generateDashboard() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Tervolt | Admin Monitor</title>

    <style>
        :root {
            --bg: #0e1013;
            --card: #1a1d23;
            --border: #2d323a;
            --text: #e0e0e0;
            --muted: #9da5b1;
            --primary: #00d1b2;
            --green: #23d160;
            --orange: #ffdd57;
            --red: #ff3860;
        }

        * {
            box-sizing: border-box;
        }

        body {
            margin: 0;
            padding: 24px;
            font-family: "Segoe UI", Tahoma, Geneva, Verdana, sans-serif;
            background: var(--bg);
            color: var(--text);
        }

        header {
            margin-bottom: 24px;
            border-bottom: 1px solid #333;
            padding-bottom: 16px;
        }

        h1 {
            margin: 0;
            color: var(--primary);
            font-weight: 300;
        }

        h1 small {
            color: #666;
            font-size: 0.5em;
        }

        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
        }

        .card {
            background: var(--card);
            border: 1px solid var(--border);
            border-radius: 14px;
            padding: 20px;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.35);
        }

        .card h2 {
            margin-top: 0;
            margin-bottom: 16px;
            color: var(--primary);
            font-size: 1rem;
            text-transform: uppercase;
            letter-spacing: 1px;
        }

        .row {
            display: flex;
            justify-content: space-between;
            gap: 12px;
            padding: 9px 0;
            border-bottom: 1px solid #262a33;
        }

        .row:last-child {
            border-bottom: none;
        }

        .label {
            color: var(--muted);
        }

        .value {
            color: #fff;
            font-family: monospace;
            font-weight: 600;
            text-align: right;
        }

        .badge {
            padding: 4px 10px;
            border-radius: 999px;
            font-size: 0.8rem;
            font-weight: 700;
        }

        .green {
            background: #23d16033;
            color: var(--green);
        }

        .orange {
            background: #ffdd5733;
            color: var(--orange);
        }

        .red {
            background: #ff386033;
            color: var(--red);
        }

        .progress {
            width: 100%;
            height: 8px;
            margin: 12px 0;
            background: #262a33;
            border-radius: 999px;
            overflow: hidden;
        }

        .progress-bar {
            height: 100%;
            width: 0%;
            background: var(--primary);
            transition: width 0.4s ease;
        }

        footer {
            margin-top: 24px;
            text-align: center;
            color: #555;
            font-size: 0.8rem;
        }

        button {
            width: 100%;
            margin-top: 24px;
            padding: 13px;
            border: none;
            border-radius: 10px;
            background: var(--primary);
            color: white;
            font-size: 1rem;
            cursor: pointer;
        }

        button:hover {
            background: #00b89c;
        }
    </style>
</head>

<body>
    <header>
        <h1>TERVOLT <small>v1.0.0 Production</small></h1>
    </header>

    <main class="grid">

        <section class="card">
            <h2>Système</h2>
            <div class="row"><span class="label">ID Borne</span><span class="value">%DEVICE_ID%</span></div>
            <div class="row"><span class="label">Client</span><span class="value">%CUSTOMER%</span></div>
            <div class="row"><span class="label">Localisation</span><span class="value">%LOCATION%</span></div>
            <div class="row"><span class="label">Uptime</span><span class="value" id="uptime">%UPTIME%</span></div>
        </section>

        <section class="card">
            <h2>Connectivité</h2>
            <div class="row"><span class="label">Adresse IP</span><span class="value" id="ip">%IP%</span></div>
            <div class="row"><span class="label">Signal WiFi</span><span class="badge %RSSI_CLASS%" id="rssi">%RSSI% dBm</span></div>
            <div class="row"><span class="label">Serveur MQTT</span><span class="value">%MQTT%</span></div>
        </section>

        <section class="card">
            <h2>Énergie / ATM90E32</h2>
            <div class="row"><span class="label">Tension L1</span><span class="value" id="voltage_a">-- V</span></div>
            <div class="row"><span class="label">Courant L1</span><span class="value" id="current_a">-- A</span></div>
            <div class="row"><span class="label">Tension L2</span><span class="value" id="voltage_b">-- V</span></div>
            <div class="row"><span class="label">Courant L2</span><span class="value" id="current_b">-- A</span></div>
            <div class="row"><span class="label">Tension L3</span><span class="value" id="voltage_c">-- V</span></div>
            <div class="row"><span class="label">Courant L3</span><span class="value" id="current_c">-- A</span></div>
            <div class="row"><span class="label">Puissance totale</span><span class="value" id="power">-- W</span></div>
        </section>

        <section class="card">
            <h2>Session de charge</h2>
            <div class="row"><span class="label">Activée</span><span class="value" id="session_active">--</span></div>
            <div class="row"><span class="label">Énergie</span><span class="value" id="session_kwh">-- kWh</span></div>
            <div class="row"><span class="label">Durée</span><span class="value" id="session_duration">-- s</span></div>
        </section>

        <section class="card">
            <h2>Hardware</h2>
            <div class="row"><span class="label">Contacteur</span><span class="badge %RELAY_CLASS%">%RELAY%</span></div>
            <div class="row"><span class="label">Pin PWM CP</span><span class="value">%PIN_PWM%</span></div>
            <div class="row"><span class="label">Pin ADC CP</span><span class="value">%PIN_ADC%</span></div>
        </section>

        <section class="card">
            <h2>Logique de charge</h2>
            <div class="row"><span class="label">État J1772</span><span class="badge %STATE_CLASS%" id="state">%STATE%</span></div>
            <div class="row"><span class="label">Signal PWM</span><span class="value">%DUTY% %</span></div>
            <div class="progress">
                <div class="progress-bar" style="width:%DUTY%%"></div>
            </div>
            <div class="row"><span class="label">Courant max</span><span class="value">%MAX_AMPS% A</span></div>
            <div class="row"><span class="label">Status charge</span><span class="badge %CHARGE_CLASS%">%CHARGE_STATUS%</span></div>
        </section>

        <section class="card">
            <h2>Sécurité Thermique</h2>
            <div class="row"><span class="label">Température Borne (ESP32)</span><span class="value" id="temp_esp">-- °C</span></div>
            <div class="row"><span class="label">Bornier Phase L1</span><span class="value" id="temp_l1">-- °C</span></div>
            <div class="row"><span class="label">Bornier Phase L2</span><span class="value" id="temp_l2">-- °C</span></div>
            <div class="row"><span class="label">Bornier Phase L3</span><span class="value" id="temp_l3">-- °C</span></div>
            <div class="row"><span class="label">Statut Thermique</span><span class="badge green" id="thermal_status">NOMINAL</span></div>
        </section>

    </main>

    <button onclick="updateStatus()">ACTUALISER LES DONNÉES</button>

    <footer>
        Mise à jour automatique toutes les 5 secondes.
    </footer>

    <script>
        async function updateStatus() {
            try {
                const response = await fetch('/api/status');
                const data = await response.json();

                document.getElementById('voltage_a').textContent = data.voltage_a.toFixed(1) + ' V';
                document.getElementById('current_a').textContent = data.current_a.toFixed(2) + ' A';
                document.getElementById('voltage_b').textContent = data.voltage_b.toFixed(1) + ' V';
                document.getElementById('current_b').textContent = data.current_b.toFixed(2) + ' A';
                document.getElementById('voltage_c').textContent = data.voltage_c.toFixed(1) + ' V';
                document.getElementById('current_c').textContent = data.current_c.toFixed(2) + ' A';
                document.getElementById('power').textContent = data.power.toFixed(1) + ' W';

                document.getElementById('uptime').textContent = Math.floor(data.uptime / 60) + ' min';
                document.getElementById('session_active').textContent = data.session_active ? 'Oui' : 'Non';
                document.getElementById('session_kwh').textContent = data.session_kwh.toFixed(3) + ' kWh';
                document.getElementById('session_duration').textContent = data.session_duration + ' s';

                document.getElementById('state').textContent = data.status;
                document.getElementById('ip').textContent = data.ip;
                
                // Rafraîchissement asynchrone sécurisé du RSSI
                const rssiElt = document.getElementById('rssi');
                rssiElt.textContent = data.rssi + ' dBm';
                if (data.rssi > -60) {
                    rssiElt.className = 'badge green';
                } else if (data.rssi > -75) {
                    rssiElt.className = 'badge orange';
                } else {
                    rssiElt.className = 'badge red';
                }

                // Affichage fluide des températures
                document.getElementById('temp_esp').textContent = data.temp_esp.toFixed(1) + ' °C';
                document.getElementById('temp_l1').textContent = data.temp_l1.toFixed(1) + ' °C';
                document.getElementById('temp_l2').textContent = data.temp_l2.toFixed(1) + ' °C';
                document.getElementById('temp_l3').textContent = data.temp_l3.toFixed(1) + ' °C';
                
                const thermalElt = document.getElementById('thermal_status');
                if (data.overheating) {
                    thermalElt.textContent = 'Surchauffe';
                    thermalElt.className = 'badge red';
                } else {
                    thermalElt.textContent = 'Normal';
                    thermalElt.className = 'badge green';
                }
            } catch (error) {
                console.error('Erreur API:', error);
            }
        }

        // Lancement immédiat au chargement de la page pour écraser les valeurs par défaut
        window.addEventListener('DOMContentLoaded', () => {
            updateStatus();
            setInterval(updateStatus, 5000);
        });
    </script>
</body>
</html>
)rawliteral";

    int rssi = _wifi.getSignalStrength(); // CORRECTION : Utilisation cohérente du manager réseau
    String rssiClass = (rssi > -60) ? "green" : (rssi > -75 ? "orange" : "red");

    bool relayOn = digitalRead(_config.data.pins.relay);

    float duty = _charger.getDutyCycle();
    ChargingState state = _charger.getState();

    String stateClass;
    switch (state) {
        case ChargingState::STATE_C:
            stateClass = "green";
            break;
        case ChargingState::STATE_A:
        case ChargingState::STATE_B:
        case ChargingState::STATE_D:
            stateClass = "orange";
            break;
        default:
            stateClass = "red";
            break;
    }

    html.replace("%DEVICE_ID%", _config.data.deviceId);
    html.replace("%CUSTOMER%", _config.data.customer_name);
    html.replace("%LOCATION%", _config.data.location);
    html.replace("%UPTIME%", String(millis() / 60000) + " min");

    html.replace("%IP%", _wifi.getIP());
    html.replace("%RSSI%", String(rssi));
    html.replace("%RSSI_CLASS%", rssiClass);
    html.replace("%MQTT%", _config.data.mqttServer);

    html.replace("%RELAY%", relayOn ? "ON" : "OFF");
    html.replace("%RELAY_CLASS%", relayOn ? "green" : "red");
    html.replace("%PIN_PWM%", String(_config.data.pins.cp_pwm));
    html.replace("%PIN_ADC%", String(_config.data.pins.cp_adc));

    html.replace("%STATE%", _charger.getStateString());
    html.replace("%STATE_CLASS%", stateClass);
    html.replace("%DUTY%", String(duty, 1));
    html.replace("%MAX_AMPS%", String(_config.data.maxAmps));
    html.replace("%CHARGE_STATUS%", _charger.isCharging() ? "ACTIVE" : "STANDBY");
    html.replace("%CHARGE_CLASS%", _charger.isCharging() ? "green" : "orange");

    return html;
}