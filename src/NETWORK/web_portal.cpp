#include "web_portal.h"

WebPortal::WebPortal(Logger& logger, ConfigManager& config, ChargingManager& charger, WifiManager& wifi) 
    : _server(80), _logger(logger), _config(config), _charger(charger), _wifi(wifi) {}

void WebPortal::begin() {
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request){
        request->send(200, "text/html", generateDashboard());
    });

    _server.begin();
    _logger.success("Portail Web de production lancé sur le port 80");
}

String WebPortal::generateDashboard() {
    String html = "<!DOCTYPE html><html><head><title>Tervolt | Admin Monitor</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1' charset='utf-8'>";
    
    // --- CSS STYLE ---
    html += "<style>";
    html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #0e1013; color: #e0e0e0; margin: 0; padding: 20px; }";
    html += "h1 { color: #00d1b2; font-weight: 300; border-bottom: 1px solid #333; padding-bottom: 10px; }";
    html += ".container { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }";
    
    // Cards
    html += ".card { background: #1a1d23; border-radius: 12px; padding: 20px; box-shadow: 0 4px 15px rgba(0,0,0,0.3); border: 1px solid #2d323a; }";
    html += ".card h2 { color: #00d1b2; font-size: 1.1rem; margin-top: 0; text-transform: uppercase; letter-spacing: 1px; }";
    
    // Data Rows
    html += ".row { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #262a33; }";
    html += ".row:last-child { border-bottom: none; }";
    html += ".label { color: #9da5b1; }";
    html += ".value { font-weight: 500; font-family: monospace; color: #ffffff; }";
    
    // Badges
    html += ".badge { padding: 4px 10px; border-radius: 20px; font-size: 0.8rem; font-weight: bold; }";
    html += ".bg-green { background: #23d16033; color: #23d160; }";
    html += ".bg-orange { background: #ffdd5733; color: #ffdd57; }";
    html += ".bg-red { background: #ff386033; color: #ff3860; }";
    
    // Button
    html += ".btn { display: block; width: 100%; background: #00d1b2; color: white; border: none; padding: 12px; border-radius: 8px; cursor: pointer; font-size: 1rem; margin-top: 20px; transition: 0.3s; }";
    html += ".btn:hover { background: #00b89c; }";
    html += "</style></head><body>";

    html += "<h1>TERVOLT <small style='font-size:0.5em; color:#666;'>v1.0.0 Production</small></h1>";
    html += "<div class='container'>";

    // --- CARD SYSTEM ---
    html += "<div class='card'><h2>Système</h2>";
    html += "<div class='row'><span class='label'>ID Borne</span><span class='value'>" + _config.data.deviceId + "</span></div>";
    html += "<div class='row'><span class='label'>Client</span><span class='value'>" + _config.data.customer_name + "</span></div>";
    html += "<div class='row'><span class='label'>Localisation</span><span class='value'>" + _config.data.location + "</span></div>";
    html += "<div class='row'><span class='label'>Uptime</span><span class='value'>" + String(millis() / 60000) + " min</span></div></div>";

    // --- CARD NETWORK ---
    int rssi = WiFi.RSSI();
    String rssiClass = (rssi > -60) ? "bg-green" : (rssi > -75 ? "bg-orange" : "bg-red");
    html += "<div class='card'><h2>Connectivité</h2>";
    html += "<div class='row'><span class='label'>Adresse IP</span><span class='value'>" + _wifi.getIP() + "</span></div>";
    html += "<div class='row'><span class='label'>Signal WiFi</span><span class='badge " + rssiClass + "'>" + String(rssi) + " dBm</span></div>";
    html += "<div class='row'><span class='label'>Serveur MQTT</span><span class='value'>" + _config.data.mqttServer + "</span></div></div>";

    // --- CARD HARDWARE ---
    bool relayOn = digitalRead(_config.data.pins.relay);
    html += "<div class='card'><h2>Hardware</h2>";
    html += "<div class='row'><span class='label'>Contacteur</span><span class='badge " + String(relayOn ? "bg-green" : "bg-red") + "'>" + (relayOn ? "ON" : "OFF") + "</span></div>";
    html += "<div class='row'><span class='label'>Pin PWM (CP)</span><span class='value'>" + String(_config.data.pins.cp_pwm) + "</span></div>";
    html += "<div class='row'><span class='label'>Pin ADC (CP)</span><span class='value'>" + String(_config.data.pins.cp_adc) + "</span></div></div>";

    // --- CARD CHARGE LOGIC ---
    float duty = _charger.getDutyCycle();
    ChargingState state = _charger.getState();
    String stateStr = _charger.getStateString();

    // Couleur badge selon l'état
    String stateBadge;
    switch (state) {
        case ChargingState::STATE_A: stateBadge = "bg-orange"; break;
        case ChargingState::STATE_B: stateBadge = "bg-orange"; break;
        case ChargingState::STATE_C: stateBadge = "bg-green";  break;
        case ChargingState::STATE_D: stateBadge = "bg-orange"; break;
        default:                     stateBadge = "bg-red";    break; // E, F
    }

    html += "<div class='card'><h2>Logique de Charge</h2>";
    html += "<div class='row'><span class='label'>État J1772</span><span class='badge " + stateBadge + "'>" + stateStr + "</span></div>";
    html += "<div class='row'><span class='label'>Signal PWM</span><span class='value'>" + String(duty, 1) + " %</span></div>";

    // Barre de visualisation du PWM
    html += "<div style='width:100%; background:#262a33; height:8px; border-radius:4px; margin:10px 0;'>";
    html += "<div style='width:" + String(duty) + "%; background:#00d1b2; height:100%; border-radius:4px; transition:width 0.5s;'></div>";
    html += "</div>";

    html += "<div class='row'><span class='label'>Courant Max</span><span class='value'>" + String(_config.data.maxAmps) + " A</span></div>";
    html += "<div class='row'><span class='label'>Status Charge</span><span class='badge " + String(_charger.isCharging() ? "bg-green" : "bg-orange") + "'>" + (_charger.isCharging() ? "ACTIVE" : "STANDBY") + "</span></div>";
    html += "</div>";



    html += "</div>"; // Fin container
    html += "<button class='btn' onclick='location.reload()'>ACTUALISER LES DONNÉES</button>";
    html += "<p style='text-align:center; color:#555; font-size:0.75rem; margin-top:10px;'>Actualisation auto dans <span id='countdown'>5</span>s</p>";
    html += "<script>";
    html += "var t=5; var el=document.getElementById('countdown');";
    html += "setInterval(function(){ t--; el.textContent=t; if(t<=0) location.reload(); }, 1000);";
    html += "</script>";
    html += "</body></html>";
    return html;
}