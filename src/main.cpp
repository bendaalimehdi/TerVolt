#include <Arduino.h>
#include "LOG/log.h"
#include "CONFIG/config_manager.h"
#include "NETWORK/wifi_manager.h"
#include "NETWORK/server_manager.h"
#include "CHARGING_MANAGER/charging_manager.h"
#include "NETWORK/web_portal.h"

WiFiClient espClient; // Objet de transport
Logger logger;
ConfigManager config;
WifiManager wifi(logger);
ServerManager server(logger, config);
ChargingManager charger(logger, config);
WebPortal webPortal(logger, config, charger, wifi);

void setup() {
    logger.begin();
    
    if (config.begin()) {
        wifi.connect(config.data.ssid, config.data.password);
        server.begin(espClient);
    }
    charger.begin();
}

bool isPortalStarted = false;
void loop() {
    wifi.maintain();
    server.maintain();
    

    if (wifi.isConnected()) {
        server.maintain();
        if (wifi.isConnected() && !isPortalStarted) {
            webPortal.begin();
            isPortalStarted = true; // Empêche de relancer au prochain tour de loop
        }
    
        // Si le WiFi coupe, on réinitialise le flag pour pouvoir relancer plus tard
        if (!wifi.isConnected()) {
            isPortalStarted = false;
        }
        server.publishStatus("{\"status\":\"online\", \"signal\":" + String(wifi.getSignalStrength()) + "}");

        
        // Exemple : Envoyer un message de "vie" toutes les 30 secondes
        static unsigned long lastHeartbeat = 0;
        if (millis() - lastHeartbeat > 30000) {
            lastHeartbeat = millis();
            server.publishStatus("{\"heartbeat\":\"alive\"}");
        }
    }
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 200) {
        lastCheck = millis();
        charger.update();
        
    }
}