// M5CoreS3_JSN_SR04T_Web_FreeRTOS.ino

#include <M5CoreS3.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <mutex>

// ---------- CONFIG ----------
const char* WIFI_SSID = "TON_SSID";
const char* WIFI_PASS = "TON_MOT_DE_PASSE";

const int trigPin = 9;
const int echoPin = 8;

const int SENSOR_PERIOD_MS = 200;    // intervalle mesures
const int DISPLAY_PERIOD_MS = 300;   // intervalle maj écran

// ---------- GLOBALS ----------
WebServer server(80);
Preferences preferences;

volatile float lastDistanceCm = 0.0f; // distance la plus récente (écrit dans task sensor)
std::mutex distMutex;

float calibs[3] = {50.0f, 100.0f, 150.0f}; // valeurs par défaut (cm)

// ---------- UTIL ----------
float measureDistanceCmOnce() {
  // déclenche le capteur JSN-SR04T
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // mesurer durée echo (us) - timeout 30000 us (≈5 m)
  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL);
  if (duration == 0) {
    // timeout, pas d'écho
    return -1.0f;
  }
  // distance en cm : durée (us) * 0.01715 (vitesse-son/2)
  float dist = duration * 0.01715f;
  return dist;
}

// moyenne glissante pour stabiliser
float runningAverage(float newVal, float prevAvg, float alpha = 0.2f) {
  if (newVal < 0) return prevAvg; // ignore timeouts
  return prevAvg * (1.0f - alpha) + newVal * alpha;
}

// ---------- PERSISTENCE ----------
void loadCalibrations() {
  preferences.begin("calib", true);
  for (int i = 0; i < 3; i++) {
    char key[8];
    sprintf(key, "c%d", i);
    if (preferences.isKey(key)) {
      calibs[i] = preferences.getFloat(key, calibs[i]);
    }
  }
  preferences.end();
}

void saveCalibration(int idx, float value) {
  if (idx < 0 || idx > 2) return;
  preferences.begin("calib", false);
  char key[8];
  sprintf(key, "c%d", idx);
  preferences.putFloat(key, value);
  preferences.end();
  calibs[idx] = value;
}


// ---------- WEB HANDLERS ----------
String jsonDistance() {
  float d;
  {
    std::lock_guard<std::mutex> lock(distMutex);
    d = lastDistanceCm;
  }
  JsonDocument doc;

  doc["distance_cm"] = d;
  String s;
  serializeJson(doc, s);
  return s;
}

String jsonCalibs() {
  JsonDocument doc;

  for (int i=0;i<3;i++) doc["calib"][i] = calibs[i];
  String s;
  serializeJson(doc, s);
  return s;
}

void handleRoot() {
  // page HTML simple — AJAX pour /distance et boutons pour sauver calibs
  String page = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>M5CoreS3 - Mesure Puits</title>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <style>
    body{font-family:sans-serif;padding:10px}
    .big{font-size:2rem;font-weight:700}
    .calib{margin-top:10px}
    button{padding:8px 12px;margin-right:8px}
  </style>
</head>
<body>
  <h2>M5CoreS3 — Mesure hauteur du puit</h2>
  <div>Distance mesurée: <span id="dist" class="big">-- cm</span></div>
  <div class="calib">
    <div>Calibrations:</div>
    <div>
      <span id="c0">C1: -- cm</span>
      <button onclick="saveCalib(0)">Sauver C1 (mesure courante)</button>
    </div>
    <div>
      <span id="c1">C2: -- cm</span>
      <button onclick="saveCalib(1)">Sauver C2 (mesure courante)</button>
    </div>
    <div>
      <span id="c2">C3: -- cm</span>
      <button onclick="saveCalib(2)">Sauver C3 (mesure courante)</button>
    </div>
  </div>
<script>
function fetchDist(){
  fetch('/distance').then(r=>r.json()).then(d=>{
    let dist = d.distance_cm;
    document.getElementById('dist').innerText = (dist>=0?dist.toFixed(1):'No echo');
  });
}
function fetchCalibs(){
  fetch('/calibs').then(r=>r.json()).then(d=>{
    let c = d.calib;
    document.getElementById('c0').innerText = 'C1: ' + c[0].toFixed(1) + ' cm';
    document.getElementById('c1').innerText = 'C2: ' + c[1].toFixed(1) + ' cm';
    document.getElementById('c2').innerText = 'C3: ' + c[2].toFixed(1) + ' cm';
  });
}
function saveCalib(id){
  fetch('/save_calib?id='+id, { method:'POST' })
    .then(r=>r.json()).then(j=>{
      if (j.ok) { fetchCalibs(); alert('Calibration sauvegardée.'); }
      else alert('Erreur.');
    });
}
setInterval(fetchDist, 800);
setInterval(fetchCalibs, 3000);
fetchDist(); fetchCalibs();
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", page);
}

void handleDistance() {
  server.send(200, "application/json", jsonDistance());
}

void handleCalibs() {
  server.send(200, "application/json", jsonCalibs());
}

void handleSaveCalib() {
  if (!server.hasArg("id")) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"no id\"}");
    return;
  }
  int id = server.arg("id").toInt();
  float cur;
  {
    std::lock_guard<std::mutex> lock(distMutex);
    cur = lastDistanceCm;
  }
  if (cur <= 0) {
    server.send(200, "application/json", "{\"ok\":false,\"err\":\"no echo\"}");
    return;
  }
  saveCalibration(id, cur);
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------- TASKS ----------
void sensorTask(void* pvParameters) {
  float avg = 0.0f;
  for (;;) {
    float m = measureDistanceCmOnce();
    avg = runningAverage(m, avg, 0.25f);
    {
      std::lock_guard<std::mutex> lock(distMutex);
      lastDistanceCm = avg;
    }
    vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
  }
}

void displayTask(void* pvParameters) {
  M5.Display.setTextSize(2);
  for (;;) {
    M5.update();
    // read distance snapshot
    float ds;
    {
      std::lock_guard<std::mutex> lock(distMutex);
      ds = lastDistanceCm;
    }
    M5.Display.fillRect(0, 0, M5.Display.width(), 60, TFT_BLACK);
    M5.Display.setCursor(6, 6);
    M5.Display.setTextSize(3);
    if (ds > 0) {
      M5.Display.printf("Distance: %.1f cm\n", ds);
    } else {
      M5.Display.printf("Distance: -- cm\n");
    }
    M5.Display.setTextSize(2);
    M5.Display.printf("C1: %.1f  C2: %.1f  C3: %.1f", calibs[0], calibs[1], calibs[2]);
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}

// ---------- SETUP ----------
void setup() {
  // Serial pour debug
  Serial.begin(115200);
  delay(100);
  M5.begin();
  M5.Lcd.println("Initialisation...");
  delay(500);

  // pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // load calib
  loadCalibrations();

  // Connect WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  M5.Display.setCursor(6, 80);
  M5.Display.println("Connexion WiFi...");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    M5.Display.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    // fallback: start AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP("M5CoreS3_Puits");
    M5.Display.println("AP: M5CoreS3_Puits");
    Serial.println("Started AP mode: M5CoreS3_Puits");
  }

  // Web routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/distance", HTTP_GET, handleDistance);
  server.on("/calibs", HTTP_GET, handleCalibs);
  server.on("/save_calib", HTTP_POST, handleSaveCalib);
  server.begin();

  // Create FreeRTOS tasks
  xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(displayTask, "displayTask", 4096, NULL, 1, NULL, 1);
}

// ---------- MAIN LOOP ----------
void loop() {
  // serveur web non bloquant: handle client
  server.handleClient();
  delay(10);
}
