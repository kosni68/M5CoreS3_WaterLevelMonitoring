#pragma once
#include <Arduino.h>

bool connectWiFiShort(uint32_t timeoutMs = 8000);
void disconnectWiFiClean();
void printLogHeapStack();
void convertUint16ToBooleans(int value, bool bits[16]);
void updateMinMaxTime(unsigned int startTime, unsigned int &currentTime, unsigned int &minTime, unsigned int &maxTime);
