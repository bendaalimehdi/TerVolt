#include <Arduino.h>
#include "LOG/log.h"
#include "CONFIG/config_manager.h"
#include "NETWORK/wifi_manager.h"
#include "NETWORK/server_manager.h"
#include "CHARGING_MANAGER/charging_manager.h"
#include "NETWORK/web_portal.h"
#include "INTERACTION/led_manager.h"
#include "INTERACTION/rfid.h"

WiFiClient espClient; // Objet de transport
Logger logger;
ConfigManager config;
WifiManager wifi(logger, config);
ServerManager server(logger, config);
ChargingManager charger(logger, config);
EnergyManager energy(logger, config);
WebPortal webPortal(logger, config, charger, wifi, energy);
LedManager ledStrip(logger, config);
RfidManager rfid(logger, config);       
TaskHandle_t TaskNetworkHandle;
TaskHandle_t TaskChargingHandle;

// Prototypes des fonctions de tâches
void TaskNetwork(void * pvParameters);
void TaskCharging(void * pvParameters);

void setup() {
    Serial.begin(115200);
    
    // 1. Initialisation de la configuration 
    if (!config.begin()) {
        logger.error("Echec config - Verifier LittleFS");
        return;
    }

    // 2. Initialisation des périphériques
    //ledStrip.begin();
    //rfid.begin();
    charger.begin();
    energy.begin();

    // 3. Création de la Tâche CHARGE sur le Cœur 1 (Application Core)
    // C'est ici que la sécurité et la norme J1772 sont gérées
    xTaskCreatePinnedToCore(
        TaskCharging,   /* Fonction */
        "TaskCharging", /* Nom */
        10000,          /* Taille pile */
        NULL,           /* Param */
        2,              /* Priorité haute */
        &TaskChargingHandle, 
        1               /* COEUR 1 */
    );

    // 4. Création de la Tâche NETWORK sur le Cœur 0 (Protocol Core)
    // Gestion du WiFi, MQTT et Portail Web
    xTaskCreatePinnedToCore(
        TaskNetwork,    /* Fonction */
        "TaskNetwork",  /* Nom */
        10000,          /* Taille pile */
        NULL,           /* Param */
        1,              /* Priorité normale */
        &TaskNetworkHandle, 
        0               /* COEUR 0 */
    );
}

// --- COEUR 0 : GESTION RÉSEAU & WIFI ---
void TaskNetwork(void * pvParameters) {
    logger.info("Task Network démarrée sur Coeur 0");
    pinMode(config.data.pins.btn_config, INPUT_PULLUP);
    
    wifi.begin();
    server.begin(espClient);
    webPortal.begin();

    unsigned long pressStart = 0;
    bool apStarted = false;  

    for(;;) {
        // Gestion du bouton WiFi AP (5 secondes sur GPIO 47)
        if (digitalRead(config.data.pins.btn_config) == LOW) {
        if (pressStart == 0) pressStart = millis();
        if (!apStarted && millis() - pressStart > 5000) {
            wifi.startAP();
            apStarted = true;
        }
    } else {
        pressStart = 0;
        apStarted = false;
    }

        wifi.maintain();
        server.maintain();
        
        // vTaskDelay est crucial pour laisser le système gérer le WiFi
        vTaskDelay(10 / portTICK_PERIOD_MS); 
    }
}

// --- COEUR 1 : GESTION DE LA CHARGE & SÉCURITÉ ---
void TaskCharging(void * pvParameters) {
    logger.info("Task Charging démarrée sur Coeur 1");

    for(;;) {
        // Lecture RFID
        //if (rfid.isCardPresent()) {
        //    String uid = rfid.readUID();
        //    logger.info("Badge détecté : " + uid);
            // Logique d'autorisation...
        //}
        energy.update();
        // Logique de session
        if (charger.getState() == ChargingState::STATE_C && !energy.session.isActive()) {
            energy.session.start();
            logger.info("Session de charge démarrée");
        } 
        else if (charger.getState() != ChargingState::STATE_C && energy.session.isActive()) {
            energy.session.stop();
            logger.warn("Session terminée. Total: " + String(energy.session.getSessionEnergyKwh()) + " kWh");
        }
        // Mise à jour de la machine à états de charge (PWM, CP, Relais)
        charger.update(); 

        // Mise à jour des animations LED (Sillage fluide)
        //ledStrip.update();

        // Si on charge, on affiche l'animation verte
        if (charger.isCharging()) {
            //ledStrip.setStatusCharging();
        } else {
            //ledStrip.setStatusAvailable();
        }

        vTaskDelay(5 / portTICK_PERIOD_MS); // Fréquence de rafraîchissement rapide
    }
}

void loop() {
    // La boucle principale reste vide car tout est géré dans les tâches
    vTaskDelete(NULL); 
}