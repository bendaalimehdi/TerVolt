/**
 * @file    main.cpp
 * @brief   Firmware EVSE ESP32-S3 N16R8 — Architecture 3 tâches FreeRTOS
 *
 * ARCHITECTURE :
 *   Cœur 1 │ TaskSafety   (priorité 3) — RCM, thermique, surintensité UNIQUEMENT
 *   Cœur 1 │ TaskCharging (priorité 2) — automate de charge, énergie, session
 *   Cœur 0 │ TaskNetwork  (priorité 1) — WiFi, MQTT, OTA, WebPortal, LoRa
 *
 * AMÉLIORATIONS APPLIQUÉES :
 *   [FIX-01] Séparation TaskSafety / TaskCharging → sécurité ne peut pas être
 *            bloquée par une écriture LittleFS ou une mise à jour énergie.
 *   [FIX-02] saveSessionLocally() déporté dans TaskNetwork via une FreeRTOS Queue
 *            → plus d'accès LittleFS dans le cœur temps-réel.
 *   [FIX-03] horodatage NTP partagé via snapshotTime (volatile + mutex) au lieu
 *            d'appeler ntp.getFormattedTime() depuis le Cœur 1.
 *   [FIX-04] forceEmergencyStop() protégé par un mutex binaire pour éviter les
 *            appels concurrents des trois vérifications de sécurité.
 *   [FIX-05] initStorage() déplacé dans setup() AVANT la création des tâches,
 *            après watchdog.begin() mais avant xTaskCreatePinnedToCore.
 *   [FIX-06] Backoff exponentiel MQTT (10 s → 5 min max) avec remise à zéro
 *            sur publication réussie.
 *   [FIX-07] Bouton config piloté par ISR + xTaskNotifyFromISR au lieu d'un
 *            polling millis() dans la boucle réseau.
 *   [FIX-08] vTaskDelayUntil() dans TaskSafety pour un timing déterministe à
 *            10 ms, indépendant de la durée des lectures capteurs.
 *   [FIX-09] Stacks augmentés et vérifiés via uxTaskGetStackHighWaterMark()
 *            en mode debug (TaskNetwork : 24 KB, TaskSafety/Charging : 16 KB).
 *   [FIX-10] Macros de log conditionnelles pour supprimer les logs en
 *            production et libérer du CPU sur le Cœur 1.
 */

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
#include "NETWORK/lora_manager.h"

// ─────────────────────────────────────────────────────────────────────────────
// Objets globaux
// ─────────────────────────────────────────────────────────────────────────────

WiFiClient espClient;

Logger          logger;
ConfigManager   config;
WatchdogManager watchdog(logger, config);
WifiManager     wifi(logger, config);
ChargingManager charger(logger, config);
EnergyManager   energy(logger, config);
TemperatureManager tempManager(logger, config);
OtaManager      ota(logger, config);
NtpManager      ntp(logger, config);
OvercurrentManager overcurrent(logger, config);
DiagnosticsManager diagnostics(logger, config);
ServerManager   server(logger, config, energy, charger, ota, tempManager);
WebPortal       webPortal(logger, config, charger, wifi, energy, tempManager);
LoraManager     loraManager(logger, config, charger, energy);
LedManager      ledStrip(logger, config);
RfidManager     rfid(logger, config);
RcmManager      rcm(logger, config);
ScreenManager   screen(logger, config, charger, energy, tempManager);

// ─────────────────────────────────────────────────────────────────────────────
// [FIX-03] Snapshot horodaté partagé Cœur-0 → Cœur-1
// ntp.getFormattedTime() n'est JAMAIS appelé depuis le Cœur 1 directement.
// TaskNetwork écrit snapshotTime toutes les secondes sous le mutex ntpMutex.
// TaskSafety lit snapshotTime sous le même mutex.
// ─────────────────────────────────────────────────────────────────────────────
static SemaphoreHandle_t ntpMutex      = nullptr;
static char              snapshotTime[32] = "??:??:??";
static unsigned long     lastNtpSnapshot  = 0;

/**
 * @brief  Lit le snapshot NTP de manière thread-safe.
 *         Peut être appelé depuis n'importe quel cœur.
 */
