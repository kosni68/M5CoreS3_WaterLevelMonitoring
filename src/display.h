#pragma once
#include <Arduino.h>

void displayTask(void *pv);
void initDisplay();
void drawGaugeBackground();
void drawGaugeFill(int percent);
void updateDisplay(float measured, float estimated, unsigned long duration, float cuveVide, float cuvePleine);
