#include "global_data.hpp"
#include "util.hpp"
#include <HMI/hmi_manager.hpp>
#include <ModBus/modbus_manager.hpp>
#include <string>
#include "Wifi/http_manager.hpp"
#include <ArduinoJson.h>
#include "StripLed/stripLed_manager.hpp"
#include "Wifi/wifi_manager.hpp"

static TaskHandle_t task_handle_modbus = nullptr;
static TaskHandle_t task_handle_wifi = nullptr;
static TaskHandle_t task_handle_led = nullptr;
static TaskHandle_t task_handle_hmi = nullptr;

unsigned long previousMillis = 0;
unsigned long currentMillis;

#include "HMI/hmiTask.hpp"
#include "Modbus/modbusTask.hpp"
#include "Wifi/wifiTask.hpp"
#include "StripLed/stripLedTask.hpp"

// ************************************************************************************************************************************
// ********************************************************* Setup function ***********************************************************
// ************************************************************************************************************************************

void setup()
{
  Serial.begin(115200);

  GlobalData::AP_wifiIdentification = generateRandomString(4);

  GlobalData::initializePreferences();
  GlobalData::setupUUID();

  if (WiFiManager::initWiFi())
  {
    stripLedManager::setup();
    xTaskCreate(hmiTask, "hmiTask", 4096 * 2, nullptr, 3, &task_handle_hmi);
    xTaskCreate(taskModbus, "taskModbus", 4096, nullptr, 2, &task_handle_modbus);
    xTaskCreate(wifiTask, "wifiTask", 4096 * 4, nullptr, 1, &task_handle_wifi);
    // xTaskCreate(FastLEDshowTask, "FastLEDshowTask", 2048, NULL, 1, &task_handle_led);
  }
  else
  {
    HMIManager::setup();
    HMIManager::displayConfigDevice();
    GlobalData::AP_wifiEnable = true;
    HMIManager::updateBackgroundSprites();
    WiFiManager::accessPointWifiManager();
  }
}

// ************************************************************************************************************************************
// ****************************************************** Core 1 Loop function ********************************************************
// ************************************************************************************************************************************

void loop()
{
  // printLogHeapStack();
  vTaskDelay(10);
}
