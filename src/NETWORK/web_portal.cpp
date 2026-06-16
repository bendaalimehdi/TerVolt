#include "web_portal.h"

WebPortal::WebPortal(Logger& logger, ConfigManager& config, ChargingManager& charger, WifiManager& wifi, EnergyManager& energy, TemperatureManager& tempManager)
    : _server(80), _logger(logger), _config(config), _charger(charger), _wifi(wifi), _energy(energy), _tempManager(tempManager) {}

extern DiagnosticsManager diagnostics;

void WebPortal::begin() {
    auto redirectToPortal = [](AsyncWebServerRequest *request) {
        String url = "http://" + WiFi.softAPIP().toString() + "/";
        request->redirect(url);
    };

    // 📡 Démarrage du serveur DNS Captif
    _dnsServer.start(53, "*", WiFi.softAPIP());
    _dnsStarted = true;

    // ─── Captures de Connectivité OS (Android, Apple, Windows) ────
    _server.on("/generate_204", HTTP_GET, redirectToPortal);
    _server.on("/gen_204", HTTP_GET, redirectToPortal);
    _server.on("/connectivity-check/generate_204", HTTP_GET, redirectToPortal);
    _server.on("/hotspot-detect.html", HTTP_GET, redirectToPortal);
    _server.on("/library/test/success.html", HTTP_GET, redirectToPortal);
    _server.on("/success.txt", HTTP_GET, redirectToPortal);
    _server.on("/ncsi.txt", HTTP_GET, redirectToPortal);
    _server.on("/connecttest.txt", HTTP_GET, redirectToPortal);
    _server.on("/redirect", HTTP_GET, redirectToPortal);

    // --- ROUTE : Page d'accueil du Dashboard ---
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
        String html = generateDashboard();

        // 🔄 INJECTION DYNAMIQUE DES ANCIENNES ET NOUVELLES VARIABLES (Résout l'erreur de compilation)
        int rssi = _wifi.getSignalStrength();
        String rssiClass = (rssi > -60) ? "green" : (rssi > -75 ? "orange" : "red");
        bool relayOn = digitalRead(_config.data.pins.relay);
        float duty = _charger.getDutyCycle();
        ChargingState state = _charger.getState();

        String stateClass;
        switch (state) {
            case ChargingState::STATE_C: stateClass = "green"; break;
            case ChargingState::STATE_A:
            case ChargingState::STATE_B:
            case ChargingState::STATE_D: stateClass = "orange"; break;
            default: stateClass = "red"; break;
        }

        // Hydratation des cartes de monitoring d'origine
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
        html.replace("%CHARGE_STATUS%", _charger.isCharging() ? "ACTIVE" : "STANDBY");
        html.replace("%CHARGE_CLASS%", _charger.isCharging() ? "green" : "orange");

        // Hydratation du nouveau formulaire de configuration d'installation
        html.replace("%DEVICE_ID%", _config.data.deviceId);
        html.replace("%WIFI_SSID%", _config.data.ssid);
        html.replace("%WIFI_PASS%", _config.data.password);
        html.replace("%AP_PASS%", _config.data.ap_password);
        html.replace("%MQTT_SERVER%", _config.data.mqttServer);
        html.replace("%OC_THRES%", String(_config.data.overcurrentThreshold, 1));
        html.replace("%MAX_AMPS%", String(_config.data.maxAmps));
        
        html.replace("%SEL_16%", _config.data.maxAmps == 16 ? "selected" : "");
        html.replace("%SEL_32%", _config.data.maxAmps == 32 ? "selected" : "");
        html.replace("%LORA_CHECKED%", _config.data.loraEnabled ? "checked" : "");
        html.replace("%DEBUG_CHECKED%", _config.data.debugMode ? "checked" : "");

        request->send(200, "text/html", html);
    });

    _server.on("/api/diagnostics", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", diagnostics.getDiagnosticsJson());
    });

    // --- ROUTE : API Status (Données en temps réel) ---
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;
        
        doc["status"] = _charger.getStateString();
        doc["voltage_a"] = _energy.getVoltageA();
        doc["current_a"] = _energy.getCurrentA();
        doc["voltage_b"] = _energy.getVoltageB();
        doc["current_b"] = _energy.getCurrentB();
        doc["voltage_c"] = _energy.getVoltageC();
        doc["current_c"] = _energy.getCurrentC();
        doc["power"] = _energy.activePowerTotal();

        doc["temp_l1"] = _tempManager.getPcbEspTemp();         
        doc["temp_l2"] = _tempManager.getPcbEnergyTemp();      
        doc["temp_l3"] = _tempManager.getContacteurTemp();
        doc["temp_esp"] = _tempManager.getInternalSiliconTemp(); 
        doc["overheating"] = _tempManager.isOverheating();
        
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

    // --- ROUTE : API WiFi Scan (Asynchrone à la demande) ---
    _server.on("/api/wifi-scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        int n = WiFi.scanNetworks(false, false, false, 300);
        JsonDocument doc;
        JsonArray networks = doc.to<JsonArray>();
        
        for (int i = 0; i < n; ++i) {
            JsonObject net = networks.add<JsonObject>();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured";
        }
        WiFi.scanDelete();

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // --- ROUTE : API Sauvegarde de la nouvelle configuration ---
    _server.on("/api/save-config", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, 
    [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        
        if (error) {
            request->send(400, "application/json", "{\"status\":\"error\",\"msg\":\"JSON Invalide\"}");
            return;
        }

        if(doc.containsKey("wifi_ssid")) _config.data.ssid = doc["wifi_ssid"].as<String>();
        if(doc.containsKey("wifi_pass")) _config.data.password = doc["wifi_pass"].as<String>();
        if(doc.containsKey("ap_password")) _config.data.ap_password = doc["ap_password"].as<String>();
        if(doc.containsKey("mqtt_server")) _config.data.mqttServer = doc["mqtt_server"].as<String>();
        if(doc.containsKey("lora_enabled")) _config.data.loraEnabled = doc["lora_enabled"].as<bool>();
        if(doc.containsKey("debug_mode")) _config.data.debugMode = doc["debug_mode"].as<bool>();
        
        if(doc.containsKey("max_current_amps")) _config.data.maxAmps = doc["max_current_amps"].as<int>();
        if(doc.containsKey("overcurrent_threshold")) _config.data.overcurrentThreshold = doc["overcurrent_threshold"].as<float>();

        if (_config.save()) {
            _logger.success("Fichier config.json réécrit avec succès suite aux modifications.");
            request->send(200, "application/json", "{\"status\":\"ok\",\"msg\":\"Configuration modifiée ! Veuillez redémarrer la borne.\"}");
        } else {
            request->send(500, "application/json", "{\"status\":\"error\",\"msg\":\"Erreur d'écriture Flash\"}");
        }
    });

    // --- ROUTE : API de Redémarrage ---
    _server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"ok\",\"msg\":\"ESP32 en cours de redémarrage matériel...\"}");
        _logger.warn("Reboot logiciel déclenché.");
        delay(1000);
        ESP.restart();
    });

    // 🚨 Redirection universelle Captive
    _server.onNotFound([this](AsyncWebServerRequest *request) {
        String urlRedirect = "http://" + WiFi.softAPIP().toString() + "/";
        request->redirect(urlRedirect);
    });

    _server.begin();
    _logger.success("Portail Web de production lancé sur le port 80 avec redirection captive");
}

