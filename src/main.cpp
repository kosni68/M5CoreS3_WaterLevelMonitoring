#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>

#include "config.h"
#include "measurement.h"
#include "display.h"
#include "mqtt.h"
#include "web_server.h"
#include "power.h"
#include "utils.h"

WebServer server(80);

bool interactiveMode = false;
uint32_t interactiveLastTouchMs = 0;

void setup() {
    Serial.begin(115200);
    DEBUG_PRINT("Booting M5CoreS3 JSN_SR04T...");

    initSensor();
    loadCalibrations();
    computePolynomialFrom3Points();
    setupMQTT();

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        float avg=0;
        for(int i=0;i<3;i++){
            avg = runningAverage(measureDistanceCmOnce(), avg, 0.5f);
            delay(30);
        }
        lastEstimatedHeight = estimateHeightFromMeasured(avg);
        lastMeasuredCm = avg;
        if (connectWiFiShort(6000))
            publishMQTT_measure();
        goDeepSleep();
    } else {
        Serial.println("interactive mode");
        
        initDisplay();

        startWebServer();

        xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, NULL, 2, NULL, 1);
        xTaskCreatePinnedToCore(displayTask, "displayTask", 8192, NULL, 1, NULL, 1);

        interactiveMode = true;
        interactiveLastTouchMs = millis();
    }
}

void loop() {
    if (interactiveMode) {
        server.handleClient();
        if ((uint32_t)(millis() - interactiveLastTouchMs) > INTERACTIVE_TIMEOUT_MS) {
            server.stop();
            disconnectWiFiClean();
            delay(50);
            goDeepSleep();
        }
    }
    delay(10);
}
