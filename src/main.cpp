/* 
  M5CoreS3_JSN_SR04T_Final_Sleep_MQTT.ino
  - TRIG = GPIO9, ECHO = GPIO8
  - Wake timer every 30s -> measure -> optionally publish MQTT -> deep sleep
  - Touch while awake -> interactive mode (web + screen) for 10 minutes inactivity
  - Display IP when WiFi connected (updated), show gauge (180px), show % inside
  - Partial redraw to avoid flicker
  - MQTT disabled by default (ENABLE_MQTT=false)
*/

#define DEBUG true
#define DEBUG_PRINT(x)  if(DEBUG){ Serial.println(x); }
#define DEBUG_PRINTF(...) if(DEBUG){ Serial.printf(__VA_ARGS__); }


#include <M5CoreS3.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>   // add to platformio.ini: PubSubClient
#include <mutex>

//
// =========== CONFIG =============
const char* WIFI_SSID = "Freebox-22A0D2";
const char* WIFI_PASS = "NicoCindy22";

// MQTT (disabled by default)
const bool ENABLE_MQTT = true;
const char* MQTT_HOST = "192.168.1.171";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "ha-mqtt";
const char* MQTT_PASS = "ha-mqtt_68440";
const char* MQTT_TOPIC = "m5stack/puits";

// pins + timing
const int trigPin = 9;
const int echoPin = 8;
const int SENSOR_PERIOD_MS = 200;
const int DISPLAY_PERIOD_MS = 300;

// deep sleep interval (seconds)
const uint64_t DEEPSLEEP_INTERVAL_S = 30ULL;
// interactive timeout (ms) before going back to deep-sleep
const uint32_t INTERACTIVE_TIMEOUT_MS = 2 * 60 * 1000UL; // 10 minutes

// Conserve cette variable dans la mémoire RTC (survit au deep sleep)
RTC_DATA_ATTR bool wokeFromTimer = false;

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
  if (!ENABLE_MQTT) {
    DEBUG_PRINT("[MQTT] MQTT disabled -> skip publish");
    return true;
  }

  if (!mqttClient.connected()) {
    String clientId = String("M5CoreS3-") + String((uint32_t)ESP.getEfuseMac(), HEX);
    DEBUG_PRINTF("[MQTT] Connecting to %s:%d as %s\n", MQTT_HOST, MQTT_PORT, clientId.c_str());
    bool connected;
    if (strlen(MQTT_USER) == 0)
      connected = mqttClient.connect(clientId.c_str());
    else
      connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);

    if (!connected) {
      DEBUG_PRINTF("[MQTT] Connection failed, state=%d\n", mqttClient.state());
      return false;
    } else {
      DEBUG_PRINT("[MQTT] Connected!");
    }
  }

  float m, h; unsigned long dur;
  { std::lock_guard<std::mutex> lock(distMutex); m = lastMeasuredCm; h = lastEstimatedHeight; dur = lastDurationUs; }

  char payload[256];
  snprintf(payload, sizeof(payload), "{\"measured_cm\":%.2f,\"estimated_cm\":%.2f,\"duration_us\":%lu}", m, h, dur);
  DEBUG_PRINTF("[MQTT] Publishing to topic %s: %s\n", MQTT_TOPIC, payload);

  bool ok = mqttClient.publish(MQTT_TOPIC, payload);

  if (!ok) {
    DEBUG_PRINT("[MQTT] Publish failed!");
  } else {
    DEBUG_PRINT("[MQTT] Publish success!");
  }

  mqttClient.disconnect();
  DEBUG_PRINT("[MQTT] Disconnected");
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
  DEBUG_PRINT("[SLEEP] Periodic measurement start");

  float avg = 0.0f;
  for (int i=0;i<3;i++) {
    float m = measureDistanceCmOnce();
    DEBUG_PRINTF("[SENSOR] Raw measure #%d: %.2f cm\n", i, m);
    avg = runningAverage(m, avg, 0.5f);
    delay(30);
  }

  DEBUG_PRINTF("[SENSOR] Averaged distance: %.2f cm\n", avg);
  float est = NAN;
  if (avg > 0) {
    est = estimateHeightFromMeasured(avg);
    std::lock_guard<std::mutex> lock(distMutex);
    lastMeasuredCm = avg; lastEstimatedHeight = est;
  }

  if (ENABLE_MQTT) {
    DEBUG_PRINT("[WIFI] Connecting to WiFi for MQTT...");
    if (connectWiFiShort(6000)) {
      DEBUG_PRINT("[WIFI] Connected!");
      setupMQTT();
      bool ok = publishMQTT_measure();
      DEBUG_PRINTF("[MQTT] Publish result: %d\n", ok);
      disconnectWiFiClean();
      DEBUG_PRINT("[WIFI] Disconnected cleanly");
    } else {
      DEBUG_PRINT("[WIFI] WiFi connect failed!");
    }
  }

  DEBUG_PRINTF("[SLEEP] Going to deep sleep for %llu s\n", DEEPSLEEP_INTERVAL_S);
  wokeFromTimer = true;
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

