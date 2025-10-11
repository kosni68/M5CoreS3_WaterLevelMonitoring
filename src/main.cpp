// M5CoreS3_JSN_SR04T_Final_With_Chart_And_Gauge.ino
// Tout-en-un : JSN-SR04T, calibration 3 points (polynôme), NVS,
// interface web avec Chart.js (durée µs, mesuré cm, estimé cm),
// jauge graphique écran (180 px) avec % et rafraîchissement par zones.

#include <M5CoreS3.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <mutex>

// -------- CONFIG ----------
const char* WIFI_SSID = "Freebox-22A0D2";
const char* WIFI_PASS = "NicoCindy22";

const int trigPin = 9;   // TRIG du JSN-SR04T
const int echoPin = 8;   // ECHO du JSN-SR04T (PROTÈGE si 5V)
const int SENSOR_PERIOD_MS = 200;
const int DISPLAY_PERIOD_MS = 300; // fréquence d'update écran (mais on ne redraw que si change)

WebServer server(80);
Preferences preferences;
std::mutex distMutex;

// ---------- mesures et état ----------
volatile float lastMeasuredCm = -1.0f;     // valeur moyenne lissée (cm)
volatile float lastEstimatedHeight = -1.0f; // hauteur estimée via polynôme
volatile unsigned long lastDurationUs = 0; // pulseIn raw value (µs)

// calibrations (m = mesuré, h = hauteur réelle)
float calib_m[3] = {0.0f, 0.0f, 0.0f};
float calib_h[3] = {50.0f, 100.0f, 150.0f};

// cuve vide / pleine (mesures distance correspondantes)
float cuveVide = 123.0f;
float cuvePleine = 42.0f;

// polynôme coefficients (height = a*x^2 + b*x + c)
double a_coef = 0.0, b_coef = 0.0, c_coef = 0.0;

// ---------- UTIL: mesure ultrason ----------
float measureDistanceCmOnce() {
  // Envoi du trigger (valeurs légèrement augmentées pour JSN robustesse)
  digitalWrite(trigPin, LOW);
  delayMicroseconds(5);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(20);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL); // timeout 30ms ~ 5m
  lastDurationUs = duration;
  if (duration == 0) return -1.0f;
  // conversion durée(us) -> cm : duration * 0.01715
  return duration * 0.01715f;
}

float runningAverage(float newVal, float prevAvg, float alpha = 0.25f) {
  if (newVal < 0) return prevAvg;
  return prevAvg * (1.0f - alpha) + newVal * alpha;
}

// ---------- PERSISTENCE ----------
void loadCalibrations() {
  preferences.begin("calib", true);
  for (int i = 0; i < 3; ++i) {
    char keyM[8], keyH[8];
    sprintf(keyM, "m%d", i);
    sprintf(keyH, "h%d", i);
    calib_m[i] = preferences.getFloat(keyM, calib_m[i]);
    calib_h[i] = preferences.getFloat(keyH, calib_h[i]);
  }
  cuveVide = preferences.getFloat("cuveVide", cuveVide);
  cuvePleine = preferences.getFloat("cuvePleine", cuvePleine);
  preferences.end();
}

void saveCalibrationToNVS(int idx, float measured, float height) {
  if (idx < 0 || idx > 2) return;
  preferences.begin("calib", false);
  char keyM[8], keyH[8];
  sprintf(keyM, "m%d", idx);
  sprintf(keyH, "h%d", idx);
  preferences.putFloat(keyM, measured);
  preferences.putFloat(keyH, height);
  preferences.end();
  calib_m[idx] = measured;
  calib_h[idx] = height;
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
  for (int i = 0; i < 3; ++i) {
    calib_m[i] = 0;
    // calib_h keep defaults (or 0) - here keep defaults
  }
}

// ---------- POLYNOMIAL SOLVER ----------
bool computePolynomialFrom3Points() {
  double x1 = calib_m[0], x2 = calib_m[1], x3 = calib_m[2];
  double y1 = calib_h[0], y2 = calib_h[1], y3 = calib_h[2];

  // check valid distinct measured points
  if (x1 == 0.0 || x2 == 0.0 || x3 == 0.0) return false;
  if ((x1 == x2) || (x1 == x3) || (x2 == x3)) return false;

  double denom = (x1 - x2)*(x1 - x3)*(x2 - x3);
  if (denom == 0.0) return false;

  a_coef = ( x3*(y2 - y1) + x2*(y1 - y3) + x1*(y3 - y2) ) / denom;
  b_coef = ( x3*x3*(y1 - y2) + x2*x2*(y3 - y1) + x1*x1*(y2 - y3) ) / denom;
  c_coef = ( x2*x3*(x2 - x3)*y1 + x3*x1*(x3 - x1)*y2 + x1*x2*(x1 - x2)*y3 ) / denom;
  return true;
}

