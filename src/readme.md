# ⚡ TerVolt - EV Smart Charger Station

Documentation technique des avancées du système de contrôle de la borne de recharge TerVolt.

## 📅 Journal de bord : Développements du jour

Aujourd'hui, nous avons stabilisé le noyau du système (Firmware ESP32-S3), corrigé les erreurs de gestion de configuration, implémenté la logique métier de charge (J1772) et créé une interface de monitoring de production.

### 1. 🏗️ Architecture & Core System
- **Fix Scope des Variables :** Déplacement des instances majeures (`ChargingManager`, `ConfigManager`, etc.) en variables globales dans `main.cpp` pour assurer leur accessibilité dans tout le cycle de vie du programme.
- **Mise à jour ESP32 SDK 3.0 :** Adaptation du code pour la nouvelle API `LEDC` (PWM) et résolution des conflits de bibliothèques asynchrones (`AsyncTCP` patché).

### 2. ⚙️ Gestion de Configuration (ConfigManager)
- **Mapping JSON Dynamique :** Correction de la structure de données pour supporter les pins hardware imbriqués (`data.pins.xxx`).
- **Nouveaux Champs Production :** Ajout du support pour les métadonnées client (`customer_name` et `location`) récupérées directement depuis le système de fichiers LittleFS.
- **Migration ArduinoJson 7 :** Passage au nouveau modèle `JsonDocument` (gestion mémoire automatique) pour plus de stabilité.

### 3. 🔌 Logique de Charge (ChargingManager)
- **Implémentation du Protocole J1772 :**
    - **État A (Repos) :** La borne maintient un signal 12V constant (Duty Cycle 100%).
    - **État B (Détection) :** Passage automatique en mode PWM lorsqu'une voiture est détectée.
    - **État C (Charge) :** Pilotage du relais de puissance (Contrôleur de contacteur).
- **Calcul du Duty Cycle :** Intégration de la formule standard ($Ampères = DutyCycle \times 0.6$) pour limiter le courant selon la configuration.
- **Calculateur de Tension Pilot :** Lecture ADC avec pont diviseur pour interpréter les états du véhicule.

### 4. 🌐 Portail Web de Production (WebPortal)
Création d'une interface de diagnostic ultra-légère et moderne accessible via l'IP de la borne.
- **Design Industriel :** Interface sombre (Dark Mode), responsive, avec indicateurs visuels (Badges de couleur).
- **Monitor Temps Réel :**
    - Affichage du signal WiFi (RSSI) avec alertes de qualité.
    - État physique des pins (Relais, PWM, ADC).
    - Visualisation du signal PWM (Barre de progression dynamique).
    - Affichage des informations client et localisation.

### 5. 💳 Contrôle d'Accès (RFID)
- **Module MFRC522 :** Initialisation et lecture des badges via le bus SPI.
- **Tampon de Données :** Système de transfert de l'UID détecté vers le portail Web pour affichage immédiat du dernier badge scanné.

---

## 🛠️ Configuration Technique (Hardware Mapping)

D'après le fichier `config.json` actuel :
- **PWM (Control Pilot) :** Pin 13
- **ADC (Lecture Pilot) :** Pin 14
- **Relais (Contacteur) :** Pin 4
- **RFID SS/RST :** Pins 5 / 22

## 🚀 Comment l'utiliser en Production ?

1. **Accès au Dashboard :**
   Connectez-vous au même réseau WiFi que la borne et entrez l'adresse IP affichée dans le log série (ex: `http://192.168.1.50`).
   
2. **Diagnostic Rapide :**
   - Vérifiez que **Signal PWM** est à `100%` au repos.
   - Passez un badge RFID : l'UID doit apparaître instantanément dans la section **Contrôle d'Accès**.
   - Branchez un simulateur de véhicule : le statut doit passer en **Véhicule Détecté** et le Duty Cycle doit s'ajuster à la limite d'ampérage (ex: 32A).

## ⚠️ Notes de Sécurité
- Le relais de puissance ne s'enclenche **QUE SI** le véhicule est en État C (Demande de charge) **ET** que l'autorisation est valide.
- En cas de déconnexion brutale du véhicule, le relais tombe instantanément (Protection contre les arcs électriques).

---
*Développé pour TerVolt V0 - Phase de Production.*