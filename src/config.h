#pragma once
#include <Arduino.h>
#include <mutex>
#include <atomic>

// ---------- DEBUG ----------
#define DEBUG true
#define DEBUG_PRINT(x)     \
    if (DEBUG)             \
    {                      \
        Serial.println(x); \
    }
#define DEBUG_PRINTF(...)           \
    if (DEBUG)                      \
    {                               \
        Serial.printf(__VA_ARGS__); \
    }

extern std::mutex distMutex;
extern std::mutex mqttMutex;
extern std::mutex displayMutex;

// ---------- Pins ----------
const int trigPin = 9;
const int echoPin = 8;

// ---------- Timing ----------
const int SENSOR_PERIOD_MS = 200;
const int DISPLAY_PERIOD_MS = 300;
extern std::atomic<uint32_t> interactiveLastTouchMs;

extern float lastMeasuredCm;
extern float lastEstimatedHeight;
extern unsigned long lastDurationUs;

extern float cuveVide;
extern float cuvePleine;

extern float calib_m[3];
extern float calib_h[3];
