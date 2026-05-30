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
#include "SAFETY/overcurrent_manager.h"      // ✅ NOUVEAU
#include "NETWORK/ota_manager.h"
#include "NETWORK/ntp_manager.h"              // ✅ NOUVEAU
#include "INTERACTION/screen_manager.h"
#include "SAFETY/watchdog.h"
#include "SAFETY/diagnostics_manager.h"  // ✅ NOUVEAU


WiFiClient espClient;
Logger logger;
ConfigManager config;
WatchdogManager watchdog(logger, config);

WifiManager wifi(logger, config);

ChargingManager charger(logger, config);
EnergyManager energy(logger, config);
TemperatureManager tempManager(logger, config);
OtaManager ota(logger, config);
NtpManager ntp(logger, config);                                          // ✅ NOUVEAU
OvercurrentManager overcurrent(logger, config);                          // ✅ NOUVEAU
DiagnosticsManager diagnostics(logger, config);                          // ✅ NOUVEAU

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
    delay(500);

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

    // 2. Initialisation du gestionnaire de diagnostics (avant tout le reste)
    diagnostics.begin();                                                  // ✅ NOUVEAU

    // 3. Initialisation des broches matérielles du RCM (GPIO 38 et 39)
    rcm.begin();

    // 4. Initialisation de la protection surintensité
    overcurrent.begin();                                                  // ✅ NOUVEAU

    // EXÉCUTION ET VÉRIFICATION DE L'AUTO-TEST MATÉRIEL
    if (!rcm.triggerSelfTest()) {
        if (config.data.debugMode) {
            logger.warn("[DEV MODE] L'auto-test du RCM a échoué, mais le blocage est ignoré.");
        } else {
            logger.critical("CRITICAL : L'auto-test du RCM a échoué ! Système bloqué pour sécurité.");
            diagnostics.logFault("RCM", "Auto-test RCM échoué au démarrage", "BOOT");  // ✅ NOUVEAU
            while(true) {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }
    }

    // 5. Initialisation des autres périphériques
    //ledStrip.begin();
    //rfid.begin();
    charger.begin();
    energy.begin();
    //screen.begin();

    // 6. Création de la Tâche CHARGE sur le Cœur 1
    xTaskCreatePinnedToCore(
        TaskCharging,
        "TaskCharging",
        10000,
        NULL,
        2,
        &TaskChargingHandle,
        1
    );

    // 7. Création de la Tâche NETWORK sur le Cœur 0
    xTaskCreatePinnedToCore(
        TaskNetwork,
        "TaskNetwork",
        10000,
        NULL,
        1,
        &TaskNetworkHandle,
        0
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
    ntp.begin();          // ✅ NOUVEAU : démarrage après wifi.begin()
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
        ntp.maintain();   // ✅ NOUVEAU : synchronisation NTP périodique
        ota.handle();
        server.maintain();
        //screen.update();

        if (server.isConnected() && (millis() - lastMsg > 10000)) {
            lastMsg = millis();
            server.publishFullStatus();
            logger.info("MQTT : Statut périodique envoyé au serveur.");
        }

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

        // 🛡️ 1. SÉCURITÉ THERMIQUE
        tempManager.update();
        if (tempManager.isOverheating()) {
            logger.critical("ARRÊT SÉCURITÉ : Surchauffe détectée !");
            diagnostics.logFault("OVERHEATING", "Surchauffe critique détectée", ntp.getFormattedTime()); // ✅ NOUVEAU
            charger.forceEmergencyStop();
        }

        // 🛡️ 2. SÉCURITÉ DIFFÉRENTIELLE RCM
        if (rcm.isFaultTriggered()) {
            if (!config.data.debugMode) {
                logger.critical("ARRÊT MATÉRIEL : Fuite de courant détectée par le RCM !");
                diagnostics.logFault("RCM", "Fuite de courant détectée", ntp.getFormattedTime()); // ✅ NOUVEAU
                charger.forceEmergencyStop();
            } else {
                static unsigned long lastRcmLog = 0;
                if (millis() - lastRcmLog > 10000) {
                    logger.warn("[DEV MODE] Fuite RCM détectée mais ignorée.");
                    lastRcmLog = millis();
                }
            }
        }

        // 🛡️ 3. SÉCURITÉ DE SURINTENSITÉ TRIPHASÉE (via OvercurrentManager)  // ✅ NOUVEAU
        if (overcurrent.check(energy.getCurrentA(), energy.getCurrentB(), energy.getCurrentC())) {
            logger.critical("ARRÊT SÉCURITÉ : Surintensité détectée par OvercurrentManager !");
            diagnostics.logFault("OVERCURRENT",
                "L1:" + String(energy.getCurrentA()) + "A "
                "L2:" + String(energy.getCurrentB()) + "A "
                "L3:" + String(energy.getCurrentC()) + "A",
                ntp.getFormattedTime());
            charger.forceEmergencyStop();
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

        //ledStrip.update();
        if (charger.isCharging()) {
            //ledStrip.setStatusCharging();
        } else {
            //ledStrip.setStatusAvailable();
        }

        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

void loop() {
    vTaskDelete(NULL);
}