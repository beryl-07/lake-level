#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include "SevSeg.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseJson.h>
#include <FirebaseClient.h>

// Segment pins
#define A_PIN  15 // A1
#define B_PIN  2 // A0 
#define C_PIN  4 // A2
#define D_PIN  16 // A3
#define E_PIN  17 // A4
#define F_PIN  5 // A5
#define G_PIN  12 // 3
#define DP_PIN 13 // 2

// Digit control pins (cathodes)
#define DIGIT1_PIN 23 // 13                                                           
#define DIGIT2_PIN 22 // 12
#define DIGIT3_PIN 33 // 8
#define DIGIT4_PIN 18 // 0 

// Ultrasonic Sensor Pins
#define TRIG_PIN 32 // 9
#define ECHO_PIN 35 // 10

// Push button Pin
#define BUTTON_PIN 25 // 7

// LED Pins
#define GREEN_LED_PIN 26 // 6
#define YELLOW_LED_PIN 14 // 4
#define RED_LED_PIN 27 // 5

// BUZZER Pin
#define BUZZER_PIN 19 // 34 // 11 


// Network credentials
const char* ssid = "";
const char* password = "";

// Firebase credentials
const char* web_api_key = "";
const char* database_url = "";
const char* user_email = "";
const char* user_pass = "";

// Authentication
UserAuth user_auth(web_api_key, user_email, user_pass);

// Firebase components
FirebaseApp app;

WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// Set web server port number to 80
WiFiServer server(80);

// Variable to store the HTTP request
String header;

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;


typedef struct {
  float normalLevel;
  float alertLevel;
  float criticalLevel;
} lake_level;

SevSeg sevseg;
lake_level lakeLevels = {-1, -1, -1};

float calibrationDistance = 0;
bool isCalibrated = false;

// timers
unsigned long lastMeasure = 0;
unsigned long lastPrint = 0;
unsigned long lastBeepToggle = 0;

// buzzer state
bool buzzerState = false;
bool criticalActive = false;

float currentDistance = 0;
float currentWaterLevel = 0;
float currentFillPercent = 0;
String currentState = "EN ATTENTE DE CALIBRATION";
unsigned long systemStartMillis = 0;
unsigned long lastMeasureMillis = 0;

void setLakeLevels(float normal, float alert, float critical);
float distanceByUltrasonSensor();
void calibrate();
void handleLevelIndicators(float waterLevel);
void beepNonBlocking();
String getLakeStateLabel(float waterLevel);
void handleWebClient();
// User function
void processData(AsyncResult &aResult);
void sendDashboardPage(WiFiClient &client);
void sendJsonStatus(WiFiClient &client);

void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  byte numDigits = 4;
  byte digitPins[] = { DIGIT1_PIN, DIGIT2_PIN, DIGIT3_PIN, DIGIT4_PIN };
  byte segmentPins[] = { A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, F_PIN, G_PIN, DP_PIN };

  sevseg.begin(COMMON_CATHODE, numDigits, digitPins, segmentPins);
  sevseg.setBrightness(150);


  // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Print local IP address and start web server
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  systemStartMillis = millis();
  server.begin();


  // Configure SSL client
  ssl_client.setInsecure();
  ssl_client.setConnectionTimeout(1000);
  ssl_client.setHandshakeTimeout(5);
  
  // Initialize Firebase
  initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(database_url);
}

