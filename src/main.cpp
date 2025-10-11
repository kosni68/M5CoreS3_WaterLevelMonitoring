/* 
  M5CoreS3_JSN_SR04T_Final_Sleep_MQTT.ino
  - TRIG = GPIO9, ECHO = GPIO8
  - Wake timer every 30s -> measure -> optionally publish MQTT -> deep sleep
  - Touch while awake -> interactive mode (web + screen) for 10 minutes inactivity
  - Display IP when WiFi connected (updated), show gauge (180px), show % inside
  - Partial redraw to avoid flicker
  - MQTT disabled by default (ENABLE_MQTT=false)
*/

#include <M5CoreS3.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>   // add to platformio.ini: PubSubClient
#include <mutex>

#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <driver/rtc_io.h>

#define SLEEP_INTERVAL_SEC 30        // réveil périodique
#define INACTIVITY_TIMEOUT_MS 600000 // 10 min
#define TOUCH_WAKEUP_PIN GPIO_NUM_3   // GPIO relié au touch (à adapter si besoin)

unsigned long lastTouchMillis = 0;
bool screenActive = true;
bool wifiActive = true;

//
// =========== CONFIG =============
const char* WIFI_SSID = "Freebox-22A0D2";
const char* WIFI_PASS = "NicoCindy22";

// MQTT (disabled by default)
const bool ENABLE_MQTT = false;
const char* MQTT_HOST = "192.168.1.10"; // exemple
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASS = "";
const char* MQTT_TOPIC = "m5stack/puits";

// pins + timing
const int trigPin = 9;
const int echoPin = 8;
const int SENSOR_PERIOD_MS = 200;
const int DISPLAY_PERIOD_MS = 300;

// deep sleep interval (seconds)
const uint64_t DEEPSLEEP_INTERVAL_S = 30ULL;
// interactive timeout (ms) before going back to deep-sleep
const uint32_t INTERACTIVE_TIMEOUT_MS = 10 * 60 * 1000UL; // 10 minutes

// =========== GLOBALS ===========
WebServer server(80);
Preferences preferences;
std::mutex distMutex;

// measurement state
volatile float lastMeasuredCm = -1.0f;
volatile unsigned long lastDurationUs = 0;
volatile float lastEstimatedHeight = -1.0f;

// calibrations (m = measured, h = real height)
float calib_m[3] = {0.0f, 0.0f, 0.0f};
float calib_h[3] = {50.0f, 100.0f, 150.0f};

// cuve levels
float cuveVide = 123.0f;
float cuvePleine = 42.0f;

// polynomial coeffs
double a_coef=0,b_coef=0,c_coef=0;

// display partial redraw variables
const int gaugeX = 250, gaugeY = 30, gaugeW = 60, gaugeH = 180;
float prevMeasured = NAN, prevEstimated = NAN;
unsigned long prevDuration = ULONG_MAX;
float prevCuveVide = NAN, prevCuvePleine = NAN;
int prevPercent = -1;

// interactive mode state
volatile bool interactiveMode = false;
uint32_t interactiveLastTouchMs = 0;

// MQTT client
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// forward declarations
float measureDistanceCmOnce();
float runningAverage(float newVal, float prevAvg, float alpha=0.25f);
void loadCalibrations();
void saveCalibrationToNVS(int idx, float measured, float height);
void saveCuveLevels();
void clearCalibrations();
bool computePolynomialFrom3Points();
float estimateHeightFromMeasured(float x);
String makeJsonDistance();
String makeJsonCalibs();
void handleRoot();
void handleDistanceApi();
void handleCalibsApi();
void handleSaveCalib();
void handleClearCalib();
void handleSetCuve();

// ========= Helper network functions =========
bool connectWiFiShort(uint32_t timeoutMs=8000) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(200);
  }
  return (WiFi.status() == WL_CONNECTED);
}

void disconnectWiFiClean() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
  }
}

// ===== MQTT publish (disabled by default) =====
void setupMQTT() {
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
}

bool publishMQTT_measure() {
  if (!ENABLE_MQTT) return true;
  if (!mqttClient.connected()) {
    // try connect
    String clientId = String("M5CoreS3-") + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (strlen(MQTT_USER) == 0) {
      if (!mqttClient.connect(clientId.c_str())) return false;
    } else {
      if (!mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) return false;
    }
  }
  // prepare JSON
  float m, h; unsigned long dur;
  { std::lock_guard<std::mutex> lock(distMutex); m = lastMeasuredCm; h = lastEstimatedHeight; dur = lastDurationUs; }
  char payload[256];
  snprintf(payload, sizeof(payload), "{\"measured_cm\":%.2f,\"estimated_cm\":%.2f,\"duration_us\":%lu}", m, h, dur);
  bool ok = mqttClient.publish(MQTT_TOPIC, payload);
  mqttClient.disconnect();
  return ok;
}

