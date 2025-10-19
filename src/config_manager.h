#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <mutex>

#define WIFI_SSID_LEN 32
#define WIFI_PASS_LEN 64

#define MQTT_HOST_LEN 64
#define MQTT_USER_LEN 32
#define MQTT_PASS_LEN 64
#define MQTT_TOPIC_LEN 64
#define DEVICE_NAME_LEN 32
#define ADMIN_USER_LEN 16
#define ADMIN_PASS_LEN 16
#define APP_VERSION_LEN 16

struct AppConfig
{
    // ---- Wi-Fi (STA) ----
    char wifi_ssid[WIFI_SSID_LEN];
    char wifi_pass[WIFI_PASS_LEN];

    // ---- MQTT ----
    bool mqtt_enabled;
    char mqtt_host[MQTT_HOST_LEN];
    uint16_t mqtt_port;
    char mqtt_user[MQTT_USER_LEN];
    char mqtt_pass[MQTT_PASS_LEN];
    char mqtt_topic[MQTT_TOPIC_LEN];

    // ---- Mesure ----
    uint32_t measure_interval_ms;
    float measure_offset_cm;

    // ---- Stabilisation / filtre ----
    float avg_alpha;          // 0..1
    uint16_t median_n;        // 1..15
    uint16_t median_delay_ms; // 0..1000
    float filter_min_cm;      // e.g. 2.0
    float filter_max_cm;      // e.g. 400.0

    // ---- Divers ----
    char device_name[DEVICE_NAME_LEN];
    uint32_t interactive_timeout_ms;
    uint32_t deepsleep_interval_s;

    char admin_user[ADMIN_USER_LEN];
    char admin_pass[ADMIN_PASS_LEN];

    char app_version[APP_VERSION_LEN];
};

class ConfigManager
{
public:
    static ConfigManager &instance();
    bool begin();
    bool save();
    String toJsonString();
    bool updateFromJson(const String &json);

    AppConfig getConfig();
    uint32_t getMeasureIntervalMs();
    float getMeasureOffsetCm();

    // NEW getters
    float getRunningAverageAlpha();
    uint16_t getMedianSamples();
    uint16_t getMedianSampleDelayMs();
    float getFilterMinCm();
    float getFilterMaxCm();

    bool isMQTTEnabled();
    const char *getAdminUser();
    const char *getAdminPass();

private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager &) = delete;
    ConfigManager &operator=(const ConfigManager &) = delete;

    void applyDefaultsIfNeeded();
    bool loadFromPreferences();

    AppConfig config_{};
    std::mutex mutex_;
};
