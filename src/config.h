#pragma once
#include <Arduino.h>
#include <mutex>

// ---------- DEBUG ----------
#define DEBUG true
#define DEBUG_PRINT(x)  if(DEBUG){ Serial.println(x); }
#define DEBUG_PRINTF(...) if(DEBUG){ Serial.printf(__VA_ARGS__); }

extern const char* WIFI_SSID;
extern const char* WIFI_PASS;

extern const bool ENABLE_MQTT;
extern const char* MQTT_HOST;
extern const char* MQTT_USER;
extern const char* MQTT_PASS;
extern const char* MQTT_TOPIC;
extern const uint16_t MQTT_PORT;

extern std::mutex distMutex;
extern std::mutex mqttMutex; 
extern std::mutex displayMutex;

// ---------- Pins ----------
const int trigPin = 9;
const int echoPin = 8;

// ---------- Timing ----------
const int SENSOR_PERIOD_MS = 200;
const int DISPLAY_PERIOD_MS = 300;
const uint64_t DEEPSLEEP_INTERVAL_S = 30ULL;
const uint32_t INTERACTIVE_TIMEOUT_MS = 1 * 60 * 1000UL; // 10 min

extern float lastMeasuredCm;
extern float lastEstimatedHeight;
extern unsigned long lastDurationUs;

extern float cuveVide;
extern float cuvePleine;

extern float calib_m[3];
extern float calib_h[3];