void loop() {

  app.loop();

  if (app.ready()) {
    sevseg.refreshDisplay();
    handleWebClient();

    // Button (non-blocking debounce)
    static unsigned long lastButtonTime = 0;
    if (digitalRead(BUTTON_PIN) == LOW && millis() - lastButtonTime > 300) {
      lastButtonTime = millis();
      calibrate();
    }

    if (!isCalibrated) return;

    // Measure distance every 100ms
    if (millis() - lastMeasure > 100) {
      lastMeasure = millis();

      float distance = distanceByUltrasonSensor();
      float waterLevel = calibrationDistance - distance;
      if (waterLevel < 0) waterLevel = 0;

      currentDistance = distance;
      currentWaterLevel = waterLevel;
      lastMeasureMillis = millis();
      currentFillPercent = calibrationDistance > 0 ? (waterLevel / calibrationDistance) * 100.0 : 0;
      if (currentFillPercent < 0) currentFillPercent = 0;
      if (currentFillPercent > 100) currentFillPercent = 100;
      currentState = getLakeStateLabel(waterLevel);

      handleLevelIndicators(waterLevel);

      if (waterLevel < 0)
        waterLevel = 0;
      if (waterLevel > 99.99)
        waterLevel = 99.99;

      // Determine how many decimal places based on water level
      if (waterLevel < 10) {
        sevseg.setNumber((waterLevel*100), 2);  // XX.XX
      } else if (waterLevel < 100) {
        sevseg.setNumber((waterLevel*100), 1);  // XXX.X
      } else {
        sevseg.setNumber((waterLevel*100), 0);  // XXXX
      }

      // Print only twice per second
      if (millis() - lastPrint > 500) {
        lastPrint = millis();
        Serial.print("Water Level: ");
        Serial.println(waterLevel);

        FirebaseJson json;
        json.set("calibrated", isCalibrated);
        json.set("state", currentState);
        json.set("waterLevelCm", currentWaterLevel);
        json.set("distanceCm", currentDistance);
        json.set("fillPercent", currentFillPercent);
        json.set("normalThresholdCm", String(lakeLevels.normalLevel, 2));
        json.set("alertThresholdCm", String(lakeLevels.alertLevel, 2));
        json.set("criticalThresholdCm", String(lakeLevels.criticalLevel, 2));
        json.set("calibrationDistanceCm", String(calibrationDistance, 2));
        json.set("criticalAlarm", criticalActive);
        json.set("uptimeSec", lastMeasureMillis > 0 ? (millis() - systemStartMillis) / 1000 : 0);
        json.set("sinceLastMeasureSec", lastMeasureMillis > 0 ? (millis() - lastMeasureMillis) / 1000 : 0);

  
        String jsonStr;
        json.toString(jsonStr, false);
        
        object_t obj(jsonStr.c_str());
        Database.set(aClient, "/data", obj, processData, "📤 updateStatus");
      
      }
    }

    if (criticalActive) {
      beepNonBlocking();
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

/**
 * @brief Mesure la distance à l'aide du capteur ultrasonique HC-SR04.
 * 
 * Cette fonction déclenche le capteur ultrasonique pour envoyer une impulsion
 * sonore et mesure le temps que met l'écho à revenir. Elle calcule ensuite la
 * distance en centimètres en utilisant la formule basée sur la vitesse du son.
 * 
 * @return float La distance mesurée en centimètres.
 * 
 * @note La fonction utilise un timeout de 30 ms pour éviter les blocages
 *       si aucun écho n'est reçu.
 */
float distanceByUltrasonSensor() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
  float distance = (duration * 0.0343) / 2;
  return distance;
}

/**
 * @brief Calibre le capteur ultrasonique pour déterminer la distance de référence.
 * 
 * Cette fonction mesure la distance actuelle à l'aide du capteur ultrasonique
 * et la stocke comme distance de calibration. Elle définit également les niveaux
 * critiques, d'alerte et normaux du lac en fonction de cette distance.
 * 
 * @details
 * Les niveaux sont définis comme suit:
 * - Niveau normal: 70% de la distance calibrée
 * - Niveau d'alerte: 50% de la distance calibrée
 * - Niveau critique: 30% de la distance calibrée
 * 
 * Après l'appel de cette fonction, le système est considéré comme calibré.
 */
void calibrate() {
  calibrationDistance = distanceByUltrasonSensor();
  isCalibrated = true;

  setLakeLevels(calibrationDistance * 0.7,
                calibrationDistance * 0.5,
                calibrationDistance * 0.3);

  // Serial.print("Calibrated at: ");
  // Serial.println(calibrationDistance);
}


/**
 * @brief Définit les niveaux critiques, d'alerte et normaux du lac
 * 
 * Cette fonction initialise les seuils de niveau d'eau pour le lac.
 * 
 * @param normal Niveau d'eau normal (en unités cm)
 * @param alert Niveau d'eau d'alerte (en unités cm)
 * @param critical Niveau d'eau critique (en unités cm)
 */
void setLakeLevels(float normal, float alert, float critical) {
  lakeLevels.normalLevel = normal;
  lakeLevels.alertLevel = alert;
  lakeLevels.criticalLevel = critical;
}

/**
 * @brief Gère les indicateurs LED en fonction du niveau d'eau du lac
 * 
 * Cette fonction contrôle l'allumage des trois LED (verte, jaune et rouge)
 * en fonction du niveau d'eau mesuré et le compare avec les seuils critiques
 * et d'alerte définis.
 * 
 * @param waterLevel Le niveau d'eau actuel du lac (en unités cm)
 * 
 * @details
 * Les états possibles sont:
 * - CRITIQUE: Niveau d'eau <= lakeLevels.criticalLevel
 *   - LED verte allumée, jaune et rouge éteintes
 *   - criticalActive défini à false
 * 
 * - ALERTE: Niveau d'eau <= lakeLevels.alertLevel
 *   - LED jaune allumée, verte et rouge éteintes
 *   - criticalActive défini à false
 * 
 * - NORMAL: Niveau d'eau > lakeLevels.alertLevel
 *   - LED rouge allumée, verte et jaune éteintes
 *   - criticalActive défini à true
 * 
 * @note Les broches GPIO doivent être correctement configurées en sortie
 *       dans la fonction setup() avant d'appeler cette fonction.
 * 
 * @see GREEN_LED_PIN, YELLOW_LED_PIN, RED_LED_PIN, lakeLevels, criticalActive
 */
void handleLevelIndicators(float waterLevel) {

  if (waterLevel <= lakeLevels.criticalLevel) {        // CRITICAL
    digitalWrite(GREEN_LED_PIN, HIGH);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    criticalActive = false;

  } else if (waterLevel <= lakeLevels.alertLevel) {    // ALERT
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, HIGH);
    digitalWrite(RED_LED_PIN, LOW);
    criticalActive = false;

  } else {                                            // NORMAL
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, HIGH);
    criticalActive = true;
  }
}

