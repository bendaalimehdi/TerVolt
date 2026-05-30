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
#include "SAFETY/overcurrent_manager.h"
#include "NETWORK/ota_manager.h"
#include "NETWORK/ntp_manager.h"
#include "INTERACTION/screen_manager.h"
#include "SAFETY/watchdog.h"
#include "SAFETY/diagnostics_manager.h"


WiFiClient espClient;
Logger logger;
ConfigManager config;
WatchdogManager watchdog(logger, config);

WifiManager wifi(logger, config);

ChargingManager charger(logger, config);
EnergyManager energy(logger, config);
TemperatureManager tempManager(logger, config);
OtaManager ota(logger, config);
NtpManager ntp(logger, config);
OvercurrentManager overcurrent(logger, config);
DiagnosticsManager diagnostics(logger, config);

ServerManager server(logger, config, energy, charger, ota, tempManager);
WebPortal webPortal(logger, config, charger, wifi, energy, tempManager);

LedManager ledStrip(logger, config);
RfidManager rfid(logger, config);
RcmManager rcm(logger, config);
ScreenManager screen(logger, config, charger, energy, tempManager);

// Prototypes
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

    // 1. Configuration
    if (!config.begin()) {
        logger.error("Echec config - Verifier LittleFS");
        return;
    }

    // 2. Diagnostics (premier, avant tout le reste)
    diagnostics.begin();

    // ✅ FIX 1 — Watchdog configuré ici, dans setup(), avant la création des tâches.
    // Les tâches s'enregistreront elles-mêmes via registerCurrentTask().
    watchdog.begin(10); // Timeout 10 secondes

    // 3. Sécurités matérielles
    rcm.begin();
    overcurrent.begin();

    // ✅ FIX 3 — tempManager initialisé ici, dans setup(), AVANT la création de TaskCharging.
    // Garantit que les sondes DS18B20 et le capteur silicium sont prêts dès le premier
    // appel à isOverheating() dans la boucle de la tâche.
    tempManager.begin();

    // 4. Auto-test RCM
    if (!rcm.triggerSelfTest()) {
        if (config.data.debugMode) {
            logger.warn("[DEV MODE] L'auto-test du RCM a échoué, mais le blocage est ignoré.");
        } else {
            logger.critical("CRITICAL : L'auto-test du RCM a échoué ! Système bloqué pour sécurité.");
            diagnostics.logFault("RCM", "Auto-test RCM echoue au demarrage", "BOOT");
            while(true) {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }
    }

    // 5. Périphériques applicatifs
    //ledStrip.begin();
    //rfid.begin();
    charger.begin();
    energy.begin();
    //screen.begin();

    // 6. Tâche CHARGE — Cœur 1, priorité haute
    xTaskCreatePinnedToCore(
        TaskCharging,
        "TaskCharging",
        16000,
        NULL,
        2,
        &TaskChargingHandle,
        1
    );

    // 7. Tâche NETWORK — Cœur 0, priorité normale
    xTaskCreatePinnedToCore(
        TaskNetwork,
        "TaskNetwork",
        16000,
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
    ntp.begin();       // Après wifi.begin() — configTzTime() nécessite le stack IP
    webPortal.begin();
    watchdog.registerCurrentTask();

    unsigned long pressStart = 0;
    bool apStarted = false;

    for(;;) {
        // Bouton de forçage AP (maintien 5s sur GPIO btn_config)
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
        ntp.maintain();    // Synchronisation NTP périodique (5s → 60s une fois synced)
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

    // tempManager.begin() est maintenant appelé dans setup() — on enregistre
    // seulement la tâche auprès du watchdog.
    watchdog.registerCurrentTask();

    for(;;) {
        watchdog.reset();

        // 🛡️ 1. SÉCURITÉ THERMIQUE
        tempManager.update();
        if (tempManager.isOverheating()) {
            logger.critical("ARRÊT SÉCURITÉ : Surchauffe détectée !");
            // ✅ FIX 2 — ntp.getFormattedTime() est thread-safe : elle n'accède qu'à
            // l'horloge POSIX système via getLocalTime() (lecture seule, protégée par
            // l'OS). Les membres mutables de NtpManager (_isSynced, _checkInterval…)
            // ne sont écrits que dans maintain(), qui s'exécute uniquement sur le Cœur 0.
            diagnostics.logFault("OVERHEATING", "Surchauffe critique detectee", ntp.getFormattedTime());
            charger.forceEmergencyStop();
        }

        // 🛡️ 2. SÉCURITÉ DIFFÉRENTIELLE RCM
        if (rcm.isFaultTriggered()) {
            if (!config.data.debugMode) {
                logger.critical("ARRÊT MATÉRIEL : Fuite de courant détectée par le RCM !");
                diagnostics.logFault("RCM", "Fuite de courant detectee", ntp.getFormattedTime());
                charger.forceEmergencyStop();
            } else {
                static unsigned long lastRcmLog = 0;
                if (millis() - lastRcmLog > 10000) {
                    logger.warn("[DEV MODE] Fuite RCM détectée mais ignorée.");
                    lastRcmLog = millis();
                }
            }
        }

        // 🛡️ 3. SÉCURITÉ SURINTENSITÉ TRIPHASÉE
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

        // Détection début de session
        if (currentState == ChargingState::STATE_C && !energy.session.isActive()) {
            energy.session.start(config.data.deviceId);
            logger.success("[OK] Session démarrée");
        }
        // Détection fin de session
        else if (currentState != ChargingState::STATE_C && energy.session.isActive()) {
            energy.session.stop();
            logger.success("[OK] Session terminée");
            server.saveSessionLocally(energy.session);
        }

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