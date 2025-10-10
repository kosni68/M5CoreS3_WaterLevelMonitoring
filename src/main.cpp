// M5CoreS3_JSN_SR04T_Calib_Polynomial.ino
// Mesure JSN-SR04T -> calibration 3 points -> polynôme quadratique
// Board: M5Stack CoreS3 (ESP32-S3)

#include <M5CoreS3.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <mutex>

// -------- CONFIG ----------
const char* WIFI_SSID = "TON_SSID";
const char* WIFI_PASS = "TON_MOT_DE_PASSE";

const int trigPin = 9;   // TRIG du JSN-SR04T
const int echoPin = 8;   // ECHO du JSN-SR04T (protéger si 5V)
const int SENSOR_PERIOD_MS = 200;
const int DISPLAY_PERIOD_MS = 300;

WebServer server(80);
Preferences preferences;
std::mutex distMutex;

volatile float lastMeasuredCm = -1.0f;   // distance brute mesurée (du capteur)
volatile float lastEstimatedHeight = -1.0f; // hauteur estimée (après polynôme)

// calibration arrays:
// m = valeur mesurée (x), h = hauteur réelle correspondante (y)
float calib_m[3] = {0.0f, 0.0f, 0.0f};   // mesuré (cm)
float calib_h[3] = {50.0f, 100.0f, 150.0f}; // hauteur réelle (défaut)

// polynomial coefficients: height = a*x^2 + b*x + c
double a_coef = 0.0, b_coef = 0.0, c_coef = 0.0;

// ---------- UTIL: mesure ultrason ----------
float measureDistanceCmOnce() {
  // trigger
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // attente echo en us, timeout 30000 us (~5 m)
  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL);
  if (duration == 0) return -1.0f; // timeout
  // distance en cm, v son /2 => 34300 cm/s => 0.0343 cm/us ; time travel /2 => *0.01715
  float dist = duration * 0.01715f;
  return dist;
}

// running average simple
float runningAverage(float newVal, float prevAvg, float alpha = 0.2f) {
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
    calib_m[i] = preferences.getFloat(keyM, calib_m[i]); // default 0 = not set
    calib_h[i] = preferences.getFloat(keyH, calib_h[i]); // default values kept
  }
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

// clear all calibrations (optionnel)
void clearCalibrations() {
  preferences.begin("calib", false);
  preferences.clear();
  preferences.end();
  for (int i=0;i<3;i++){ calib_m[i]=0; /* calib_h keep defaults if you want */ }
}

// ---------- POLYNOMIAL SOLVER ----------
bool computePolynomialFrom3Points() {
  // Use points (x1,y1), (x2,y2), (x3,y3) where x = measured distance, y = real height
  double x1 = calib_m[0], x2 = calib_m[1], x3 = calib_m[2];
  double y1 = calib_h[0], y2 = calib_h[1], y3 = calib_h[2];

  // require distinct x's and non-zero (we need proper calibrations)
  if (x1 == 0.0 || x2 == 0.0 || x3 == 0.0) {
    Serial.println("computePolynomial: one of measured values is 0 -> need save calibration first");
    return false;
  }
  if ((x1 == x2) || (x1 == x3) || (x2 == x3)) {
    Serial.println("computePolynomial: measured values must be distinct");
    return false;
  }

  // Solve the Vandermonde system for a,b,c.
  // Formula via Cramer's rule (direct closed-form) as used earlier.
  double denom = (x1 - x2)*(x1 - x3)*(x2 - x3);
  if (denom == 0.0) return false;

  a_coef = ( x3*(y2 - y1) + x2*(y1 - y3) + x1*(y3 - y2) ) / denom;
  b_coef = ( x3*x3*(y1 - y2) + x2*x2*(y3 - y1) + x1*x1*(y2 - y3) ) / denom;
  c_coef = ( x2*x3*(x2 - x3)*y1 + x3*x1*(x3 - x1)*y2 + x1*x2*(x1 - x2)*y3 ) / denom;

  Serial.printf("Poly computed: height = %.8f*x^2 + %.8f*x + %.8f\n", a_coef, b_coef, c_coef);
  return true;
}

float estimateHeightFromMeasured(float x) {
  // if coefficients not set, just return -1
  return (float)(a_coef*x*x + b_coef*x + c_coef);
}

// ---------- WEB HANDLERS ----------
String makeJsonDistance() {
  float m, h;
  {
    std::lock_guard<std::mutex> lock(distMutex);
    m = lastMeasuredCm;
    h = lastEstimatedHeight;
  }
  // manual JSON small (to avoid heavy libs)
  char buf[128];
  if (m < 0) sprintf(buf, "{\"measured_cm\":null,\"estimated_cm\":null}");
  else sprintf(buf, "{\"measured_cm\":%.2f,\"estimated_cm\":%.2f}", m, h);
  return String(buf);
}

String makeJsonCalibs() {
  // return arrays of objects: [{index:0, measured:.., height:..}, ...]
  String s = "[";
  for (int i=0;i<3;i++){
    char tmp[128];
    sprintf(tmp, "{\"index\":%d,\"measured\":%.2f,\"height\":%.2f}", i, calib_m[i], calib_h[i]);
    s += String(tmp);
    if (i<2) s += ",";
  }
  s += "]";
  return s;
}

