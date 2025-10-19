#include <Arduino.h>

#include "config.h"
#include "config_manager.h"
#include "measurement.h"
#include "display.h"
#include "mqtt.h"
#include "web_server.h"
#include "power.h"
#include "utils.h"
#include <math.h> // isfinite

bool interactiveMode = false;

void setup()
{
    Serial.begin(115200);
    DEBUG_PRINT("Booting M5CoreS3 JSN_SR04T...");

    // Initialisation du gestionnaire de configuration
    if (!ConfigManager::instance().begin())
    {
        Serial.println("[WEB][WARN] ConfigManager n’a pas pu charger la configuration, utilisation des valeurs par défaut.");
        ConfigManager::instance().save();
    }

    initSensor();
    loadCalibrations();
    computePolynomialFrom3Points();
    setupMQTT();

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER)
    {
        float avg = (isfinite(emaStateCm) ? emaStateCm : NAN);

        float alpha = ConfigManager::instance().getRunningAverageAlpha();
        if (alpha <= 0.0f || alpha > 1.0f)
            alpha = 0.25f;

        for (int i = 0; i < 3; i++)
        {
            float m = measureDistanceStable();
            if (m > 0)
            {
                m += ConfigManager::instance().getMeasureOffsetCm();

                if (!isfinite(avg))
                {
                    avg = m;
                }
                else
                {
                    avg = runningAverage(m, avg, alpha);
                }
            }
            delay(30);
        }

        lastEstimatedHeight = (isfinite(avg) ? estimateHeightFromMeasured(avg) : -1.0f);
        lastMeasuredCm = (isfinite(avg) ? avg : -1.0f);

        if (isfinite(avg))
        {
            emaStateCm = avg;
        }

        if (connectWiFiShort(6000))
            publishMQTT_measure();

        goDeepSleep();
    }
    else
    {
        Serial.println("interactive mode");

        initDisplay();

        startWebServer();

        xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, NULL, 2, NULL, 1);
        xTaskCreatePinnedToCore(displayTask, "displayTask", 8192, NULL, 1, NULL, 1);

        interactiveMode = true;
        interactiveLastTouchMs = millis();
    }
}

void loop()
{
    if (interactiveMode)
    {
        const uint32_t timeout = ConfigManager::instance().getConfig().interactive_timeout_ms;
        if ((uint32_t)(millis() - interactiveLastTouchMs.load()) > timeout)
        {
            if (isApModeActive())
            {
                interactiveLastTouchMs = millis();
            }
            else
            {
                disconnectWiFiClean();
                delay(50);
                goDeepSleep();
            }
        }
    }
    delay(10);
}
