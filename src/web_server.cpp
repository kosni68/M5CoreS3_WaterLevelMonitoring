#include <ESPAsyncWebServer.h>
#include "web_server.h"
#include "measurement.h"
#include "mqtt.h"
#include "config.h"
#include "utils.h"

#include <LittleFS.h>
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <mutex>

// Serveur HTTP sur le port 80
AsyncWebServer server(80);
extern std::mutex mqttMutex;
extern volatile bool mqttBusy;

// --- Déclarations des handlers ---
void handleDistanceApi(AsyncWebServerRequest *request);
void handleCalibsApi(AsyncWebServerRequest *request);
void handleSaveCalib(AsyncWebServerRequest *request);
void handleClearCalib(AsyncWebServerRequest *request);
void handleSetCuve(AsyncWebServerRequest *request);
void handleSendMQTT(AsyncWebServerRequest *request);

// --- Initialisation du serveur ---
void startWebServer() {  
    Serial.println("[WEB] Initialisation du serveur...");
    
    if (!LittleFS.begin(true)) {
        DEBUG_PRINT("LittleFS mount failed!");
        while (true) delay(1000);
    }

    // Tentative de connexion WiFi rapide
    if (!connectWiFiShort(8000)) {
        Serial.println("[WEB] Échec WiFi, passage en mode point d'accès...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("M5CoreS3_Puits");
    } else {
        Serial.print("[WEB] Connecté au WiFi : ");
        Serial.println(WiFi.localIP());
    }

    // --- Fichiers statiques ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
        response->addHeader("Content-Type", "text/html; charset=utf-8");
        request->send(response);
    });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/style.css", "text/css");
        response->addHeader("Content-Type", "text/css; charset=utf-8");
        request->send(response);
    });
    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/script.js", "application/javascript");
        response->addHeader("Content-Type", "application/javascript; charset=utf-8");
        request->send(response);
    });

    // --- Routes API ---
    server.on("/distance", HTTP_GET, [](AsyncWebServerRequest *request){ handleDistanceApi(request); });
    server.on("/calibs", HTTP_GET, [](AsyncWebServerRequest *request){ handleCalibsApi(request); });
    server.on("/save_calib", HTTP_POST, [](AsyncWebServerRequest *request){ handleSaveCalib(request); });
    server.on("/clear_calib", HTTP_POST, [](AsyncWebServerRequest *request){ handleClearCalib(request); });
    server.on("/setCuve", HTTP_POST, [](AsyncWebServerRequest *request){ handleSetCuve(request); });
    server.on("/send_mqtt", HTTP_POST, [](AsyncWebServerRequest *request){ handleSendMQTT(request); });


    // --- Démarrage du serveur ---
    server.begin();
    Serial.println("[WEB] Serveur Web démarré !");
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

// --- Handlers des routes API ---

void handleDistanceApi(AsyncWebServerRequest *request) {
    String json = makeJsonDistance();
    request->send(200, "application/json", json);
}

void handleCalibsApi(AsyncWebServerRequest *request) {
    String json = makeJsonCalibs();
    request->send(200, "application/json", json);
}

void handleSaveCalib(AsyncWebServerRequest *request) {
    if (!request->hasParam("id", true) || !request->hasParam("height", true)) {
        request->send(400, "application/json", "{\"ok\":false}");
        return;
    }

    int id = request->getParam("id", true)->value().toInt();
    float height = request->getParam("height", true)->value().toFloat();

    float measured;
    {
        std::lock_guard<std::mutex> lock(distMutex);
        measured = lastMeasuredCm;
    }

    if (measured <= 0.0f) {
        request->send(200, "application/json", "{\"ok\":false,\"err\":\"no echo\"}");
        return;
    }

    saveCalibrationToNVS(id, measured, height);
    computePolynomialFrom3Points();

    request->send(200, "application/json", "{\"ok\":true}");
}

void handleClearCalib(AsyncWebServerRequest *request) {
    clearCalibrations();
    computePolynomialFrom3Points();
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleSetCuve(AsyncWebServerRequest *request) {
    if (request->hasParam("vide", true)) {
        cuveVide = request->getParam("vide", true)->value().toFloat();
    }
    if (request->hasParam("pleine", true)) {
        cuvePleine = request->getParam("pleine", true)->value().toFloat();
    }

    saveCuveLevels();
    request->send(200, "application/json", "{\"ok\":true}");
}

void handleSendMQTT(AsyncWebServerRequest *request) {
    {
        std::lock_guard<std::mutex> lock(mqttMutex);
        if (mqttBusy) {
            request->send(200, "application/json", "{\"ok\":false,\"err\":\"mqtt_busy\"}");
            return;
        }
        mqttBusy = true;
    }

    bool ok = publishMQTT_measure();

    {
        std::lock_guard<std::mutex> lock(mqttMutex);
        mqttBusy = false;
    }

    String resp = String("{\"ok\":") + (ok ? "true" : "false") + "}";
    request->send(200, "application/json", resp);
}
