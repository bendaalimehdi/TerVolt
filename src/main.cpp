#include <Arduino.h>
#include "LOG/log.h"
#include "CONFIG/config_manager.h"
#include "NETWORK/wifi_manager.h"
#include "NETWORK/server_manager.h"
#include "CHARGING_MANAGER/charging_manager.h"
#include "NETWORK/web_portal.h"
#include "INTERACTION/led_manager.h"
#include "INTERACTION/rfid.h"
#include "ENERGY/energy_manager.h"
#include "SAFETY/temperature_manager.h"
#include "NETWORK/ota_manager.h"

WiFiClient espClient;
Logger logger;
ConfigManager config;

WifiManager wifi(logger, config);


ChargingManager charger(logger, config);
EnergyManager energy(logger, config);
TemperatureManager tempManager(logger, config);
OtaManager ota(logger, config);

ServerManager server(logger, config, energy, charger, ota, tempManager);

WebPortal webPortal(logger, config, charger, wifi, energy, tempManager);

LedManager ledStrip(logger, config);
RfidManager rfid(logger, config);

// Prototypes des fonctions de tâches
void TaskNetwork(void * pvParameters);
void TaskCharging(void * pvParameters);

TaskHandle_t TaskChargingHandle = NULL;
TaskHandle_t TaskNetworkHandle = NULL;

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
    server.initStorage();
    unsigned long lastMsg = 0;
    logger.info("Task Network démarrée sur Coeur 0");
    pinMode(config.data.pins.btn_config, INPUT_PULLUP);
    
    wifi.begin();
    server.begin(espClient);
    ota.begin();
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
        ota.handle();
        server.maintain();

        if (server.isConnected() && (millis() - lastMsg > 10000)) { 
            lastMsg = millis();
            server.publishFullStatus();
            logger.info("MQTT : Statut périodique envoyé au serveur.");
        }
        
        // vTaskDelay est crucial pour laisser le système gérer le WiFi
        vTaskDelay(10 / portTICK_PERIOD_MS); 
    }
}

// --- COEUR 1 : GESTION DE LA CHARGE & SÉCURITÉ ---
void TaskCharging(void * pvParameters) {
    logger.info("Task Charging démarrée sur Coeur 1");
    tempManager.begin();

    for(;;) {
        // Lecture RFID
        //if (rfid.isCardPresent()) {
        //    String uid = rfid.readUID();
        //    logger.info("Badge détecté : " + uid);
            // Logique d'autorisation...
        //}
        tempManager.update();
        
        // 2. Arrêt immédiat de la borne si anomalie thermique détectée
        if (tempManager.isOverheating()) {
            charger.forceEmergencyStop(); // Une méthode simple pour ouvrir les relais
        }

        energy.update();
        ChargingState currentState = charger.getState();

        // 1. Détection Début Session
        if (currentState == ChargingState::STATE_C && !energy.session.isActive()) {
            energy.session.start(config.data.deviceId);
            logger.success("[OK] Session démarrée");
        }

        // 2. Détection Fin Session
        else if (currentState != ChargingState::STATE_C && energy.session.isActive()) {
            energy.session.stop();
            logger.success("[OK] Session terminée");
            server.saveSessionLocally(energy.session);
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