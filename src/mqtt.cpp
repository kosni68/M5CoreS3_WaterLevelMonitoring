#include <WiFi.h>
#include <PubSubClient.h>
#include "mqtt.h"
#include "measurement.h"
#include "config.h"

// ---------- MQTT client ----------
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
volatile bool mqttBusy = false;

void setupMQTT() {
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
}

bool publishMQTT_measure() {
  // Prevent re-entrancy
  {
    std::lock_guard<std::mutex> lock(mqttMutex);
    if (mqttBusy) {
      DEBUG_PRINT("[MQTT] Busy - skipping publish");
      return false;
    }
    mqttBusy = true;
  }

  // Optional safe display update: try to lock displayMutex and draw; if not, skip display write.
  bool drewStatus = false;
  /*if (displayMutex.try_lock()) {
    if (M5.Display.isReadable()) {
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setTextColor(GREEN);
      M5.Lcd.setTextDatum(MC_DATUM);
      M5.Lcd.drawString("MQTT...", M5.Lcd.width()/2, M5.Lcd.height()/2);
      drewStatus = true;
    }
    displayMutex.unlock();
  }*/

  if (!ENABLE_MQTT) {
    DEBUG_PRINT("[MQTT] MQTT disabled -> skip publish");
    // release mqttBusy
    std::lock_guard<std::mutex> lock(mqttMutex);
    mqttBusy = false;
    return true;
  }

  bool ok = false;
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
      // reset busy
      std::lock_guard<std::mutex> lock(mqttMutex);
      mqttBusy = false;
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

  ok = mqttClient.publish(MQTT_TOPIC, payload);

  // ensure the library processes the packet and receives ACK if QoS0/1
  mqttClient.loop();
  delay(60); // small wait to allow ACK/packet handling

  if (!ok) {
    DEBUG_PRINT("[MQTT] Publish failed!");
  } else {
    DEBUG_PRINT("[MQTT] Publish success!");
  }

  // Disconnect gracefully
  mqttClient.disconnect();
  DEBUG_PRINT("[MQTT] Disconnected");

  // Optionally update display with result (non-blocking if possible)
  /*if (displayMutex.try_lock()) {
    if (M5.Display.isReadable()) {
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setTextColor(ok ? GREEN : RED);
      M5.Lcd.setTextDatum(MC_DATUM);
      M5.Lcd.drawString(ok ? "MQTT OK" : "MQTT FAIL", M5.Lcd.width()/2, M5.Lcd.height()/2);
      delay(800);
      // restore later display will redraw
    }
    displayMutex.unlock();
  }*/

  // release busy flag
  {
    std::lock_guard<std::mutex> lock(mqttMutex);
    mqttBusy = false;
  }
  return ok;
}

void mqttPeriodicTask(void *pv) {
    for (;;) {
        if (WiFi.status() == WL_CONNECTED && ENABLE_MQTT) {
            publishMQTT_measure();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
