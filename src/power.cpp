#include <M5CoreS3.h>
#include "power.h"
#include "config_manager.h"

void goDeepSleep() {
    M5.Display.sleep();
    M5.Display.setBrightness(0);
    esp_sleep_enable_timer_wakeup(ConfigManager::instance().getConfig().deepsleep_interval_s * 1000000ULL);
    delay(20);
    esp_deep_sleep_start();
}
