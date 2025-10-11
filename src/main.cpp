// M5CoreS3_JSN_SR04T_Calib_Polynomial_Chart_Brute.ino
#include <M5CoreS3.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <mutex>

// -------- CONFIG ----------
const char* WIFI_SSID = "Freebox-22A0D2";
const char* WIFI_PASS = "NicoCindy22";

const int trigPin = 9;   
const int echoPin = 8;   
const int SENSOR_PERIOD_MS = 200;
const int DISPLAY_PERIOD_MS = 300;

WebServer server(80);
Preferences preferences;
std::mutex distMutex;

volatile float lastMeasuredCm = -1.0f;   
volatile float lastEstimatedHeight = -1.0f; 
volatile unsigned long lastDurationUs = 0; // valeur brute du capteur

float calib_m[3] = {0.0f, 0.0f, 0.0f};   
float calib_h[3] = {50.0f, 100.0f, 150.0f}; 

double a_coef = 0.0, b_coef = 0.0, c_coef = 0.0;

// ---------- UTIL ----------
float measureDistanceCmOnce() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(5);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(20);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL);
  lastDurationUs = duration; // stocke la valeur brute
  if (duration == 0) return -1.0f; 
  return duration * 0.01715f;
}

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
    calib_m[i] = preferences.getFloat(keyM, calib_m[i]);
    calib_h[i] = preferences.getFloat(keyH, calib_h[i]);
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

void clearCalibrations() {
  preferences.begin("calib", false);
  preferences.clear();
  preferences.end();
  for (int i=0;i<3;i++){ calib_m[i]=0; }
}

// ---------- POLYNOMIAL ----------
bool computePolynomialFrom3Points() {
  double x1 = calib_m[0], x2 = calib_m[1], x3 = calib_m[2];
  double y1 = calib_h[0], y2 = calib_h[1], y3 = calib_h[2];

  if (x1==0.0 || x2==0.0 || x3==0.0 || x1==x2 || x1==x3 || x2==x3) return false;

  double denom = (x1 - x2)*(x1 - x3)*(x2 - x3);
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
  char buf[128];
  if (m < 0) sprintf(buf, "{\"measured_cm\":null,\"estimated_cm\":null,\"duration_us\":null}");
  else sprintf(buf, "{\"measured_cm\":%.2f,\"estimated_cm\":%.2f,\"duration_us\":%lu}", m, h, dur);
  return String(buf);
}

String makeJsonCalibs() {
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

// ---------- HTML + Chart.js ----------
void handleRoot(){
  String page = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5 Puits - Chart Brute</title>
<style>
body{font-family:sans-serif;padding:10px} 
.big{font-size:1.5rem;font-weight:700} 
button{margin:6px} 
canvas{max-width:100%;height:150px;} /* hauteur réduite */
</style>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
<h2>M5 Puits - Calibration & Chart</h2>
<div>Mesuré (cm): <span id="meas" class="big">--</span></div>
<div>Estimé (cm): <span id="est" class="big">--</span></div>
<div>Brut (µs): <span id="dur" class="big">--</span></div>
<hr>
<h3>Graphique temps réel</h3>
<canvas id="chart" width="400" height="150"></canvas>
<hr>
<h3>Points de calibration</h3>
<div id="calibs"></div>
<button onclick="clearCalib()">Effacer calibrations NVS</button>

<script>
let durData=[], measData=[], estData=[], labels=[];
const ctx=document.getElementById('chart').getContext('2d');
const chart=new Chart(ctx,{
  type:'line',
  data:{
    labels:labels,
    datasets:[
      {label:'Mesuré cm',data:measData,borderColor:'blue',fill:false,yAxisID:'y1'},
      {label:'Estimé cm',data:estData,borderColor:'green',fill:false,yAxisID:'y1'},
      {label:'Durée µs',data:durData,borderColor:'red',fill:false,yAxisID:'y2'}
    ]
  },
  options:{
    responsive:true,
    scales:{
      y1:{type:'linear',position:'left'},
      y2:{type:'linear',position:'right'}
    }
  }
});

function refreshDistance(){
  fetch('/distance').then(r=>r.json()).then(j=>{
    document.getElementById('meas').innerText=j.measured_cm!==null?j.measured_cm.toFixed(1):'--';
    document.getElementById('est').innerText=j.estimated_cm!==null?j.estimated_cm.toFixed(1):'--';
    document.getElementById('dur').innerText=j.duration_us!==null?j.duration_us:'--';

    const t=(new Date()).toLocaleTimeString();
    labels.push(t); if(labels.length>50){labels.shift();measData.shift();estData.shift();durData.shift();}
    measData.push(j.measured_cm||0);
    estData.push(j.estimated_cm||0);
    durData.push(j.duration_us||0);
    chart.update();
  });
}

function refreshCalibs(){
  fetch('/calibs').then(r=>r.text()).then(t=>{
    let arr = JSON.parse(t);
    let html='';
    arr.forEach(function(c){
      html += 'C'+(c.index+1)+': Mesuré = <b>'+ (c.measured>0 ? c.measured.toFixed(1) : '--') + 
              ' cm</b> Hauteur réelle (cm): <input id="h'+c.index+'" value="'+ c.height +'"> ' +
              '<button onclick="save('+c.index+')">Sauver current</button><br>';
    });
    document.getElementById('calibs').innerHTML=html;
  });
}

function save(idx){
  const val=document.getElementById('h'+idx).value;
  fetch('/save_calib?id='+idx+'&height='+encodeURIComponent(val),{method:'POST'})
    .then(r=>r.json()).then(j=>{if(j.ok){alert('Saved');refreshCalibs();}else alert('Error');});
}

function clearCalib(){
  fetch('/clear_calib',{method:'POST'}).then(r=>r.json()).then(j=>{alert('Calibrations cleared');refreshCalibs();});
}

setInterval(refreshDistance,800);
setInterval(refreshCalibs,5000);
refreshDistance(); refreshCalibs();
</script>
</body>
</html>
)rawliteral";
  server.send(200,"text/html",page);
}

