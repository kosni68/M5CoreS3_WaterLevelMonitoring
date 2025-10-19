#include <WiFi.h>
#include <PubSubClient.h>
#include "mqtt.h"
#include "measurement.h"
#include "config.h"
#include "config_manager.h"
#include <atomic>

// ---------- MQTT client ----------
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
std::atomic<bool> mqttBusy{false};

void setupMQTT()
{
  const auto cfg = ConfigManager::instance().getConfig();
  mqttClient.setServer(cfg.mqtt_host, cfg.mqtt_port);
}

bool publishMQTT_measure()
{
  // Vérifie et réserve le flag atomiquement
  bool expected = false;
  if (!mqttBusy.compare_exchange_strong(expected, true))
  {
    DEBUG_PRINT("[MQTT] Busy - skipping publish");
    return false;
  }

  bool ok = false;
  const auto cfg = ConfigManager::instance().getConfig();

  // --- Vérifie si MQTT est activé ---
  if (!cfg.mqtt_enabled)
  {
    DEBUG_PRINT("[MQTT] MQTT disabled -> skip publish");
    mqttBusy.store(false);
    return true;
  }

  // --- Vérifie le Wi-Fi ---
  if (WiFi.status() != WL_CONNECTED)
  {
    DEBUG_PRINT("[MQTT] WiFi not connected!");
    mqttBusy.store(false);
    return false;
  }

  mqttClient.setServer(cfg.mqtt_host, cfg.mqtt_port);

  // --- Connexion MQTT ---
  String clientId = String(cfg.device_name);
  if (clientId.isEmpty())
    clientId = String("M5CoreS3-") + String((uint32_t)ESP.getEfuseMac(), HEX);

  DEBUG_PRINTF("[MQTT] Connecting to %s:%d as %s\n",
               cfg.mqtt_host, cfg.mqtt_port, clientId.c_str());

  bool connected = false;
  if (strlen(cfg.mqtt_user) == 0)
    connected = mqttClient.connect(clientId.c_str());
  else
    connected = mqttClient.connect(clientId.c_str(), cfg.mqtt_user, cfg.mqtt_pass);

  if (!connected)
  {
    DEBUG_PRINTF("[MQTT] Connection failed, state=%d\n", mqttClient.state());
    mqttBusy.store(false);
    return false;
  }

  // --- Données à publier ---
  float m, h;
  unsigned long dur;
  {
    std::lock_guard<std::mutex> lock(distMutex);
    m = lastMeasuredCm;
    h = lastEstimatedHeight;
    dur = lastDurationUs;
  }

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"measured_cm\":%.2f,\"estimated_cm\":%.2f,\"duration_us\":%lu}",
           m, h, dur);

  DEBUG_PRINTF("[MQTT] Publishing to topic %s: %s\n", cfg.mqtt_topic, payload);

  ok = mqttClient.publish(cfg.mqtt_topic, payload);
  mqttClient.loop();
  delay(50);
  mqttClient.disconnect();

  mqttBusy.store(false);

  DEBUG_PRINT(ok ? "[MQTT] Publish success!" : "[MQTT] Publish failed!");
  return ok;
}