// ========= Measurement =========
float measureDistanceCmOnce() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(5);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(20);
  digitalWrite(trigPin, LOW);
  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL);
  lastDurationUs = duration;
  if (duration == 0) return -1.0f;
  return duration * 0.01715f;
}

float runningAverage(float newVal, float prevAvg, float alpha) {
  if (newVal < 0) return prevAvg;
  return prevAvg * (1.0f - alpha) + newVal * alpha;
}

// ============ Persistence & polynomial ============
void loadCalibrations() {
  preferences.begin("calib", true);
  for (int i=0;i<3;i++) {
    char kM[8], kH[8];
    sprintf(kM,"m%d",i); sprintf(kH,"h%d",i);
    calib_m[i] = preferences.getFloat(kM, calib_m[i]);
    calib_h[i] = preferences.getFloat(kH, calib_h[i]);
  }
  cuveVide = preferences.getFloat("cuveVide", cuveVide);
  cuvePleine = preferences.getFloat("cuvePleine", cuvePleine);
  preferences.end();
}

void saveCalibrationToNVS(int idx, float measured, float height) {
  if (idx<0 || idx>2) return;
  preferences.begin("calib", false);
  char kM[8], kH[8];
  sprintf(kM,"m%d",idx); sprintf(kH,"h%d",idx);
  preferences.putFloat(kM, measured);
  preferences.putFloat(kH, height);
  preferences.end();
  calib_m[idx]=measured; calib_h[idx]=height;
}

void saveCuveLevels() {
  preferences.begin("calib", false);
  preferences.putFloat("cuveVide", cuveVide);
  preferences.putFloat("cuvePleine", cuvePleine);
  preferences.end();
}

void clearCalibrations() {
  preferences.begin("calib", false);
  preferences.clear();
  preferences.end();
  for (int i=0;i<3;i++) calib_m[i]=0;
}

bool computePolynomialFrom3Points() {
  double x1=calib_m[0], x2=calib_m[1], x3=calib_m[2];
  double y1=calib_h[0], y2=calib_h[1], y3=calib_h[2];
  if (x1==0||x2==0||x3==0) return false;
  if ((x1==x2)||(x1==x3)||(x2==x3)) return false;
  double denom=(x1-x2)*(x1-x3)*(x2-x3);
  if (denom==0) return false;
  a_coef=( x3*(y2-y1)+x2*(y1-y3)+x1*(y3-y2) )/denom;
  b_coef=( x3*x3*(y1-y2)+x2*x2*(y3-y1)+x1*x1*(y2-y3) )/denom;
  c_coef=( x2*x3*(x2-x3)*y1 + x3*x1*(x3-x1)*y2 + x1*x2*(x1-x2)*y3 )/denom;
  return true;
}

float estimateHeightFromMeasured(float x) {
  return (float)(a_coef*x*x + b_coef*x + c_coef);
}

// ======== Web handlers & JSON ==========
String makeJsonDistance() {
  float m,h; unsigned long dur;
  {
    std::lock_guard<std::mutex> lock(distMutex);
    m = lastMeasuredCm; h = lastEstimatedHeight; dur = lastDurationUs;
  }
  char buf[256];
  if (m < 0) snprintf(buf,sizeof(buf), "{\"measured_cm\":null,\"estimated_cm\":null,\"duration_us\":%lu,\"cuveVide\":%.1f,\"cuvePleine\":%.1f}", dur, cuveVide, cuvePleine);
  else snprintf(buf,sizeof(buf), "{\"measured_cm\":%.2f,\"estimated_cm\":%.2f,\"duration_us\":%lu,\"cuveVide\":%.1f,\"cuvePleine\":%.1f}", m, h, dur, cuveVide, cuvePleine);
  return String(buf);
}

String makeJsonCalibs() {
  String s = "{\"calibs\":[";
  for (int i=0;i<3;i++){
    char tmp[96];
    snprintf(tmp,sizeof(tmp), "{\"index\":%d,\"measured\":%.2f,\"height\":%.2f}", i, calib_m[i], calib_h[i]);
    s += String(tmp);
    if (i<2) s += ",";
  }
  s += "]}";
  return s;
}

