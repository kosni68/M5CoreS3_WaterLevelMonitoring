#include <WebServer.h>
#include "web_server.h"
#include "measurement.h"
#include "mqtt.h"
#include "config.h"
#include "utils.h"

extern WebServer server;
extern volatile bool mqttBusy;

void startWebServer() {  
    
    if (!connectWiFiShort(8000)) {
    // start AP fallback
    WiFi.mode(WIFI_AP);
    WiFi.softAP("M5CoreS3_Puits");
    }

    server.on("/", HTTP_GET, handleRoot);
    server.on("/distance", HTTP_GET, handleDistanceApi);
    server.on("/calibs", HTTP_GET, handleCalibsApi);
    server.on("/save_calib", HTTP_POST, handleSaveCalib);
    server.on("/clear_calib", HTTP_POST, handleClearCalib);
    server.on("/setCuve", HTTP_POST, handleSetCuve);
    server.on("/send_mqtt", HTTP_POST, handleSendMQTT);
    server.begin();
}

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
<button onclick="sendMQTT()">Envoyer MQTT</button>

<script>
function sendMQTT(){
  fetch('/send_mqtt', {method:'POST'})
    .then(r=>r.json())
    .then(j=>{
      if(j.ok) alert('MQTT envoyé !');
      else alert('Échec de l’envoi MQTT.');
    })
    .catch(e=>alert('Erreur de requête MQTT: '+e));
}

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

void handleSendMQTT() {
  // protect against concurrent publishes
  {
    std::lock_guard<std::mutex> lock(mqttMutex);
    if (mqttBusy) {
      server.send(200, "application/json", "{\"ok\":false,\"err\":\"mqtt_busy\"}");
      return;
    }
  }
  bool ok = publishMQTT_measure();
  String resp = String("{\"ok\":") + (ok ? "true" : "false") + "}";
  server.send(200, "application/json", resp);
}

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