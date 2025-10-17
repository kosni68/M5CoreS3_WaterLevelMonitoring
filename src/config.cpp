// config.cpp
#include "config.h"

// ---------- WIFI ----------
const char* WIFI_SSID = "Freebox-22A0D2";
const char* WIFI_PASS = "NicoCindy22";

// ---------- MQTT ----------
const bool ENABLE_MQTT = true;
const char* MQTT_HOST = "192.168.1.171";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "ha-mqtt";
const char* MQTT_PASS = "ha-mqtt_68440";
const char* MQTT_TOPIC = "m5stack/puits";

uint32_t interactiveLastTouchMs;

std::mutex distMutex;
std::mutex mqttMutex; 
std::mutex displayMutex;
