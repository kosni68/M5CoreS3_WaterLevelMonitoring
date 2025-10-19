#pragma once
#include <Arduino.h>

/**
 * État EMA persistant entre les deep sleep.
 * Déclaré en extern ici, défini dans measurement.cpp (RTC_DATA_ATTR).
 */
extern float emaStateCm;

void sensorTask(void *pv);
void initSensor();
float measureDistanceStable();
float measureDistanceCmOnce();
float runningAverage(float newVal, float prevAvg, float alpha = 0.25f);
float estimateHeightFromMeasured(float x);
bool computePolynomialFrom3Points();
void loadCalibrations();
void saveCalibrationToNVS(int idx, float measured, float height);
void saveCuveLevels();
void clearCalibrations();