float estimateHeightFromMeasured(float x) {
  return (float)(a_coef*x*x + b_coef*x + c_coef);
}

// ---------- WEB HANDLERS ----------
String makeJsonDistance() {
  float m, h; unsigned long dur;
  {
    std::lock_guard<std::mutex> lock(distMutex);
    m = lastMeasuredCm;
    h = lastEstimatedHeight;
    dur = lastDurationUs;
  }
  // include cuveVide and cuvePleine as well
  char buf[256];
  if (m < 0)
    snprintf(buf, sizeof(buf), "{\"measured_cm\":null,\"estimated_cm\":null,\"duration_us\":%lu,\"cuveVide\":%.1f,\"cuvePleine\":%.1f}", dur, cuveVide, cuvePleine);
  else
    snprintf(buf, sizeof(buf), "{\"measured_cm\":%.2f,\"estimated_cm\":%.2f,\"duration_us\":%lu,\"cuveVide\":%.1f,\"cuvePleine\":%.1f}", m, h, dur, cuveVide, cuvePleine);
  return String(buf);
}

String makeJsonCalibs() {
  String s = "{\"calibs\":[";
  for (int i=0;i<3;i++){
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "{\"index\":%d,\"measured\":%.2f,\"height\":%.2f}", i, calib_m[i], calib_h[i]);
    s += String(tmp);
    if (i<2) s += ",";
  }
  s += "]}";
  return s;
}

// root page: Chart.js + calibration + cuve inputs
void handleRoot() {
  String page = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5 Puits - Complete</title>
<style>body{font-family:sans-serif;padding:10px} .big{font-size:1.2rem;font-weight:700} input{width:70px}</style>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
<h2>M5 Puits - Dashboard</h2>
<div>Mesuré: <span id="meas">--</span> cm &nbsp; Estimé: <span id="est">--</span> cm &nbsp; Brut: <span id="dur">--</span> µs</div>
<hr>
<canvas id="chart" width="400" height="150"></canvas>
<hr>
<h3>Calibration 3 points</h3>
<div id="calibs"></div>
<h3>Cuve levels</h3>
<div>Vide: <input id="v" value="123"> cm &nbsp; Pleine: <input id="p" value="42"> cm <button onclick="saveCuve()">Sauvegarder</button></div>
<button onclick="clearCalib()">Effacer calibrations</button>

<script>
let labels=[], measData=[], estData=[], durData=[];
const ctx=document.getElementById('chart').getContext('2d');
const chart=new Chart(ctx,{
  type:'line', data:{labels:labels, datasets:[
    {label:'Mesuré (cm)', data:measData, borderColor:'blue', fill:false, yAxisID:'y1'},
    {label:'Estimé (cm)', data:estData, borderColor:'green', fill:false, yAxisID:'y1'},
    {label:'Durée (µs)', data:durData, borderColor:'red', fill:false, yAxisID:'y2'}
  ]},
  options:{responsive:true, scales:{ y1:{type:'linear',position:'left'}, y2:{type:'linear',position:'right'} }}
});

function refreshDistance(){
  fetch('/distance').then(r=>r.json()).then(j=>{
    document.getElementById('meas').innerText = j.measured_cm!==null?j.measured_cm.toFixed(1):'--';
    document.getElementById('est').innerText = j.estimated_cm!==null?j.estimated_cm.toFixed(1):'--';
    document.getElementById('dur').innerText = j.duration_us!==null?j.duration_us:'--';
    const t=new Date().toLocaleTimeString();
    labels.push(t);
    if(labels.length>60){labels.shift();measData.shift();estData.shift();durData.shift();}
    measData.push(j.measured_cm||0);
    estData.push(j.estimated_cm||0);
    durData.push(j.duration_us||0);
    chart.update();
  });
}

function refreshCalibs(){
  fetch('/calibs').then(r=>r.json()).then(j=>{
    let html='';
    j.calibs.forEach(function(c){
      html += 'C'+(c.index+1)+': Mesuré = <b>'+ (c.measured>0 ? c.measured.toFixed(1) : '--') + 
              ' cm</b> Hauteur (cm): <input id="h'+c.index+'" value="'+c.height+'"> ' +
              '<button onclick="save('+c.index+')">Sauver</button><br>';
    });
    document.getElementById('calibs').innerHTML = html;
  });
}

function save(idx){
  const val = document.getElementById('h'+idx).value;
  fetch('/save_calib?id='+idx+'&height='+encodeURIComponent(val), { method:'POST' })
    .then(r=>r.json()).then(j=>{ if(j.ok){ alert('Saved'); refreshCalibs(); } else alert('Error'); });
}

function saveCuve(){
  const v=document.getElementById('v').value;
  const p=document.getElementById('p').value;
  fetch('/setCuve?vide='+encodeURIComponent(v)+'&pleine='+encodeURIComponent(p), { method:'POST' })
    .then(r=>r.json()).then(j=>{ if(j.ok) alert('Saved cuve'); });
}

function clearCalib(){
  fetch('/clear_calib', { method:'POST' }).then(r=>r.json()).then(j=>{ alert('Cleared'); refreshCalibs(); });
}

setInterval(refreshDistance,800);
setInterval(refreshCalibs,5000);
refreshDistance(); refreshCalibs();
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html", page);
}