/**
 * @brief Bascule l'état du buzzer de manière non-bloquante à intervalles réguliers.
 * 
 * Cette fonction alterne l'état du buzzer (activé/désactivé) tous les 120 millisecondes
 * sans bloquer l'exécution du programme. Elle utilise un délai basé sur millis() pour
 * gérer le timing de manière asynchrone.
 * 
 * @note Requiert que les variables globales suivantes soient définies:
 *       - lastBeepToggle: timestamp du dernier basculement (unsigned long)
 *       - buzzerState: état actuel du buzzer (boolean)
 *       - BUZZER_PIN: numéro de la broche du buzzer (int)
 * 
 * @return void
 */
void beepNonBlocking() {
  if (millis() - lastBeepToggle > 120) {
    lastBeepToggle = millis();
    buzzerState = !buzzerState;
    digitalWrite(BUZZER_PIN, buzzerState);
  }
}

String getLakeStateLabel(float waterLevel) {
  if (!isCalibrated) {
    return "EN ATTENTE DE CALIBRATION";
  }

  if (waterLevel <= lakeLevels.criticalLevel) {
    return "NIVEAU BAS";
  }

  if (waterLevel <= lakeLevels.alertLevel) {
    return "VIGILANCE";
  }

  return "CRITIQUE";
}

void handleWebClient() {
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  unsigned long timeoutStart = millis();
  while (client.connected() && !client.available() && millis() - timeoutStart < timeoutTime) {
    delay(1);
  }

  if (!client.available()) {
    client.stop();
    return;
  }

  String requestLine = client.readStringUntil('\r');
  client.readStringUntil('\n');

  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() <= 1) {
      break;
    }
  }

  if (requestLine.indexOf("GET /api/status") >= 0) {
    sendJsonStatus(client);
  } else {
    sendDashboardPage(client);
  }

  delay(1);
  client.stop();
}

void sendJsonStatus(WiFiClient &client) {
  unsigned long uptimeSec = (millis() - systemStartMillis) / 1000;
  unsigned long sinceLastMeasureSec = lastMeasureMillis > 0 ? (millis() - lastMeasureMillis) / 1000 : 0;

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type: application/json");
  client.println("Cache-Control: no-store");
  client.println("Connection: close");
  client.println();

  String payload = "{";
  payload += "\"calibrated\":" + String(isCalibrated ? "true" : "false") + ",";
  payload += "\"state\":\"" + currentState + "\",";
  payload += "\"waterLevelCm\":" + String(currentWaterLevel, 2) + ",";
  payload += "\"distanceCm\":" + String(currentDistance, 2) + ",";
  payload += "\"fillPercent\":" + String(currentFillPercent, 1) + ",";
  payload += "\"normalThresholdCm\":" + String(lakeLevels.normalLevel, 2) + ",";
  payload += "\"alertThresholdCm\":" + String(lakeLevels.alertLevel, 2) + ",";
  payload += "\"criticalThresholdCm\":" + String(lakeLevels.criticalLevel, 2) + ",";
  payload += "\"calibrationDistanceCm\":" + String(calibrationDistance, 2) + ",";
  payload += "\"criticalAlarm\":" + String(criticalActive ? "true" : "false") + ",";
  payload += "\"uptimeSec\":" + String(uptimeSec) + ",";
  payload += "\"sinceLastMeasureSec\":" + String(sinceLastMeasureSec);
  payload += "}";

  client.println(payload);
}

