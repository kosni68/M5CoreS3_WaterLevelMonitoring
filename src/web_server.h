#pragma once
#include <Arduino.h>

void startWebServer();
void handleRoot();
void handleDistanceApi();
void handleCalibsApi();
void handleSaveCalib();
void handleClearCalib();
void handleSetCuve();
void handleSendMQTT();