void handleRoot(){
  // HTML page: shows measured, estimated and calibration UI
  String page = R"rawliteral(
<!doctype html>
<html>
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5 Puits - Calib</title>
<style>body{font-family:sans-serif;padding:10px} .big{font-size:1.5rem;font-weight:700} button{margin:6px}</style>
</head>
<body>
<h2>M5 Puits - Calibration</h2>
<div>Mesuré (capteur): <span id="meas" class="big">--</span> cm</div>
<div>Hauteur estimée: <span id="est" class="big">--</span> cm</div>
<hr>
<h3>Points de calibration (saisir hauteur réelle puis "Sauver current")</h3>
<div id="calibs"></div>
<button onclick="clearCalib()">Effacer calibrations NVS</button>
<script>
function refresh(){
  fetch('/distance').then(r=>r.json()).then(j=>{
    if (j.measured_cm === null) {
      document.getElementById('meas').innerText = '--';
      document.getElementById('est').innerText = '--';
    } else {
      document.getElementById('meas').innerText = j.measured_cm.toFixed(1);
      document.getElementById('est').innerText = j.estimated_cm.toFixed(1);
    }
  });
  fetch('/calibs').then(r=>r.text()).then(t=>{
    let arr = JSON.parse(t);
    let html='';
    arr.forEach(function(c){
      html += 'C'+(c.index+1)+': Mesuré = <b>'+ (c.measured>0 ? c.measured.toFixed(1) : '--') + ' cm</b> ' +
              'Hauteur réelle (cm): <input id=\"h'+c.index+'\" value=\"'+ (c.height?c.height:100) +'\"> ' +
              '<button onclick=\"save('+c.index+')\">Sauver current</button><br>';
    });
    document.getElementById('calibs').innerHTML = html;
  });
}
function save(idx){
  const val = document.getElementById('h'+idx).value;
  // POST to save_calib with id and height (height = real height)
  fetch('/save_calib?id='+idx+'&height='+encodeURIComponent(val), { method:'POST' })
    .then(r=>r.json()).then(j=>{
      if (j.ok) { alert('Saved'); refresh(); } else alert('Error');
    });
}
function clearCalib(){
  fetch('/clear_calib', { method:'POST'}).then(r=>r.json()).then(j=>{
    alert('Calibrations cleared'); refresh();
  });
}
setInterval(refresh,1200);
refresh();
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", page);
}

void handleDistanceApi(){
  String j = makeJsonDistance();
  server.send(200, "application/json", j);
}

void handleCalibsApi(){
  String j = makeJsonCalibs();
  server.send(200, "application/json", j);
}

// Save calibration: expects args id (0..2) and height (float, cm)
// will read current measured distance, and save (measured, height)
void handleSaveCalib(){
  if (!server.hasArg("id") || !server.hasArg("height")) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"missing args\"}");
    return;
  }
  int id = server.arg("id").toInt();
  float height = server.arg("height").toFloat();

  float measured;
  {
    std::lock_guard<std::mutex> lock(distMutex);
    measured = lastMeasuredCm;
  }
  if (measured <= 0.0f) {
    server.send(200, "application/json", "{\"ok\":false,\"err\":\"no measured value (no echo)\"}");
    return;
  }

  // save
  saveCalibrationToNVS(id, measured, height);
  bool ok = computePolynomialFrom3Points();
  if (!ok) {
    server.send(200, "application/json", "{\"ok\":false,\"err\":\"failed compute polynomial\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// clear calibrations stored in NVS (and memory)
void handleClearCalib(){
  clearCalibrations();
  // recompute (likely fail until 3 points saved)
  computePolynomialFrom3Points();
  server.send(200, "application/json", "{\"ok\":true}");
}

// ---------- TASKS ----------
void sensorTask(void *pv){
  float avg = 0.0f;
  for(;;){
    float m = measureDistanceCmOnce();
    avg = runningAverage(m, avg, 0.25f);
    float estimated = -1.0f;
    if (avg > 0.0f) estimated = estimateHeightFromMeasured(avg);
    {
      std::lock_guard<std::mutex> lock(distMutex);
      lastMeasuredCm = avg;
      lastEstimatedHeight = estimated;
    }
    vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
  }
}

void displayTask(void *pv) {
  for(;;){
    float m, est;
    {
      std::lock_guard<std::mutex> lock(distMutex);
      m = lastMeasuredCm;
      est = lastEstimatedHeight;
    }
    M5.update();
    M5.Display.fillRect(0,0,M5.Display.width(),80,TFT_BLACK);
    M5.Display.setCursor(6,6);
    M5.Display.setTextSize(3);
    if (m>0) M5.Display.printf("Mes: %.1f cm\n", m);
    else M5.Display.printf("Mes: -- cm\n");
    M5.Display.setTextSize(2);
    if (est > -0.5f) M5.Display.printf("Ht: %.1f cm\n", est);
    else M5.Display.printf("Ht: --\n");
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}

// ---------- SETUP & LOOP ----------
void setup(){
  Serial.begin(115200);
  delay(800);
  M5.begin();
  M5.Display.println("Init...");

  // pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // load saved calibrations and try compute polynomial
  loadCalibrations();
  computePolynomialFrom3Points();

  // Wifi connect (if fails goes to AP mode)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  M5.Display.println("WiFi...");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());
    M5.Display.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("M5CoreS3_Puits");
    M5.Display.println("AP: M5CoreS3_Puits");
  }

  // web routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/distance", HTTP_GET, handleDistanceApi);
  server.on("/calibs", HTTP_GET, handleCalibsApi);
  server.on("/save_calib", HTTP_POST, handleSaveCalib);
  server.on("/clear_calib", HTTP_POST, handleClearCalib);
  server.begin();

  // create tasks
  xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(displayTask, "displayTask", 4096, NULL, 1, NULL, 1);
}

void loop(){
  server.handleClient();
  delay(10);
}