// ---------- ROUTES ----------
void handleDistanceApi(){ server.send(200,"application/json",makeJsonDistance()); }
void handleCalibsApi(){ server.send(200,"application/json",makeJsonCalibs()); }

void handleSaveCalib(){
  if(!server.hasArg("id")||!server.hasArg("height")){server.send(400,"application/json","{\"ok\":false}");return;}
  int id=server.arg("id").toInt();
  float h=server.arg("height").toFloat();
  float m; { std::lock_guard<std::mutex> lock(distMutex); m=lastMeasuredCm; }
  if(m<=0.0f){ server.send(200,"application/json","{\"ok\":false}"); return; }
  saveCalibrationToNVS(id,m,h);
  computePolynomialFrom3Points();
  server.send(200,"application/json","{\"ok\":true}");
}

void handleClearCalib(){ clearCalibrations(); computePolynomialFrom3Points(); server.send(200,"application/json","{\"ok\":true}"); }

// ---------- TASKS ----------
void sensorTask(void *pv){
  float avg=0.0f;
  for(;;){
    float m=measureDistanceCmOnce();
    avg=runningAverage(m,avg,0.25f);
    float est=-1.0f;
    if(avg>0.0f) est=estimateHeightFromMeasured(avg);
    { std::lock_guard<std::mutex> lock(distMutex); lastMeasuredCm=avg; lastEstimatedHeight=est; }
    vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
  }
}

void displayTask(void *pv){
  for(;;){
    float m, est; { std::lock_guard<std::mutex> lock(distMutex); m=lastMeasuredCm; est=lastEstimatedHeight; }
    M5.update();
    M5.Display.fillRect(0,0,M5.Display.width(),80,TFT_BLACK);
    M5.Display.setCursor(6,6); M5.Display.setTextSize(3);
    if(m>0) M5.Display.printf("Mes: %.1f cm\n", m); else M5.Display.printf("Mes: -- cm\n");
    M5.Display.setTextSize(2);
    if(est>-0.5f) M5.Display.printf("Ht: %.1f cm\n", est); else M5.Display.printf("Ht: --\n");
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}

// ---------- SETUP & LOOP ----------
void setup(){
  Serial.begin(115200);
  delay(800);
  M5.begin(); M5.Display.println("Init...");
  pinMode(trigPin,OUTPUT); pinMode(echoPin,INPUT);
  loadCalibrations(); computePolynomialFrom3Points();

  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID,WIFI_PASS);
  M5.Display.println("WiFi...");
  int tries=0;
  while(WiFi.status()!=WL_CONNECTED && tries<30){ delay(500); Serial.print("."); tries++; }
  if(WiFi.status()==WL_CONNECTED){ M5.Display.printf("IP: %s\n",WiFi.localIP().toString().c_str()); }
  else{ WiFi.mode(WIFI_AP); WiFi.softAP("M5CoreS3_Puits"); M5.Display.println("AP: M5CoreS3_Puits"); }

  server.on("/",HTTP_GET,handleRoot);
  server.on("/distance",HTTP_GET,handleDistanceApi);
  server.on("/calibs",HTTP_GET,handleCalibsApi);
  server.on("/save_calib",HTTP_POST,handleSaveCalib);
  server.on("/clear_calib",HTTP_POST,handleClearCalib);
  server.begin();

  xTaskCreatePinnedToCore(sensorTask,"sensorTask",4096,NULL,2,NULL,1);
  xTaskCreatePinnedToCore(displayTask,"displayTask",4096,NULL,1,NULL,1);
}

void loop(){ server.handleClient(); delay(10); }