// display task (interactive mode)
void displayTask(void *pv) {
  // initial draw
  M5.update();
  M5.Display.fillScreen(TFT_BLACK);
  drawGaugeBackground();
  prevPercent = -1;
  prevMeasured = NAN; prevEstimated = NAN; prevDuration = ULONG_MAX;
  prevCuveVide = cuveVide; prevCuvePleine = cuvePleine;

  for (;;) {
    float measured, estimated; unsigned long duration;
    {
      std::lock_guard<std::mutex> lock(distMutex);
      measured = lastMeasuredCm; estimated = lastEstimatedHeight; duration = lastDurationUs;
    }
    // left text zone update?
    bool needText = false;
    if (isnan(prevMeasured) || (measured>=0 && fabs(measured - prevMeasured) > 0.2f)) needText = true;
    if (isnan(prevEstimated) || (estimated>=0 && fabs(estimated - prevEstimated) > 0.2f)) needText = true;
    if (duration != prevDuration) needText = true;
    if (cuveVide != prevCuveVide || cuvePleine != prevCuvePleine) needText = true;

    if (needText) {
      M5.Display.fillRect(0, 0, gaugeX-8, 120, TFT_BLACK);
      M5.Display.setTextSize(3); M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setCursor(8, 8);
      if (measured > 0) M5.Display.printf("Mes: %.1f cm\n", measured); else M5.Display.printf("Mes: --\n");
      M5.Display.setTextSize(2);
      if (estimated > -0.5f) M5.Display.printf("Ht: %.1f cm\n", estimated); else M5.Display.printf("Ht: --\n");
      M5.Display.setTextSize(2);
      M5.Display.printf("Dur: %lu us\n", duration);
      M5.Display.setTextSize(2);
      M5.Display.printf("Vide: %.1f\n", cuveVide);
      M5.Display.printf("Pleine: %.1f\n", cuvePleine);

      prevMeasured = measured; prevEstimated = estimated; prevDuration = duration;
      prevCuveVide = cuveVide; prevCuvePleine = cuvePleine;
    }

    // gauge percent computation
    int percent = 0;
    if (measured > 0) {
      float denom = (cuveVide - cuvePleine);
      if (fabs(denom) < 1e-3) percent = 0;
      else {
        float ratio = (cuveVide - measured) / denom;
        ratio = constrain(ratio, 0.0f, 1.0f);
        percent = (int)round(ratio * 100.0f);
      }
    } else percent = 0;

    if (percent != prevPercent) {
      drawGaugeFill(percent);
      prevPercent = percent;
    }

    // display IP at bottom if connected
    if (WiFi.status() == WL_CONNECTED) {
      String ip = WiFi.localIP().toString();
      M5.Display.fillRect(0, M5.Display.height()-24, M5.Display.width(), 24, TFT_BLACK);
      M5.Display.setTextSize(2);
      M5.Display.setCursor(8, M5.Display.height()-22);
      M5.Display.printf("IP: %s", ip.c_str());
    } else {
      M5.Display.fillRect(0, M5.Display.height()-24, M5.Display.width(), 24, TFT_BLACK);
      M5.Display.setTextSize(2);
      M5.Display.setCursor(8, M5.Display.height()-22);
      M5.Display.print("IP: --");
    }

    // check touch to update interactiveLastTouchMs
    M5.update();
    // Note: touchscreen is via I2C; M5.Touch is available in M5CoreS3 lib.
    if (M5.Touch.getDetail().isPressed()) {
      interactiveLastTouchMs = millis();
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

void stopInteractiveModeAndSleep() {
  // stop webserver and go to deep-sleep
  server.stop();
  disconnectWiFiClean();
  // small delay
  delay(50);

  M5.Power.Axp2101.powerOff();

  // schedule deep sleep
  wokeFromTimer = false;
  esp_sleep_enable_timer_wakeup(DEEPSLEEP_INTERVAL_S * 1000000ULL);
  delay(20);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== M5CoreS3 JSN_SR04T Debug Build ===");
  Serial.printf("WiFi SSID: %s, MQTT: %s:%d (enabled=%d)\n", WIFI_SSID, MQTT_HOST, MQTT_PORT, ENABLE_MQTT);

  delay(300);

  M5.begin();
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(6, 6);
  M5.Display.println("Boot...");

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  loadCalibrations();
  computePolynomialFrom3Points();
  setupMQTT();

  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  Serial.printf("Wake reason: %d\n", (int)wakeReason);

  // Si réveil périodique
  if (wakeReason == ESP_SLEEP_WAKEUP_TIMER) {
      Serial.println("Periodic wake: quick measure -> MQTT -> sleep");
      periodicMeasureAndMaybeMQTT_andSleep();
  } else {
      // Cold boot ou réveil manuel
      Serial.println("Cold boot or manual wake -> interactive mode");
      startInteractiveMode();
  }
  
  publishMQTT_measure();
}

void loop() {
    if (interactiveMode) {
        server.handleClient();
        if ((uint32_t)(millis() - interactiveLastTouchMs) > INTERACTIVE_TIMEOUT_MS) {
            stopInteractiveModeAndSleep();
        }
    }
    delay(10);
}
