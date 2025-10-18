#include <ESPAsyncWebServer.h>
#include "web_server.h"
#include "measurement.h"
#include "mqtt.h"
#include "config.h"
#include "utils.h"
#include "config_manager.h"

#include <LittleFS.h>
#include <Arduino.h>
#include <WiFi.h>
#include <mutex>
#include <M5Unified.h>

// Serveur HTTP sur le port 80
AsyncWebServer server(80);
extern std::mutex mqttMutex;
extern volatile bool mqttBusy;

// --- Déclarations des handlers existants ---
void handleDistanceApi(AsyncWebServerRequest *request);
void handleCalibsApi(AsyncWebServerRequest *request);
void handleSaveCalib(AsyncWebServerRequest *request);
void handleClearCalib(AsyncWebServerRequest *request);
void handleSetCuve(AsyncWebServerRequest *request);
void handleSendMQTT(AsyncWebServerRequest *request);

// --- NEW: API config ---
void handleGetConfig(AsyncWebServerRequest *request);
void handlePostConfig(AsyncWebServerRequest *request, const String &body);

    // --- Initialisation du serveur ---
    void startWebServer()
{
    Serial.println("[WEB] Initialisation du serveur HTTP...");

    if (!LittleFS.begin(true))
    {
        Serial.println("[WEB][ERREUR] Échec du montage LittleFS !");
        while (true)
            delay(1000);
    }

    // Initialisation du gestionnaire de configuration
    if (!ConfigManager::instance().begin())
    {
        Serial.println("[WEB][WARN] ConfigManager n’a pas pu charger la configuration, utilisation des valeurs par défaut.");
        ConfigManager::instance().save();
    }

    // Tentative de connexion Wi-Fi
    if (!connectWiFiShort(8000))
    {
        Serial.println("[WEB][WARN] Échec de connexion Wi-Fi. Activation du mode point d’accès...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("M5CoreS3_Puits");
        Serial.print("[WEB] Point d’accès actif : ");
        Serial.println(WiFi.softAPIP());
    }
    else
    {
        Serial.print("[WEB] Connecté au Wi-Fi : ");
        Serial.println(WiFi.localIP());
    }

    // --- Routes statiques ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] GET /index.html");
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
        response->addHeader("Content-Type", "text/html; charset=utf-8");
        request->send(response); });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] GET /style.css");
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/style.css", "text/css");
        response->addHeader("Content-Type", "text/css; charset=utf-8");
        request->send(response); });

    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] GET /script.js");
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/script.js", "application/javascript");
        response->addHeader("Content-Type", "application/javascript; charset=utf-8");
        request->send(response); });

    // --- Page de configuration protégée ---
    server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        const char *adminUser = ConfigManager::instance().getAdminUser();
        const char *adminPass = ConfigManager::instance().getAdminPass();
        if (!request->authenticate(adminUser, adminPass)) {
            Serial.println("[WEB][AUTH] Authentification requise sur /config.html");
            return request->requestAuthentication();
        }
        Serial.println("[WEB] GET /config.html (auth OK)");
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/config.html", "text/html");
        response->addHeader("Content-Type", "text/html; charset=utf-8");
        request->send(response); });

    // Script JS de la page de config
    server.on("/script_config.js", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        const char *adminUser = ConfigManager::instance().getAdminUser();
        const char *adminPass = ConfigManager::instance().getAdminPass();
        if (!request->authenticate(adminUser, adminPass)) {
            Serial.println("[WEB][AUTH] Authentification requise sur /script_config.js");
            return request->requestAuthentication();
        }
        Serial.println("[WEB] GET /script_config.js (auth OK)");
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/script_config.js", "application/javascript");
        response->addHeader("Content-Type", "application/javascript; charset=utf-8");
        request->send(response); });

    // --- PING keepalive ---
    server.on("/ping", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        String page = request->arg("page");
        interactiveLastTouchMs.store(millis());
        Serial.printf("[WEB] POST /ping (%s)\n", page.c_str());
        request->send(200, "application/json; charset=utf-8", "{\"ok\":true}"); });

    // --- API existantes ---
    server.on("/distance", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] GET /distance");
        handleDistanceApi(request); });

    server.on("/calibs", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] GET /calibs");
        handleCalibsApi(request); });

    server.on("/save_calib", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] POST /save_calib");
        handleSaveCalib(request); });

    server.on("/clear_calib", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] POST /clear_calib");
        handleClearCalib(request); });

    server.on("/setCuve", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] POST /setCuve");
        handleSetCuve(request); });

    server.on("/send_mqtt", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] POST /send_mqtt");
        handleSendMQTT(request); });

    // --- NEW: Config API (protected) ---
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        Serial.println("[WEB] GET /api/config");
        handleGetConfig(request); });

    // --- Handler POST /api/config avec gestion du corps JSON ---
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                  // on ne traite rien ici, juste placeholder
              },
              NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
              {
               static String body;
               if (index == 0) {
                   body = "";
                   Serial.printf("[WEB] Début réception body JSON (%u octets)\n", total);
               }
               body += String((char *)data).substring(0, len);
               if (index + len == total) {
                   Serial.printf("[WEB] Corps JSON complet reçu (%u octets)\n", total);
                   handlePostConfig(request, body);
                   body = "";
               } });

    // --- Lancement du serveur ---
    server.begin();
    Serial.println("[WEB] Serveur Web démarré et prêt !");
}