void sendDashboardPage(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type: text/html; charset=utf-8");
  client.println("Cache-Control: no-store");
  client.println("Connection: close");
  client.println();

  client.println(R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Lake Level Dashboard</title>
  <style>
    :root {
      --paper: #ebe7de;
      --ink: #221f1f;
      --panel: #d9c1e1;
      --card: #f2ebf4;
      --line: #2a2525;
      --ok: #2e7d32;
      --warn: #ef6c00;
      --danger: #b71c1c;
      --muted: #5e5656;
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      background: var(--paper);
      color: var(--ink);
      font-family: "Arial", "Helvetica", sans-serif;
      min-height: 100vh;
      padding: 20px;
    }

    .frame {
      border: 1px solid rgba(0,0,0,0.15);
      max-width: 1180px;
      margin: 0 auto;
      display: grid;
      grid-template-columns: 34% 66%;
      background: #f1eee7;
      min-height: calc(100vh - 40px);
    }

    .left {
      border-right: 1px solid rgba(0,0,0,0.15);
      padding: 28px 24px;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
    }

    .top-label {
      font-size: 12px;
      letter-spacing: 0.1em;
      text-transform: uppercase;
      color: var(--muted);
      margin-bottom: 18px;
    }

    .brand {
      font-size: 56px;
      font-weight: 700;
      line-height: 0.9;
      margin: 0 0 8px;
    }

    .subtitle {
      font-size: 18px;
      margin: 0;
      color: var(--muted);
    }

    .state-pill {
      margin-top: 28px;
      display: inline-block;
      padding: 8px 14px;
      border-radius: 22px;
      border: 1px solid var(--line);
      font-size: 13px;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      background: transparent;
    }

    .state-pill.ok { background: rgba(46,125,50,0.12); border-color: var(--ok); }
    .state-pill.warn { background: rgba(239,108,0,0.15); border-color: var(--warn); }
    .state-pill.danger { background: rgba(183,28,28,0.15); border-color: var(--danger); }

    .left-footer {
      font-size: 13px;
      color: var(--muted);
      border-top: 1px solid rgba(0,0,0,0.2);
      padding-top: 16px;
      margin-top: 20px;
      display: grid;
      gap: 6px;
    }

    .right {
      background: var(--panel);
      padding: 28px 26px 20px;
      display: grid;
      grid-template-rows: auto 1fr auto;
      gap: 22px;
    }

    .header {
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      border-bottom: 1px solid rgba(0,0,0,0.18);
      padding-bottom: 10px;
    }

    .header h1 {
      margin: 0;
      font-size: 24px;
      letter-spacing: 0.02em;
    }

    .header .ip {
      font-size: 13px;
      color: #3d3434;
    }

    .grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 14px;
      align-content: start;
    }

    .card {
      background: var(--card);
      border: 1px solid rgba(0,0,0,0.18);
      padding: 14px;
      min-height: 112px;
      display: grid;
      gap: 8px;
      align-content: start;
    }

    .card .label {
      font-size: 12px;
      text-transform: uppercase;
      letter-spacing: 0.08em;
      color: var(--muted);
    }

    .card .value {
      font-size: 34px;
      line-height: 1;
      font-weight: 700;
    }

    .card .small {
      font-size: 13px;
      color: #3d3535;
    }

    .bottom {
      border-top: 1px solid rgba(0,0,0,0.2);
      padding-top: 12px;
      display: flex;
      justify-content: space-between;
      font-size: 13px;
      color: #3d3535;
    }

    @media (max-width: 900px) {
      .frame { grid-template-columns: 1fr; }
      .left { border-right: none; border-bottom: 1px solid rgba(0,0,0,0.15); }
      .grid { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <main class="frame">
    <section class="left">
      <div>
        <div class="top-label">Station de surveillance</div>
        <h2 class="brand">Lake<br/>Level</h2>
        <p class="subtitle">Lake Level Monitoring</p>
        <div id="statePill" class="state-pill">Chargement...</div>
      </div>

      <div class="left-footer">
        <div><strong>État capteur</strong>: <span id="calibStatus">-</span></div>
        <div><strong>Alarme critique</strong>: <span id="alarmStatus">-</span></div>
        <div><strong>Dernière mesure</strong>: <span id="lastMeasure">-</span></div>
      </div>
    </section>

    <section class="right">
      <header class="header">
        <h1>Tableau de bord du niveau d’eau</h1>
        <div class="ip">Mise à jour automatique (2s)</div>
      </header>

      <div class="grid">
        <article class="card">
          <div class="label">Niveau d’eau actuel</div>
          <div class="value" id="waterLevel">--</div>
          <div class="small">Centimètres</div>
        </article>

        <article class="card">
          <div class="label">Taux de remplissage</div>
          <div class="value" id="fillPercent">--</div>
          <div class="small">Par rapport à la calibration</div>
        </article>

        <article class="card">
          <div class="label">Distance capteur-surface</div>
          <div class="value" id="distance">--</div>
          <div class="small">Plus la distance diminue, plus l’eau monte</div>
        </article>

        <article class="card">
          <div class="label">Seuils (cm)</div>
          <div class="small">Bas: <strong id="criticalThreshold">--</strong></div>
          <div class="small">Vigilance: <strong id="alertThreshold">--</strong></div>
          <div class="small">Critique: <strong id="normalThreshold">--</strong></div>
        </article>
      </div>

      <footer class="bottom">
        <span>Calibration: <strong id="calibrationDistance">--</strong> cm</span>
        <span>Uptime: <strong id="uptime">--</strong></span>
      </footer>
    </section>
  </main>

  <script>
    const statePill = document.getElementById('statePill');
    const waterLevel = document.getElementById('waterLevel');
    const fillPercent = document.getElementById('fillPercent');
    const distance = document.getElementById('distance');
    const criticalThreshold = document.getElementById('criticalThreshold');
    const alertThreshold = document.getElementById('alertThreshold');
    const normalThreshold = document.getElementById('normalThreshold');
    const calibrationDistance = document.getElementById('calibrationDistance');
    const uptime = document.getElementById('uptime');
    const calibStatus = document.getElementById('calibStatus');
    const alarmStatus = document.getElementById('alarmStatus');
    const lastMeasure = document.getElementById('lastMeasure');

    function formatDuration(totalSec) {
      const s = Number(totalSec || 0);
      const h = Math.floor(s / 3600);
      const m = Math.floor((s % 3600) / 60);
      const sec = s % 60;
      return `${h}h ${m}m ${sec}s`;
    }

    function applyStateStyle(stateText) {
      statePill.classList.remove('ok', 'warn', 'danger');
      if (stateText.includes('CRITIQUE')) {
        statePill.classList.add('danger');
      } else if (stateText.includes('VIGILANCE')) {
        statePill.classList.add('warn');
      } else {
        statePill.classList.add('ok');
      }
    }

    async function refreshStatus() {
      try {
        const response = await fetch('/api/status', { cache: 'no-store' });
        if (!response.ok) {
          throw new Error('HTTP ' + response.status);
        }

        const data = await response.json();

        statePill.textContent = data.state || '-';
        applyStateStyle(statePill.textContent);
        waterLevel.textContent = `${Number(data.waterLevelCm || 0).toFixed(2)}`;
        fillPercent.textContent = `${Number(data.fillPercent || 0).toFixed(1)}%`;
        distance.textContent = `${Number(data.distanceCm || 0).toFixed(2)}`;

        criticalThreshold.textContent = Number(data.criticalThresholdCm || 0).toFixed(2);
        alertThreshold.textContent = Number(data.alertThresholdCm || 0).toFixed(2);
        normalThreshold.textContent = Number(data.normalThresholdCm || 0).toFixed(2);
        calibrationDistance.textContent = Number(data.calibrationDistanceCm || 0).toFixed(2);

        uptime.textContent = formatDuration(data.uptimeSec);
        calibStatus.textContent = data.calibrated ? 'Calibré' : 'Non calibré';
        alarmStatus.textContent = data.criticalAlarm ? 'ACTIVE' : 'Inactive';
        lastMeasure.textContent = `${Number(data.sinceLastMeasureSec || 0)} sec`;
      } catch (error) {
        statePill.textContent = 'Connexion perdue';
        statePill.classList.remove('ok', 'warn');
        statePill.classList.add('danger');
      }
    }

    refreshStatus();
    setInterval(refreshStatus, 2000);
  </script>
</body>
</html>
  )rawliteral");
}

void processData(AsyncResult &aResult) {
  if (!aResult.isResult())
    return;

  if (aResult.isEvent())
    Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());

  if (aResult.isDebug())
    Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());

  if (aResult.isError())
    Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());

  if (aResult.available())
    Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
}