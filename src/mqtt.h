#pragma once
#include <Arduino.h>

void setupMQTT();
bool publishMQTT_measure();
void mqttPeriodicTask(void *pv);