String makeJsonDistance()
{
    float m, h;
    unsigned long dur;
    {
        std::lock_guard<std::mutex> lock(distMutex);
        m = lastMeasuredCm;
        h = lastEstimatedHeight;
        dur = lastDurationUs;
    }

    char buf[256];
    if (m < 0)
        snprintf(buf, sizeof(buf), "{\"measured_cm\":null,\"estimated_cm\":null,\"duration_us\":%lu,\"cuveVide\":%.1f,\"cuvePleine\":%.1f}", dur, cuveVide, cuvePleine);
    else
        snprintf(buf, sizeof(buf), "{\"measured_cm\":%.2f,\"estimated_cm\":%.2f,\"duration_us\":%lu,\"cuveVide\":%.1f,\"cuvePleine\":%.1f}", m, h, dur, cuveVide, cuvePleine);
    return String(buf);
}

String makeJsonCalibs()
{
    String s = "{\"calibs\":[";
    for (int i = 0; i < 3; i++)
    {
        char tmp[96];
        snprintf(tmp, sizeof(tmp), "{\"index\":%d,\"measured\":%.2f,\"height\":%.2f}", i, calib_m[i], calib_h[i]);
        s += tmp;
        if (i < 2)
            s += ",";
    }
    s += "]}";
    return s;
}

// --- Handlers API existants ---

void handleDistanceApi(AsyncWebServerRequest *request)
{
    String json = makeJsonDistance();
    request->send(200, "application/json; charset=utf-8", json);
}

void handleCalibsApi(AsyncWebServerRequest *request)
{
    String json = makeJsonCalibs();
    request->send(200, "application/json; charset=utf-8", json);
}

void handleSaveCalib(AsyncWebServerRequest *request)
{
    if (!request->hasParam("id", true) || !request->hasParam("height", true))
    {
        Serial.println("[WEB][ERR] save_calib: paramètres manquants !");
        request->send(400, "application/json; charset=utf-8", "{\"ok\":false}");
        return;
    }

    int id = request->getParam("id", true)->value().toInt();
    float height = request->getParam("height", true)->value().toFloat();

    float measured;
    {
        std::lock_guard<std::mutex> lock(distMutex);
        measured = lastMeasuredCm;
    }

    if (measured <= 0.0f)
    {
        Serial.println("[WEB][WARN] save_calib: mesure invalide (no echo)");
        request->send(200, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"no echo\"}");
        return;
    }

    Serial.printf("[WEB] Sauvegarde calibration #%d -> mesuré=%.2f, hauteur=%.2f\n", id, measured, height);
    saveCalibrationToNVS(id, measured, height);
    computePolynomialFrom3Points();
    request->send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

void handleClearCalib(AsyncWebServerRequest *request)
{
    Serial.println("[WEB] Effacement des calibrations...");
    clearCalibrations();
    computePolynomialFrom3Points();
    request->send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

void handleSetCuve(AsyncWebServerRequest *request)
{
    Serial.println("[WEB] Mise à jour des niveaux de cuve...");
    if (request->hasParam("vide", true))
        cuveVide = request->getParam("vide", true)->value().toFloat();
    if (request->hasParam("pleine", true))
        cuvePleine = request->getParam("pleine", true)->value().toFloat();

    Serial.printf("  -> Vide=%.2f, Pleine=%.2f\n", cuveVide, cuvePleine);
    saveCuveLevels();
    request->send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

void handleSendMQTT(AsyncWebServerRequest *request)
{
    {
        std::lock_guard<std::mutex> lock(mqttMutex);
        if (mqttBusy)
        {
            Serial.println("[WEB][WARN] MQTT déjà en cours d’envoi !");
            request->send(200, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"mqtt_busy\"}");
            return;
        }
        mqttBusy = true;
    }

    Serial.println("[WEB] Envoi MQTT manuel...");
    bool ok = publishMQTT_measure();

    {
        std::lock_guard<std::mutex> lock(mqttMutex);
        mqttBusy = false;
    }

    Serial.printf("[WEB] MQTT %s\n", ok ? "OK" : "ÉCHEC");
    String resp = String("{\"ok\":") + (ok ? "true" : "false") + "}";
    request->send(200, "application/json; charset=utf-8", resp);
}

// --- NEW: Config handlers ---

void handleGetConfig(AsyncWebServerRequest *request)
{
    const char *adminUser = ConfigManager::instance().getAdminUser();
    const char *adminPass = ConfigManager::instance().getAdminPass();
    if (!request->authenticate(adminUser, adminPass))
    {
        Serial.println("[WEB][AUTH] /api/config GET non autorisé");
        return request->requestAuthentication();
    }

    Serial.println("[WEB] GET /api/config (auth OK)");
    String json = ConfigManager::instance().toJsonString();
    request->send(200, "application/json; charset=utf-8", json);
}

void handlePostConfig(AsyncWebServerRequest *request, const String &body)
{
    Serial.println("[WEB] POST /api/config reçu");

    if (body.isEmpty())
    {
        Serial.println("[WEB][ERR] Corps JSON vide !");
        request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"empty body\"}");
        return;
    }

    bool okUpdate = ConfigManager::instance().updateFromJson(body);
    if (okUpdate)
    {
        Serial.println("[WEB] Configuration mise à jour (sauvegarde différée).");
        request->send(200, "application/json; charset=utf-8", "{\"ok\":true}");
    }
    else
    {
        Serial.println("[WEB][ERR] Échec de la mise à jour JSON !");
        request->send(400, "application/json; charset=utf-8", "{\"ok\":false}");
    }
}