void handleRoot() {
  // same HTML as before but trimmed for brevity - full page includes Chart.js + controls
  // For space I'll keep the same content previously delivered; we reuse it.
  String page = R"rawliteral(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5 Puits - Dashboard</title>
<style>body{font-family:sans-serif;padding:10px} input{width:70px}</style>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head><body>
<h2>M5 Puits - Dashboard</h2>
<div>Mesuré: <span id="meas">--</span> cm &nbsp; Estimé: <span id="est">--</span> cm &nbsp; Brut: <span id="dur">--</span> µs</div>
<hr>
<canvas id="chart" width="400" height="150"></canvas>
<hr>
<div id="calibs"></div>
<h3>Cuve levels</h3>
Vide: <input id="v" value="123"> cm Pleine: <input id="p" value="42"> cm <button onclick="saveCuve()">Save</button>
<button onclick="clearCalib()">Clear calibs</button>

<script>
let labels=[], measData=[], estData=[], durData=[];
const ctx=document.getElementById('chart').getContext('2d');
const chart=new Chart(ctx,{type:'line',data:{labels:labels,datasets:[
  {label:'Mes (cm)',data:measData,borderColor:'blue',fill:false,yAxisID:'y1'},
  {label:'Est (cm)',data:estData,borderColor:'green',fill:false,yAxisID:'y1'},
  {label:'Dur (us)',data:durData,borderColor:'red',fill:false,yAxisID:'y2'}]},
  options:{responsive:true, scales:{ y1:{type:'linear',position:'left'}, y2:{type:'linear',position:'right'} }}});

function refreshDistance(){
  fetch('/distance').then(r=>r.json()).then(j=>{
    document.getElementById('meas').innerText = j.measured_cm!==null?j.measured_cm.toFixed(1):'--';
    document.getElementById('est').innerText = j.estimated_cm!==null?j.estimated_cm.toFixed(1):'--';
    document.getElementById('dur').innerText = j.duration_us!==null?j.duration_us:'--';
    const t=new Date().toLocaleTimeString(); labels.push(t);
    if(labels.length>60){labels.shift();measData.shift();estData.shift();durData.shift();}
    measData.push(j.measured_cm||0); estData.push(j.estimated_cm||0); durData.push(j.duration_us||0);
    chart.update();
  });
}
function refreshCalibs(){
  fetch('/calibs').then(r=>r.json()).then(j=>{
    let html='';
    j.calibs.forEach(function(c){ html += 'C'+(c.index+1)+': Mesuré='+ (c.measured>0?c.measured.toFixed(1):'--') +' Hauteur:<input id=\"h'+c.index+'\" value=\"'+c.height+'\"> <button onclick=\"save('+c.index+')\">Save</button><br>';});
    document.getElementById('calibs').innerHTML = html;
  });
}
function save(id){
  const val = document.getElementById('h'+id).value;
  fetch('/save_calib?id='+id+'&height='+val, {method:'POST'}).then(r=>r.json()).then(j=>{ if (j.ok) alert('Saved'); refreshCalibs();});
}
function saveCuve(){ const v=document.getElementById('v').value; const p=document.getElementById('p').value; fetch('/setCuve?vide='+v+'&pleine='+p,{method:'POST'}).then(r=>r.json()).then(j=>{ if(j.ok) alert('Saved cuve');}); }
function clearCalib(){ fetch('/clear_calib',{method:'POST'}).then(r=>r.json()).then(j=>{ alert('Cleared'); refreshCalibs();}); }

setInterval(refreshDistance,800); setInterval(refreshCalibs,5000);
refreshDistance(); refreshCalibs();
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html", page);
}

void handleDistanceApi() { server.send(200, "application/json", makeJsonDistance()); }
void handleCalibsApi()  { server.send(200, "application/json", makeJsonCalibs()); }

