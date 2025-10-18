#include <WiFi.h>
#include <PubSubClient.h>
#include "mqtt.h"
#include "measurement.h"
#include "config.h"
#include "config_manager.h"

// ---------- MQTT client ----------
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
volatile bool mqttBusy = false;

void setupMQTT()
{
  const auto cfg = ConfigManager::instance().getConfig();
  mqttClient.setServer(cfg.mqtt_host, cfg.mqtt_port);
}

bool publishMQTT_measure()
{
  {
    std::lock_guard<std::mutex> lock(mqttMutex);
    if (mqttBusy)
    {
      DEBUG_PRINT("[MQTT] Busy - skipping publish");
      return false;
    }
    mqttBusy = true;
  }

  const auto cfg = ConfigManager::instance().getConfig();
  if (!cfg.mqtt_enabled)
  {
    DEBUG_PRINT("[MQTT] MQTT disabled -> skip publish");
    std::lock_guard<std::mutex> lock(mqttMutex);
    mqttBusy = false;
    return true;
  }

  bool ok = false;
  if (!mqttClient.connected())
  {
    String clientId = String(cfg.device_name);
    if (clientId.length() == 0)
      clientId = String("M5CoreS3-") + String((uint32_t)ESP.getEfuseMac(), HEX);

    DEBUG_PRINTF("[MQTT] Connecting to %s:%d as %s\n", cfg.mqtt_host, cfg.mqtt_port, clientId.c_str());
    bool connected = false;
    if (strlen(cfg.mqtt_user) == 0)
      connected = mqttClient.connect(clientId.c_str());
    else
      connected = mqttClient.connect(clientId.c_str(), cfg.mqtt_user, cfg.mqtt_pass);

    if (!connected)
    {
      DEBUG_PRINTF("[MQTT] Connection failed, state=%d\n", mqttClient.state());
      std::lock_guard<std::mutex> lock(mqttMutex);
      mqttBusy = false;
      return false;
    }
  }

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
  delay(60);
  mqttClient.disconnect();

  {
    std::lock_guard<std::mutex> lock(mqttMutex);
    mqttBusy = false;
  }

  DEBUG_PRINT(ok ? "[MQTT] Publish success!" : "[MQTT] Publish failed!");
  return ok;
}

void mqttPeriodicTask(void *pv)
{
  for (;;)
  {
    if (WiFi.status() == WL_CONNECTED && ConfigManager::instance().isMQTTEnabled())
    {
      publishMQTT_measure();
    }
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}
