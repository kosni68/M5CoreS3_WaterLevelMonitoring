#include <M5CoreS3.h>
#include <Preferences.h>
#include <algorithm> // std::sort
#include "measurement.h"
#include "config.h"
#include "config_manager.h"

// ---------- Globals ----------
RTC_DATA_ATTR bool wokeFromTimer = false;

float lastMeasuredCm = -1.0f;
float lastEstimatedHeight = -1.0f;
unsigned long lastDurationUs = 0;

// Calibration
float calib_m[3] = {0.0f, 0.0f, 0.0f};
float calib_h[3] = {50.0f, 100.0f, 150.0f};

// Cuve levels
float cuveVide = 123.0f;
float cuvePleine = 42.0f;

// Polynomial coeffs
double a_coef = 0, b_coef = 0, c_coef = 0;

Preferences preferences;

void sensorTask(void *pv)
{
    float avg = 0.0f;
    for (;;)
    {
        float m = measureDistanceStable();

        // Apply configurable offset (dynamic)
        float offset = ConfigManager::instance().getMeasureOffsetCm();
        if (m > 0)
            m += offset;

        // alpha dynamique depuis la config
        float alpha = ConfigManager::instance().getRunningAverageAlpha();
        if (alpha <= 0.0f || alpha > 1.0f)
            alpha = 0.25f;
        avg = runningAverage(m, avg, alpha);

        float est = NAN;
        if (avg > 0.0f)
            est = estimateHeightFromMeasured(avg);
        {
            std::lock_guard<std::mutex> lock(distMutex);
            lastMeasuredCm = avg;
            lastEstimatedHeight = est;
        }

        // Dynamic period from ConfigManager (allows live tuning)
        uint32_t periodMs = ConfigManager::instance().getMeasureIntervalMs();
        if (periodMs < 50)
            periodMs = 50; // safety lower bound

        vTaskDelay(pdMS_TO_TICKS(periodMs));
    }
}

void initSensor()
{
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
}

float measureDistanceCmOnce()
{
    digitalWrite(trigPin, LOW);
    delayMicroseconds(4);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    unsigned long duration = pulseInLong(echoPin, HIGH, 30000UL);
    lastDurationUs = duration;
    if (duration == 0)
        return -1.0f;
    return duration * 0.01715f;
}

float measureDistanceStable()
{
    // paramètres dynamiques
    const uint16_t N = ConfigManager::instance().getMedianSamples();
    const uint16_t dlyMs = ConfigManager::instance().getMedianSampleDelayMs();
    const float minCm = ConfigManager::instance().getFilterMinCm();
    const float maxCm = ConfigManager::instance().getFilterMaxCm();

    const uint16_t Ns = (N == 0 ? 1 : (N > 15 ? 15 : N));

    float values[15];
    int count = 0;

    for (uint16_t i = 0; i < Ns; ++i)
    {
        float d = measureDistanceCmOnce();
        if (d > 0 && d >= minCm && d <= maxCm)
        {
            values[count++] = d; // filtre bruit
        }
        if (dlyMs > 0)
            delay(dlyMs);
    }
    if (count == 0)
        return -1.0f;

    std::sort(values, values + count);
    // médiane robuste
    return values[count / 2];
}

float runningAverage(float newVal, float prevAvg, float alpha)
{
    if (newVal < 0)
        return prevAvg;
    return prevAvg * (1.0f - alpha) + newVal * alpha;
}

bool computePolynomialFrom3Points()
{
    double x1 = calib_m[0], x2 = calib_m[1], x3 = calib_m[2];
    double y1 = calib_h[0], y2 = calib_h[1], y3 = calib_h[2];
    if (x1 == 0 || x2 == 0 || x3 == 0)
        return false;
    if ((x1 == x2) || (x1 == x3) || (x2 == x3))
        return false;
    double denom = (x1 - x2) * (x1 - x3) * (x2 - x3);
    if (denom == 0)
        return false;
    a_coef = (x3 * (y2 - y1) + x2 * (y1 - y3) + x1 * (y3 - y2)) / denom;
    b_coef = (x3 * x3 * (y1 - y2) + x2 * x2 * (y3 - y1) + x1 * x1 * (y2 - y3)) / denom;
    c_coef = (x2 * x3 * (x2 - x3) * y1 + x3 * x1 * (x3 - x1) * y2 + x1 * x2 * (x1 - x2) * y3) / denom;
    return true;
}

float estimateHeightFromMeasured(float x)
{
    return (float)(a_coef * x * x + b_coef * x + c_coef);
}

void loadCalibrations()
{
    preferences.begin("calib", true);
    for (int i = 0; i < 3; i++)
    {
        char kM[8], kH[8];
        sprintf(kM, "m%d", i);
        sprintf(kH, "h%d", i);
        calib_m[i] = preferences.getFloat(kM, calib_m[i]);
        calib_h[i] = preferences.getFloat(kH, calib_h[i]);
    }
    cuveVide = preferences.getFloat("cuveVide", cuveVide);
    cuvePleine = preferences.getFloat("cuvePleine", cuvePleine);
    preferences.end();
}

void saveCalibrationToNVS(int idx, float measured, float height)
{
    if (idx < 0 || idx > 2)
        return;
    preferences.begin("calib", false);
    char kM[8], kH[8];
    sprintf(kM, "m%d", idx);
    sprintf(kH, "h%d", idx);
    preferences.putFloat(kM, measured);
    preferences.putFloat(kH, height);
    preferences.end();
    calib_m[idx] = measured;
    calib_h[idx] = height;
}

void saveCuveLevels()
{
    preferences.begin("calib", false);
    preferences.putFloat("cuveVide", cuveVide);
    preferences.putFloat("cuvePleine", cuvePleine);
    preferences.end();
}

void clearCalibrations()
{
    preferences.begin("calib", false);
    preferences.clear();
    preferences.end();
    for (int i = 0; i < 3; i++)
        calib_m[i] = 0;
}
