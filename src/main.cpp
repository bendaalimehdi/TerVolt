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
#include "SAFETY/rcm_manager.h"
#include "NETWORK/ota_manager.h"
#include "INTERACTION/screen_manager.h"
#include "SAFETY/watchdog.h"


WiFiClient espClient;
Logger logger;
ConfigManager config;
WatchdogManager watchdog(logger, config);


WifiManager wifi(logger, config);


ChargingManager charger(logger, config);
EnergyManager energy(logger, config);
TemperatureManager tempManager(logger, config);
OtaManager ota(logger, config);

ServerManager server(logger, config, energy, charger, ota, tempManager);

WebPortal webPortal(logger, config, charger, wifi, energy, tempManager);

LedManager ledStrip(logger, config);
RfidManager rfid(logger, config);
RcmManager rcm(logger, config);
ScreenManager screen(logger, config, charger, energy, tempManager);
// Prototypes des fonctions de tâches
void TaskNetwork(void * pvParameters);
void TaskCharging(void * pvParameters);

TaskHandle_t TaskChargingHandle = NULL;
TaskHandle_t TaskNetworkHandle = NULL;

void setup() {
    Serial.begin(115200);
    delay(500); // Petit délai de courtoisie pour stabiliser la tension et le moniteur série

    
    // Initialisation du système de log (si applicable dans ton architecture)
    logger.begin(); 
    esp_reset_reason_t reason = esp_reset_reason();
    
    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
        Serial.println("🚨 [DIAGNOSTIC] ATTENTION : Le dernier redémarrage était un CRASH WATCHDOG !");
    } else if (reason == ESP_RST_POWERON) {
        Serial.println("⚡ [DIAGNOSTIC] Démarrage normal (Mise sous tension).");
    } else {
        Serial.printf("[DIAGNOSTIC] Autre cause de reboot détectée : %d\n", reason);
    }

    // 1. Initialisation de la configuration 
    if (!config.begin()) {
        logger.error("Echec config - Verifier LittleFS");
        return;
    }

    // Initialisation des broches matérielles du RCM (GPIO 38 et 39)
    rcm.begin();

    // EXÉCUTION ET VÉRIFICATION DE L'AUTO-TEST MATÉRIEL
    // Si l'auto-test échoue (renvoie false), on verrouille la borne immédiatement
    if (!rcm.triggerSelfTest()) { 
        if (config.data.debugMode) {
            logger.warn("[DEV MODE] L'auto-test du RCM a échoué, mais le blocage est ignoré.");
        } else {
            logger.critical("CRITICAL : L'auto-test du RCM a échoué ! Système bloqué pour sécurité.");
            // Boucle de sécurité infinie en mode production
            while(true) { 
                vTaskDelay(1000 / portTICK_PERIOD_MS); 
            }
        }
    }

    // 2. Initialisation des autres périphériques (si l'auto-test est validé)
    //ledStrip.begin();
    //rfid.begin();
    charger.begin();
    energy.begin();
    screen.begin();
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
    watchdog.registerCurrentTask();

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
        watchdog.reset();
        wifi.maintain();
        ota.handle();
        server.maintain();
        screen.update();

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
    watchdog.registerCurrentTask();

    for(;;) {
        watchdog.reset();
        // Lecture RFID
        //if (rfid.isCardPresent()) {
        //    String uid = rfid.readUID();
        //    logger.info("Badge détecté : " + uid);
            // Logique d'autorisation...
        //}
        tempManager.update();
        
        // 🛡️ 2. SÉCURITÉ DIFFÉRENTIELLE RCM (Nouveau)
        if (rcm.isFaultTriggered()) {
            if (!config.data.debugMode) {
                logger.critical("ARRÊT MATÉRIEL : Fuite de courant détectée par le RCM !");
                charger.forceEmergencyStop();
            } else {
                static unsigned long lastRcmLog = 0;
                if (millis() - lastRcmLog > 10000) {
                    logger.warn("[DEV MODE] Fuite RCM détectée mais ignorée.");
                    lastRcmLog = millis();
                }
            }
        }

        // 🛡️ 3. SÉCURITÉ DE SURINTENSITÉ TRIPHASÉE (Nouveau)
        if (energy.getCurrentA() > config.data.maxAmps + 3 || 
            energy.getCurrentB() > config.data.maxAmps + 3 || 
            energy.getCurrentC() > config.data.maxAmps + 3) {
            logger.critical("ARRÊT SÉCURITÉ : Dépassement de l'ampérage maximal autorisé !");
            charger.forceEmergencyStop();
        }
        
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