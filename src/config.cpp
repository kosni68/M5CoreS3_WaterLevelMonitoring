// config.cpp
#include "config.h"

// ---------- WIFI ----------
const char* WIFI_SSID = "Freebox-22A0D2";
const char* WIFI_PASS = "NicoCindy22";

uint32_t interactiveLastTouchMs;

std::mutex distMutex;
std::mutex mqttMutex; 
std::mutex displayMutex;
