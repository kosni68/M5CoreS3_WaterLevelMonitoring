#pragma once
#include <Arduino.h>

void sensorTask(void *pv);
void initSensor();
float measureDistanceCmOnce();
float runningAverage(float newVal, float prevAvg, float alpha=0.25f);
float estimateHeightFromMeasured(float x);
bool computePolynomialFrom3Points();
void loadCalibrations();
void saveCalibrationToNVS(int idx, float measured, float height);
void saveCuveLevels();
void clearCalibrations();