void handleSaveCalib() {
  if (!server.hasArg("id") || !server.hasArg("height")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
  int id = server.arg("id").toInt();
  float height = server.arg("height").toFloat();
  float measured;
  { std::lock_guard<std::mutex> lock(distMutex); measured = lastMeasuredCm; }
  if (measured <= 0.0f) { server.send(200, "application/json", "{\"ok\":false,\"err\":\"no echo\"}"); return; }
  saveCalibrationToNVS(id, measured, height);
  computePolynomialFrom3Points();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleClearCalib() {
  clearCalibrations(); computePolynomialFrom3Points();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleSetCuve() {
  if (server.hasArg("vide")) cuveVide = server.arg("vide").toFloat();
  if (server.hasArg("pleine")) cuvePleine = server.arg("pleine").toFloat();
  saveCuveLevels();
  server.send(200, "application/json", "{\"ok\":true}");
}

// ======= Display helpers (partial redraw) =======
void drawGaugeBackground() {
  M5.Display.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, TFT_WHITE);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(gaugeX + gaugeW + 6, gaugeY - 4); M5.Display.print("Pleine");
  M5.Display.setCursor(gaugeX + gaugeW + 6, gaugeY + gaugeH - 8); M5.Display.print("Vide");
}

void drawGaugeFill(int percent) {
  // clear inner area
  M5.Display.fillRect(gaugeX+1, gaugeY+1, gaugeW-2, gaugeH-2, TFT_BLACK);
  int fillH = (int)((percent/100.0f) * (gaugeH-4));
  if (fillH > 0) {
    int yFill = gaugeY + gaugeH - 2 - fillH;
    M5.Display.fillRect(gaugeX+2, yFill, gaugeW-4, fillH, TFT_BLUE);
  }
  M5.Display.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, TFT_WHITE);
  char buf[8]; snprintf(buf, sizeof(buf), "%d%%", percent);
  int tx = gaugeX + (gaugeW/2) - 12; int ty = gaugeY + (gaugeH/2) - 8;
  M5.Display.setTextSize(2); M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(tx, ty); M5.Display.print(buf);
}

// ======= Tasks =======
void periodicMeasureAndMaybeMQTT_andSleep() {
  // Called when wake from timer and not entering interactive mode
  // Perform a quick measurement, optional MQTT send, then deep-sleep again
  float avg = 0.0f;
  // do a few quick samples to stabilize
  for (int i=0;i<3;i++) {
    float m = measureDistanceCmOnce();
    avg = runningAverage(m, avg, 0.5f);
    delay(30);
  }
  float est = NAN;
  if (avg > 0) {
    // do not rely on polynomial if not computed; best-effort
    est = estimateHeightFromMeasured(avg);
    std::lock_guard<std::mutex> lock(distMutex);
    lastMeasuredCm = avg; lastEstimatedHeight = est;
  }
  // connect WiFi briefly to publish MQTT (if enabled)
  if (ENABLE_MQTT) {
    if (connectWiFiShort(6000)) {
      setupMQTT();
      publishMQTT_measure();
      disconnectWiFiClean();
    }
  }
  // now go back to deep-sleep (timer)
  esp_sleep_enable_timer_wakeup(DEEPSLEEP_INTERVAL_S * 1000000ULL);
  delay(20);
  esp_deep_sleep_start();
}

// sensor task (used in interactive mode)
void sensorTask(void *pv) {
  float avg = 0.0f;
  for (;;) {
    float m = measureDistanceCmOnce();
    avg = runningAverage(m, avg, 0.25f);
    float est = NAN;
    if (avg > 0.0f) est = estimateHeightFromMeasured(avg);
    {
      std::lock_guard<std::mutex> lock(distMutex);
      lastMeasuredCm = avg; lastEstimatedHeight = est;
    }
    vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
  }
}

void displayTask(void *pv) {
  static float lastM = -1, lastEst = -1;
  static String lastIP = "";
  static int lastLevel = -1;

  for (;;) {
    M5.update();

    float m, est;
    {
      std::lock_guard<std::mutex> lock(distMutex);
      m = lastMeasuredCm;
      est = lastEstimatedHeight;
    }

    // Rafraîchir uniquement si changement
    if (fabs(m - lastM) > 0.5f || fabs(est - lastEst) > 0.5f) {
      M5.Display.fillRect(0, 0, M5.Display.width(), 80, TFT_BLACK);
      M5.Display.setCursor(6, 6);
      M5.Display.setTextSize(3);
      if (m > 0)
        M5.Display.printf("Mes: %.1f cm\n", m);
      else
        M5.Display.printf("Mes: -- cm\n");
      M5.Display.setTextSize(2);
      if (est > -0.5f)
        M5.Display.printf("Ht: %.1f cm\n", est);
      else
        M5.Display.printf("Ht: --\n");

      lastM = m;
      lastEst = est;
    }

    // --- Affichage jauge graphique ---
    int topY = 100;
    int gaugeHeight = 180;
    int fullDist = 123; // distance cuve vide
    int emptyDist = 42; // distance cuve pleine
    float ratio = (fullDist - m) / (fullDist - emptyDist);
    ratio = constrain(ratio, 0.0f, 1.0f);
    int levelH = (int)(gaugeHeight * ratio);

    if (abs(levelH - lastLevel) > 3) {
      int baseY = topY + gaugeHeight;
      M5.Display.fillRect(20, topY, 40, gaugeHeight, TFT_DARKGREY);
      M5.Display.fillRect(20, baseY - levelH, 40, levelH, TFT_BLUE);
      lastLevel = levelH;
    }

    // --- Affichage IP ---
    String ip = WiFi.localIP().toString();
    if (ip != lastIP) {
      M5.Display.fillRect(0, 280, 320, 20, TFT_BLACK);
      M5.Display.setCursor(6, 280);
      M5.Display.setTextSize(2);
      M5.Display.printf("IP: %s", ip.c_str());
      lastIP = ip;
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}

// ======== setup & main loop ===========
void startInteractiveMode() {
  // called when user touches while device is awake or when user wants UI
  // connect WiFi and start web services and tasks
  if (!connectWiFiShort(8000)) {
    // start AP fallback
    WiFi.mode(WIFI_AP);
    WiFi.softAP("M5CoreS3_Puits");
  }
  // start server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/distance", HTTP_GET, handleDistanceApi);
  server.on("/calibs", HTTP_GET, handleCalibsApi);
  server.on("/save_calib", HTTP_POST, handleSaveCalib);
  server.on("/clear_calib", HTTP_POST, handleClearCalib);
  server.on("/setCuve", HTTP_POST, handleSetCuve);
  server.begin();

  // start interactive tasks
  xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(displayTask, "displayTask", 4096, NULL, 1, NULL, 1);

  interactiveMode = true;
  interactiveLastTouchMs = millis();
}

void goToDeepSleep() {
  Serial.println("➡️ Mise en veille profonde...");

  // Éteindre l’écran proprement
  M5.Display.clear();
  M5.Display.sleep();

  // Couper le WiFi pour économie
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Activer réveil tactile et timer
  esp_sleep_enable_touchpad_wakeup();
  esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_SEC * 1000000ULL);

  // Entrée en deep sleep
  esp_deep_sleep_start();
}

void stopInteractiveModeAndSleep() {
  // stop webserver and go to deep-sleep
  server.stop();
  disconnectWiFiClean();
  // small delay
  delay(50);
  // schedule deep sleep
  esp_sleep_enable_timer_wakeup(DEEPSLEEP_INTERVAL_S * 1000000ULL);
  delay(20);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  M5.begin();
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(6, 6); M5.Display.println("Boot...");

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  loadCalibrations();
  computePolynomialFrom3Points();
  setupMQTT();

  // Determine wake reason
  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  Serial.printf("Wake reason: %d\n", (int)wakeReason);

  // If we woke from timer, perform a quick measure and publish MQTT, then go back to sleep
  if (wakeReason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Periodic wake: quick measure -> MQTT -> sleep");
    periodicMeasureAndMaybeMQTT_andSleep();
    // should not return
  }

  // Otherwise cold boot or manual reset -> enter interactive mode by default
  // Connect WiFi and run interactive UI
  startInteractiveMode();
}

void loop() {
  server.handleClient();
  M5.update();

  // --- Détection tactile ---
  if (M5.Touch.getDetail().isPressed()) {
    lastTouchMillis = millis();
    if (!screenActive) {
      // Réactivation de l’écran et du WiFi
      screenActive = true;
      M5.Display.wakeup();
      M5.Display.setBrightness(180);
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
  }

  // --- Gestion inactivité ---
  if (screenActive && (millis() - lastTouchMillis > INACTIVITY_TIMEOUT_MS)) {
    goToDeepSleep();
  }

  // --- Mesure périodique ---
  static unsigned long lastMeasure = 0;
  if (millis() - lastMeasure > SLEEP_INTERVAL_SEC * 1000UL) {
    lastMeasure = millis();
    // lancer une mesure et calcul polynomial ici
    float m = measureDistanceCmOnce();
    float est = estimateHeightFromMeasured(m);
    {
      std::lock_guard<std::mutex> lock(distMutex);
      lastMeasuredCm = m;
      lastEstimatedHeight = est;
    }

    // futur: envoi MQTT ici
    Serial.printf("MQTT send (disabled): %.1f cm\n", est);
  }

  delay(10);
}