// API routes
void handleDistanceApi() { server.send(200, "application/json", makeJsonDistance()); }
void handleCalibsApi() { server.send(200, "application/json", makeJsonCalibs()); }

void handleSaveCalib() {
  if (!server.hasArg("id") || !server.hasArg("height")) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  int id = server.arg("id").toInt();
  float height = server.arg("height").toFloat();
  float measured;
  { std::lock_guard<std::mutex> lock(distMutex); measured = lastMeasuredCm; }
  if (measured <= 0.0f) {
    server.send(200, "application/json", "{\"ok\":false,\"err\":\"no echo\"}");
    return;
  }
  saveCalibrationToNVS(id, measured, height);
  computePolynomialFrom3Points();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleClearCalib() {
  clearCalibrations();
  computePolynomialFrom3Points();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleSetCuve() {
  if (server.hasArg("vide")) cuveVide = server.arg("vide").toFloat();
  if (server.hasArg("pleine")) cuvePleine = server.arg("pleine").toFloat();
  saveCuveLevels();
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------- DISPLAY: rafraîchissement par zones ----------
/*
  Layout:
   - Left area: numeric/text (x=8,y=8)
   - Right area: gauge rectangle (x=250,y=30,width=60,height=180)
*/
const int gaugeX = 250;
const int gaugeY = 30;
const int gaugeW = 60;
const int gaugeH = 180;

// previous values to detect change
float prevMeasured = -9999.0f;
float prevEstimated = -9999.0f;
unsigned long prevDuration = 0xFFFFFFFF;
float prevCuveVide = -9999.0f;
float prevCuvePleine = -9999.0f;
int prevPercent = -1;

void drawGaugeBackground() {
  M5.Display.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, TFT_WHITE);
  // draw markers for full and empty
  M5.Display.setTextSize(1);
  M5.Display.setCursor(gaugeX + gaugeW + 6, gaugeY - 4);
  M5.Display.print("Pleine");
  M5.Display.setCursor(gaugeX + gaugeW + 6, gaugeY + gaugeH - 8);
  M5.Display.print("Vide");
}

void drawGaugeFill(int percent) {
  // percent 0..100 fill from bottom
  M5.Display.fillRect(gaugeX+1, gaugeY+1, gaugeW-2, gaugeH-2, TFT_BLACK); // clear inner
  int fillH = (int)((percent / 100.0f) * (gaugeH-4));
  if (fillH > 0) {
    int yFill = gaugeY + gaugeH - 2 - fillH;
    M5.Display.fillRect(gaugeX+2, yFill, gaugeW-4, fillH, TFT_BLUE);
  }
  // draw border again
  M5.Display.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, TFT_WHITE);
  // percentage text inside gauge (centered)
  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  int tx = gaugeX + (gaugeW/2) - 10;
  int ty = gaugeY + (gaugeH/2) - 8;
  M5.Display.setTextSize(2);
  M5.Display.setCursor(tx, ty);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.print(buf);
}

// ---------- TASKS ----------
void sensorTask(void *pv) {
  float avg = 0.0f;
  for (;;) {
    float m = measureDistanceCmOnce();
    avg = runningAverage(m, avg, 0.25f);
    float est = -1.0f;
    if (avg > 0.0f) est = estimateHeightFromMeasured(avg);
    {
      std::lock_guard<std::mutex> lock(distMutex);
      lastMeasuredCm = avg;
      lastEstimatedHeight = est;
    }
    vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
  }
}

void displayTask(void *pv) {
  // initial draw
  M5.update();
  M5.Display.fillScreen(TFT_BLACK);
  drawGaugeBackground();

  for (;;) {
    float measured, estimated;
    unsigned long duration;
    {
      std::lock_guard<std::mutex> lock(distMutex);
      measured = lastMeasuredCm;
      estimated = lastEstimatedHeight;
      duration = lastDurationUs;
    }

    // --- zone 1: left numeric (only redraw when value changes noticeably) ---
    bool needTextUpdate = false;
    if ( (measured < 0 && prevMeasured >= 0) ||
         (measured >=0 && fabs(measured - prevMeasured) > 0.2f) ||
         (estimated < 0 && prevEstimated >= 0) ||
         (estimated >=0 && fabs(estimated - prevEstimated) > 0.2f) ||
         (duration != prevDuration) ||
         (cuveVide != prevCuveVide) || (cuvePleine != prevCuvePleine)
       ) {
      needTextUpdate = true;
    }

    if (needTextUpdate) {
      // clear left area
      M5.Display.fillRect(0, 0, gaugeX - 8, 120, TFT_BLACK);
      M5.Display.setTextSize(3);
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Display.setCursor(8, 8);
      if (measured > 0) M5.Display.printf("Mes: %.1f cm\n", measured);
      else M5.Display.printf("Mes: --\n");
      M5.Display.setTextSize(2);
      if (estimated > -0.5f) M5.Display.printf("Ht: %.1f cm\n", estimated);
      else M5.Display.printf("Ht: --\n");
      M5.Display.setTextSize(2);
      M5.Display.printf("Dur: %lu us\n", duration);
      M5.Display.setTextSize(2);
      M5.Display.printf("Vide: %.1f\n", cuveVide);
      M5.Display.printf("Pleine: %.1f\n", cuvePleine);

      prevMeasured = measured;
      prevEstimated = estimated;
      prevDuration = duration;
      prevCuveVide = cuveVide;
      prevCuvePleine = cuvePleine;
    }

    // --- zone 2: gauge (redraw only if percent changed) ---
    int percent = 0;
    if (measured > 0) {
      // fillRatio = (cuveVide - measured) / (cuveVide - cuvePleine)
      float denom = (cuveVide - cuvePleine);
      if (fabs(denom) < 1e-3) percent = 0;
      else {
        float ratio = (cuveVide - measured) / denom;
        ratio = constrain(ratio, 0.0f, 1.0f);
        percent = (int)round(ratio * 100.0f);
      }
    } else {
      percent = 0;
    }

    if (percent != prevPercent) {
      drawGaugeFill(percent);
      prevPercent = percent;
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}

// ---------- SETUP & LOOP ----------
void setup() {
  Serial.begin(115200);
  delay(800);
  M5.begin();
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(6, 6);
  M5.Display.println("Init...");

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  loadCalibrations();
  computePolynomialFrom3Points();

  // Wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  M5.Display.setCursor(6, 40);
  M5.Display.println("WiFi...");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    M5.Display.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("M5CoreS3_Puits");
    M5.Display.println("AP: M5CoreS3_Puits");
  }

  // Web routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/distance", HTTP_GET, handleDistanceApi);
  server.on("/calibs", HTTP_GET, handleCalibsApi);
  server.on("/save_calib", HTTP_POST, handleSaveCalib);
  server.on("/clear_calib", HTTP_POST, handleClearCalib);
  server.on("/setCuve", HTTP_POST, handleSetCuve);
  server.begin();

  // Create tasks pinned to core 1 to keep core 0 for WiFi/stack (common pattern)
  xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(displayTask, "displayTask", 4096, NULL, 1, NULL, 1);
}

void loop() {
  server.handleClient();
  delay(10);
}
