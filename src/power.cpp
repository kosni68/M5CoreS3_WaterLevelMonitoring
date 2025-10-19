#include <M5CoreS3.h>
#include <WiFi.h>
#include "power.h"
#include "config_manager.h"

static inline bool modeIsAp(wifi_mode_t mode)
{
    // Couverture des constantes ESP-IDF/Arduino
    return (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA
#ifdef WIFI_AP
            || mode == WIFI_AP || mode == WIFI_AP_STA
#endif
    );
}

bool isApModeActive()
{
    wifi_mode_t mode = WiFi.getMode();
    return modeIsAp(mode);
}

void goDeepSleep()
{
    // Filet de sécurité : jamais de deep sleep si AP actif
    if (isApModeActive())
    {
        Serial.println("[POWER] AP actif : deep sleep desactive.");
        return;
    }

    M5.Display.sleep();
    M5.Display.setBrightness(0);

    const uint64_t us = (uint64_t)ConfigManager::instance().getConfig().deepsleep_interval_s * 1000000ULL;
    esp_sleep_enable_timer_wakeup(us);
    delay(20);
    esp_deep_sleep_start();
}