void WebPortal::handleDNS() {
    if (_dnsStarted) {
        _dnsServer.processNextRequest();
    }
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

        * { box-sizing: border-box; }

        body {
            margin: 0; padding: 24px;
            font-family: "Segoe UI", Tahoma, Geneva, sans-serif;
            background: var(--bg); color: var(--text);
        }

        header {
            margin-bottom: 24px; border-bottom: 1px solid #333; padding-bottom: 16px;
            display: flex; justify-content: space-between; align-items: center;
        }

        h1 { margin: 0; color: var(--primary); font-weight: 300; }
        h1 small { color: #666; font-size: 0.5em; }

        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
        }

        .card {
            background: var(--card); border: 1px solid var(--border);
            border-radius: 14px; padding: 20px; box-shadow: 0 4px 15px rgba(0, 0, 0, 0.35);
        }

        .card h2 {
            margin-top: 0; margin-bottom: 16px; color: var(--primary);
            font-size: 1rem; text-transform: uppercase; letter-spacing: 1px;
        }

        .row {
            display: flex; justify-content: space-between; gap: 12px;
            padding: 9px 0; border-bottom: 1px solid #262a33;
        }

        .row:last-child { border-bottom: none; }
        .label { color: var(--muted); }
        .value { color: #fff; font-family: monospace; font-weight: 600; text-align: right; }
        .badge { padding: 4px 10px; border-radius: 999px; font-size: 0.8rem; font-weight: 700; }
        .green { background: #23d16033; color: var(--green); }
        .orange { background: #ffdd5733; color: var(--orange); }
        .red { background: #ff386033; color: var(--red); }

        .progress {
            width: 100%; height: 8px; margin: 12px 0;
            background: #262a33; border-radius: 999px; overflow: hidden;
        }
        .progress-bar { height: 100%; width: 0%; background: var(--primary); transition: width 0.4s ease; }
        footer { margin-top: 24px; text-align: center; color: #555; font-size: 0.8rem; }

        /* Style Formulaires */
        .form-group { margin-bottom: 15px; display: flex; flex-direction: column; }
        .form-group label { margin-bottom: 6px; color: var(--muted); font-size: 0.9rem; }
        .form-control {
            background: #0e1013; border: 1px solid var(--border); border-radius: 6px;
            padding: 10px; color: white; font-family: monospace; font-size: 1rem;
        }
        .form-control:focus { border-color: var(--primary); outline: none; }
        .inline-group { display: flex; gap: 10px; align-items: center; }
        
        .btn {
            padding: 11px 16px; border: none; border-radius: 8px;
            font-weight: 600; cursor: pointer; transition: background 0.2s;
            font-size: 0.9rem; text-transform: uppercase;
        }
        .btn-primary { background: var(--primary); color: #0e1013; }
        .btn-primary:hover { background: #00b89c; }
        .btn-sec { background: #2d323a; color: white; }
        .btn-sec:hover { background: #3a414c; }
        .btn-danger { background: var(--red); color: white; width: auto; margin-top: 0; }
        .btn-danger:hover { background: #e02447; }
        .btn-update { width: 100%; margin-top: 24px; padding: 13px; background: var(--primary); color: white; border-radius: 10px; }

        /* Modal Scanner Popup */
        .modal {
            display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%;
            background: rgba(0,0,0,0.85); justify-content: center; align-items: center; z-index: 1000;
        }
        .modal-content {
            background: var(--card); border: 1px solid var(--border); padding: 25px;
            border-radius: 14px; width: 90%; max-width: 450px; max-height: 75vh; overflow-y: auto;
        }
        .wifi-item {
            padding: 12px; border-bottom: 1px solid var(--border); cursor: pointer;
            display: flex; justify-content: space-between; align-items: center;
        }
        .wifi-item:hover { background: #2d323a; }
    </style>
</head>

<body>
    <header>
        <h1>TERVOLT <small>v1.0.0 Production</small></h1>
        <button class="btn btn-danger" onclick="rebootDevice()">REDÉMARRER LA BORNE</button>
    </header>

    <div class="grid">

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
            <div class="row"><span class="label">Courant max</span><span class="value" id="max_amps_val">%MAX_AMPS% A</span></div>
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

        <section class="card">
            <h2>Déploiement & Configuration d'Usine</h2>
            <form id="configForm" onsubmit="saveConfig(event)">
                
                <div class="form-group">
                    <label>SSID Wi-Fi Réseau Principal</label>
                    <div class="inline-group">
                        <input type="text" id="wifi_ssid" class="form-control" style="flex:1;" value="%WIFI_SSID%" required>
                        <button type="button" class="btn btn-sec" onclick="scanWifi()">Scanner</button>
                    </div>
                </div>

                <div class="form-group">
                    <label>Mot de passe Wi-Fi Principal</label>
                    <input type="password" id="wifi_pass" class="form-control" value="%WIFI_PASS%">
                </div>

                <div class="form-group">
                    <label>Mot de passe Wi-Fi AP Local (Sécurité Borne)</label>
                    <input type="text" id="ap_password" class="form-control" value="%AP_PASS%" required>
                </div>

                <div class="form-group">
                    <label>Broker MQTT (IP ou Hostname)</label>
                    <input type="text" id="mqtt_server" class="form-control" value="%MQTT_SERVER%" required>
                </div>

                <div class="form-group">
                    <label>Courant Max de Charge (Limitation Physique)</label>
                    <select id="max_current_amps" class="form-control">
                        <option value="16" %SEL_16%>16 A (Triphasé 11 kW)</option>
                        <option value="32" %SEL_32%>32 A (Triphasé 22 kW)</option>
                    </select>
                </div>

                <div class="form-group">
                    <label>Seuil Déclenchement Surintensité (Ampères)</label>
                    <input type="number" step="0.1" id="overcurrent_threshold" class="form-control" value="%OC_THRES%" required>
                </div>

                <div class="form-group" style="display: flex; flex-direction: row; gap: 20px; margin-top: 10px;">
                    <label style="cursor:pointer; color:var(--muted);"><input type="checkbox" id="lora_enabled" %LORA_CHECKED%> Module LoRa</label>
                    <label style="cursor:pointer; color:var(--muted);"><input type="checkbox" id="debug_mode" %DEBUG_CHECKED%> Mode Debug</label>
                </div>

                <button type="submit" class="btn btn-primary" style="width:100%; margin-top: 15px; border-radius:8px;">Appliquer les modifications</button>
            </form>
        </section>

    </div>

    <button class="btn btn-update" onclick="updateStatus()">ACTUALISER LES DONNÉES</button>

    <div id="wifiModal" class="modal">
        <div class="modal-content">
            <h2 style="color:var(--primary); margin-top:0; font-size:1.1rem;">Réseaux Disponibles</h2>
            <div id="wifiList">Recherche des réseaux Wi-Fi...</div>
            <button class="btn btn-sec" style="width:100%; margin-top:15px; border-radius:6px;" onclick="closeModal()">Fermer</button>
        </div>
    </div>

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
                document.getElementById('max_amps_val').textContent = data.max_current_amps + ' A';
                
                const rssiElt = document.getElementById('rssi');
                rssiElt.textContent = data.rssi + ' dBm';
                if (data.rssi > -60) { rssiElt.className = 'badge green'; } 
                else if (data.rssi > -75) { rssiElt.className = 'badge orange'; } 
                else { rssiElt.className = 'badge red'; }

                document.getElementById('temp_esp').textContent = data.temp_esp.toFixed(1) + ' °C';
                document.getElementById('temp_l1').textContent = data.temp_l1.toFixed(1) + ' °C';
                document.getElementById('temp_l2').textContent = data.temp_l2.toFixed(1) + ' °C';
                document.getElementById('temp_l3').textContent = data.temp_l3.toFixed(1) + ' °C';
                
                const thermalElt = document.getElementById('thermal_status');
                if (data.overheating) {
                    thermalElt.textContent = 'Surchauffe'; thermalElt.className = 'badge red';
                } else {
                    thermalElt.textContent = 'Normal'; thermalElt.className = 'badge green';
                }
            } catch (error) { console.error('Erreur API:', error); }
        }

        async function scanWifi() {
            document.getElementById('wifiModal').style.display = 'flex';
            document.getElementById('wifiList').innerHTML = 'Balayage des ondes...';
            try {
                const response = await fetch('/api/wifi-scan');
                const list = await response.json();
                let html = '';
                list.forEach(net => {
                    html += `<div class="wifi-item" onclick="selectWifi('${net.ssid}')">
                                <span>📡 <b>${net.ssid}</b></span>
                                <span style="color:var(--muted); font-size:0.85rem;">${net.rssi} dBm (${net.encryption})</span>
                             </div>`;
                });
                document.getElementById('wifiList').innerHTML = html || 'Aucun point d\'accès trouvé.';
            } catch (e) { document.getElementById('wifiList').innerHTML = 'Erreur lors du balayage.'; }
        }

        function selectWifi(ssid) {
            document.getElementById('wifi_ssid').value = ssid;
            closeModal();
            document.getElementById('wifi_pass').focus();
        }

        function closeModal() { document.getElementById('wifiModal').style.display = 'none'; }

        async function saveConfig(e) {
            e.preventDefault();
            const payload = {
                wifi_ssid: document.getElementById('wifi_ssid').value,
                wifi_pass: document.getElementById('wifi_pass').value,
                ap_password: document.getElementById('ap_password').value,
                mqtt_server: document.getElementById('mqtt_server').value,
                max_current_amps: parseInt(document.getElementById('max_current_amps').value),
                overcurrent_threshold: parseFloat(document.getElementById('overcurrent_threshold').value),
                lora_enabled: document.getElementById('lora_enabled').checked,
                debug_mode: document.getElementById('debug_mode').checked
            };

            try {
                const res = await fetch('/api/save-config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(payload)
                });
                const ans = await res.json();
                alert(ans.msg);
            } catch (e) { alert('Erreur d\'envoi de la configuration.'); }
        }

        async function rebootDevice() {
            if (confirm('Êtes-vous sûr de vouloir forcer le redémarrage de l\'ESP32-S3 ?')) {
                try {
                    const res = await fetch('/api/reboot', { method: 'POST' });
                    const ans = await res.json();
                    alert(ans.msg);
                } catch (e) { alert('Ordre de reboot envoyé.'); }
            }
        }

        window.addEventListener('DOMContentLoaded', () => {
            updateStatus();
            setInterval(updateStatus, 5000);
        });
    </script>
</body>
</html>
)rawliteral";

    return html;
}