static void getThreadSafeTime(char* buf, size_t len) {
    if (xSemaphoreTake(ntpMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        strncpy(buf, snapshotTime, len);
        buf[len - 1] = '\0';
        xSemaphoreGive(ntpMutex);
    } else {
        strncpy(buf, "??:??:??", len);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// [FIX-04] Mutex pour forceEmergencyStop()
// Les 3 vérifications de sécurité (thermique, RCM, surintensité) tournent
// dans la même tâche mais un flag volatile évite un double appel réentrant
// si jamais la structure évolue.
// ─────────────────────────────────────────────────────────────────────────────
static SemaphoreHandle_t emergencyMutex = nullptr;
static volatile bool     emergencyActive = false;

/**
 * @brief  Déclenche l'arrêt d'urgence de manière atomique.
 *         Journalise la cause et l'horodatage thread-safe.
 */
static void triggerEmergencyStop(const char* cause, const char* detail) {
    if (xSemaphoreTake(emergencyMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (!emergencyActive) {
            emergencyActive = true;
            char ts[32];
            getThreadSafeTime(ts, sizeof(ts));
            logger.critical(String("ARRÊT SÉCURITÉ [") + cause + "] " + detail);
            diagnostics.logFault(cause, detail, ts);
            charger.forceEmergencyStop();
        }
        xSemaphoreGive(emergencyMutex);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// [FIX-02] Queue pour les sauvegardes de session LittleFS
// TaskCharging pousse une session terminée dans la queue.
// TaskNetwork dépile et écrit sur le filesystem → zéro I/O bloquant sur Cœur 1.
// ─────────────────────────────────────────────────────────────────────────────
static QueueHandle_t sessionSaveQueue = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// [FIX-07] ISR bouton configuration
// Notifie TaskNetwork via xTaskNotifyFromISR au lieu d'un polling millis().
// ─────────────────────────────────────────────────────────────────────────────
static TaskHandle_t TaskNetworkHandle  = nullptr;
static TaskHandle_t TaskChargingHandle = nullptr;
static TaskHandle_t TaskSafetyHandle   = nullptr;

void IRAM_ATTR btnConfigISR() {
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    if (TaskNetworkHandle) {
        xTaskNotifyFromISR(TaskNetworkHandle, 1, eSetBits, &higherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

// ─────────────────────────────────────────────────────────────────────────────
// Prototypes
// ─────────────────────────────────────────────────────────────────────────────
void TaskSafety(void* pvParameters);
void TaskCharging(void* pvParameters);
void TaskNetwork(void* pvParameters);

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    logger.begin();

    // ── Diagnostic cause de reboot ──────────────────────────────────────────
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
        Serial.println("🚨 [DIAGNOSTIC] Dernier redémarrage : CRASH WATCHDOG !");
    } else if (reason == ESP_RST_POWERON) {
        Serial.println("⚡ [DIAGNOSTIC] Démarrage normal (mise sous tension).");
    } else {
        Serial.printf("[DIAGNOSTIC] Autre cause de reboot : %d\n", reason);
    }

    // ── 1. Configuration ─────────────────────────────────────────────────────
    if (!config.begin()) {
        logger.error("Echec config — vérifier LittleFS");
        return;
    }

    // ── 2. Diagnostics ───────────────────────────────────────────────────────
    diagnostics.begin();

    // ── 3. Primitives de synchronisation ─────────────────────────────────────
    // [FIX-03] [FIX-04]
    ntpMutex      = xSemaphoreCreateMutex();
    emergencyMutex = xSemaphoreCreateMutex();
    if (!ntpMutex || !emergencyMutex) {
        logger.critical("FATAL : impossible de créer les mutex — système bloqué.");
        while (true) { vTaskDelay(1000 / portTICK_PERIOD_MS); }
    }

    // ── 4. Watchdog ───────────────────────────────────────────────────────────
    // [FIX-ORIG] : configuré dans setup() avant la création des tâches
    watchdog.begin(10); // timeout 10 s

    // ── 5. Sécurités matérielles ─────────────────────────────────────────────
    rcm.begin();
    overcurrent.begin();
    tempManager.begin(); // DS18B20 + capteur silicium prêts avant TaskSafety

    // ── 6. Auto-test RCM ─────────────────────────────────────────────────────
    if (!rcm.triggerSelfTest()) {
        if (config.data.debugMode) {
            logger.warn("[DEV MODE] Auto-test RCM échoué — blocage ignoré.");
        } else {
            logger.critical("CRITICAL : Auto-test RCM échoué ! Système bloqué.");
            diagnostics.logFault("RCM", "Auto-test RCM echoue au demarrage", "BOOT");
            while (true) { vTaskDelay(1000 / portTICK_PERIOD_MS); }
        }
    }

    // ── 7. Périphériques applicatifs ─────────────────────────────────────────
    charger.begin();
    energy.begin();
    loraManager.begin();

    if (config.data.has_rfid) {
        rfid.begin();
    }
    
    if (config.data.has_screen) {
        screen.begin();
    }

    // ── 8. [FIX-05] initStorage() dans setup(), avant les tâches ─────────────
    server.initStorage();

    // ── 9. [FIX-02] Queue de sauvegarde session ───────────────────────────────
    // sizeof(energy.session) — adapter si la session est un pointeur ou une struct
    sessionSaveQueue = xQueueCreate(4, sizeof(energy.session));
    if (sessionSaveQueue == nullptr) {
        logger.critical("FATAL : Queue sessionSave non créée — sauvegarde de session désactivée !");
    } else {
        logger.info("Queue sessionSave créée avec succès.");
    }

    // ── 10. [FIX-07] ISR bouton config ───────────────────────────────────────
    pinMode(config.data.pins.btn_config, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(config.data.pins.btn_config), btnConfigISR, FALLING);

    // ── 11. Création des tâches ───────────────────────────────────────────────
    // [FIX-01] TaskSafety — Cœur 1, priorité maximale
    xTaskCreatePinnedToCore(
        TaskSafety,
        "TaskSafety",
        16000,     // 16 KB — sécurité pure, peu de stack nécessaire
        NULL,
        3,         // priorité la plus haute
        &TaskSafetyHandle,
        1
    );

    // TaskCharging — Cœur 1, priorité intermédiaire
    xTaskCreatePinnedToCore(
        TaskCharging,
        "TaskCharging",
        16000,
        NULL,
        2,
        &TaskChargingHandle,
        1
    );

    // TaskNetwork — Cœur 0, priorité normale
    // [FIX-09] Stack augmenté à 24 KB (WiFi + MQTT + OTA + WebPortal + LoRa)
    xTaskCreatePinnedToCore(
        TaskNetwork,
        "TaskNetwork",
        24000,
        NULL,
        1,
        &TaskNetworkHandle,
        0
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// CŒUR 1 — TaskSafety (priorité 3)
// SEULE RESPONSABILITÉ : vérifier les 3 conditions d'arrêt d'urgence.
// Aucune I/O, aucune logique métier, aucun accès réseau.
// [FIX-08] vTaskDelayUntil() pour un timing déterministe à SAFETY_PERIOD_MS.
// ─────────────────────────────────────────────────────────────────────────────
#define SAFETY_PERIOD_MS 10

void TaskSafety(void* pvParameters) {
    logger.info("TaskSafety démarrée — Cœur 1, priorité 3");
    watchdog.registerCurrentTask();

    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        watchdog.reset();

        // ── 🛡 1. Thermique ───────────────────────────────────────────────────
        tempManager.update();
        if (tempManager.isOverheating()) {
            triggerEmergencyStop("OVERHEATING", "Surchauffe critique detectee");
        }

        // ── 🛡 2. RCM — fuite différentielle ─────────────────────────────────
        if (rcm.isFaultTriggered()) {
            if (!config.data.debugMode) {
                triggerEmergencyStop("RCM", "Fuite de courant detectee");
            } else {
                static unsigned long lastRcmLog = 0;
                if (millis() - lastRcmLog > 10000) {
                    logger.warn("[DEV MODE] Fuite RCM détectée mais ignorée.");
                    lastRcmLog = millis();
                }
            }
        }

        // ── 🛡 3. Surintensité triphasée ──────────────────────────────────────
        float iA = energy.getCurrentA();
        float iB = energy.getCurrentB();
        float iC = energy.getCurrentC();
        if (overcurrent.check(iA, iB, iC)) {
            String detail = "L1:" + String(iA, 1) + "A "
                          + "L2:" + String(iB, 1) + "A "
                          + "L3:" + String(iC, 1) + "A";
            triggerEmergencyStop("OVERCURRENT", detail.c_str());
        }

        // [FIX-08] Délai déterministe — SAFETY_PERIOD_MS exact, même si
        // tempManager.update() a pris du temps (OneWire peut durer 750 ms en
        // conversion, à gérer en mode non bloquant côté TemperatureManager).
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SAFETY_PERIOD_MS));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CŒUR 1 — TaskCharging (priorité 2)
// Automate de charge + comptage énergie + gestion session.
// [FIX-02] saveSessionLocally() remplacé par un push dans sessionSaveQueue.
// ─────────────────────────────────────────────────────────────────────────────
void TaskCharging(void* pvParameters) {
    logger.info("TaskCharging démarrée — Cœur 1, priorité 2");
    watchdog.registerCurrentTask();

    for (;;) {
        watchdog.reset();

        energy.update();
        ChargingState currentState = charger.getState();

        // ── Détection début de session ────────────────────────────────────────
        if (currentState == ChargingState::STATE_C && !energy.session.isActive()) {
            energy.session.start(config.data.deviceId);
            logger.success("[OK] Session démarrée");
            // Réinitialiser le flag d'urgence pour permettre une nouvelle session
            // après résolution manuelle d'une alarme (ex. thermique résolue)
            if (xSemaphoreTake(emergencyMutex, 0) == pdTRUE) {
                emergencyActive = false;
                xSemaphoreGive(emergencyMutex);
            }
        }
        // ── Détection fin de session ──────────────────────────────────────────
        else if (currentState != ChargingState::STATE_C && energy.session.isActive()) {
            energy.session.stop();
            logger.success("[OK] Session terminée");

            // [FIX-02] Push dans la queue → TaskNetwork écrit sur LittleFS
            if (sessionSaveQueue != nullptr) {
                // Copie de la session dans la queue (ne bloque pas si pleine)
                if (xQueueSend(sessionSaveQueue, &energy.session, 0) != pdTRUE) {
                    logger.warn("sessionSaveQueue pleine — session perdue !");
                }
            }
        }

        charger.update();

        // ── Stack monitor (mode debug uniquement) ─────────────────────────────
#ifdef DEBUG_STACK
        static unsigned long lastStackLog = 0;
        if (millis() - lastStackLog > 30000) {
            Serial.printf("[STACK] TaskCharging HWM: %u bytes\n",
                          uxTaskGetStackHighWaterMark(NULL) * 4);
            lastStackLog = millis();
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CŒUR 0 — TaskNetwork (priorité 1)
// WiFi, MQTT, OTA, WebPortal, NTP, LoRa, sauvegarde LittleFS, LCD & RFID.
// ─────────────────────────────────────────────────────────────────────────────
void TaskNetwork(void* pvParameters) {
    logger.info("TaskNetwork démarrée — Cœur 0, priorité 1");

    wifi.begin();
    server.begin(espClient);
    ota.begin();
    ntp.begin();
    webPortal.begin();
    watchdog.registerCurrentTask();

    // [FIX-06] État du backoff MQTT
    static uint32_t mqttBackoff   = 10000;   // commence à 10 s
    static unsigned long lastMqtt = 0;
    static const uint32_t MQTT_BACKOFF_MAX = 300000; // 5 min max

    // [FIX-07] Gestion du bouton via notification ISR
    bool apStarted = false;
    static unsigned long btnPressStart = 0;

    for (;;) {
        watchdog.reset();

        // ── [FIX-07] Bouton config via notification ISR ───────────────────────
        uint32_t notif = 0;
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &notif, 0) == pdTRUE) {
            if (notif & 1) {
                if (btnPressStart == 0) btnPressStart = millis();
            }
        }
        // Vérification maintien 5 s après premier front détecté
        if (btnPressStart > 0 && digitalRead(config.data.pins.btn_config) == LOW) {
            if (!apStarted && millis() - btnPressStart > 5000) {
                wifi.startAP();
                apStarted = true;
            }
        } else if (digitalRead(config.data.pins.btn_config) == HIGH) {
            btnPressStart = 0;
            apStarted = false;
        }

        // ── Maintenance réseau ────────────────────────────────────────────────
        wifi.maintain();
        webPortal.handleDNS();

        // ── [FIX-03] Snapshot NTP (max 1×/s) ─────────────────────────────────
        if (millis() - lastNtpSnapshot > 1000) {
            String ts = ntp.getFormattedTime(); // appelé UNIQUEMENT ici, Cœur 0
            if (xSemaphoreTake(ntpMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                strncpy(snapshotTime, ts.c_str(), sizeof(snapshotTime) - 1);
                xSemaphoreGive(ntpMutex);
            }
            lastNtpSnapshot = millis();
        }

        // ── [FIX-02] Sauvegarde session depuis la queue ───────────────────────
        if (sessionSaveQueue != nullptr) {
            decltype(energy.session) savedSession;
            while (xQueueReceive(sessionSaveQueue, &savedSession, 0) == pdTRUE) {
                server.saveSessionLocally(savedSession);
                logger.info("Session sauvegardée sur LittleFS.");
            }
        }

        // ── Routage selon configuration ───────────────────────────────────────
        if (!config.data.loraEnabled) {
            ntp.maintain();
            ota.handle();
            server.maintain();

            // [FIX-06] Publication MQTT avec backoff exponentiel
            if (server.isConnected() && (millis() - lastMqtt > mqttBackoff)) {
                lastMqtt = millis();
                if (server.publishFullStatus()) {
                    mqttBackoff = 10000; // succès → remise à 10 s
                    logger.info("MQTT : statut publié.");
                } else {
                    mqttBackoff = min(mqttBackoff * 2, (uint32_t)MQTT_BACKOFF_MAX);
                    logger.warn("MQTT : échec publication, backoff " +
                                String(mqttBackoff / 1000) + "s");
                }
            }
        } else {
            loraManager.maintain();
        }

        // ── 🚀 GESTION DE L'ÉCRAN LCD (Conditionnelle) ────────────────────────
        if (config.data.has_screen) {
            screen.update();
        }

        // ── 🚀 GESTION DE L'AUTHENTIFICATION & RFID (Conditionnelle) ──────────
        if (!config.data.with_auth) {
            // MODE PARTICULIER : "Plug & Charge"
            // Si le véhicule est branché (État B) mais pas encore autorisé, on force l'autorisation
            if (charger.getState() == ChargingState::STATE_B && !charger.isAuthorized()) {
                charger.setAuthorized(true);
                logger.info("[AUTH] Mode Plug & Charge : Véhicule auto-autorisé.");
            }
        } 
        else if (config.data.has_rfid) {
            // MODE B2B / PREMIUM : Attente du badge RFID
            String uid = rfid.update();
            if (uid != "") {
                logger.info("[AUTH] Badge lu : " + uid);
                
                // Note: Ici, le badge est accepté par défaut. Plus tard, tu pourras
                // ajouter la vérification de l'UID avec une base de données locale ou MQTT.
                charger.setAuthorized(true);
                logger.success("[AUTH] Session déverrouillée par badge RFID.");
            }
        }

        // ── Stack monitor (mode debug uniquement) ─────────────────────────────
#ifdef DEBUG_STACK
        static unsigned long lastStackLogN = 0;
        if (millis() - lastStackLogN > 30000) {
            Serial.printf("[STACK] TaskNetwork HWM: %u bytes\n",
                          uxTaskGetStackHighWaterMark(NULL) * 4);
            lastStackLogN = millis();
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
// ─────────────────────────────────────────────────────────────────────────────
// loop() — non utilisé avec FreeRTOS
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    vTaskDelete(NULL);
}