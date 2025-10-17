#include "config_manager.h"
#include <Preferences.h>
#include <ArduinoJson.h>

ConfigManager& ConfigManager::instance() {
    static ConfigManager mgr;
    return mgr;
}

bool ConfigManager::begin() {
    Preferences prefs;
    if (!prefs.begin("config", true)) return false;

    bool exists = prefs.isKey("mqtt_host") || prefs.isKey("device_name");
    prefs.end();

    if (!exists) {
        applyDefaultsIfNeeded();
        save();
        return true;
    }
    bool ok = loadFromPreferences();
    applyDefaultsIfNeeded();
    return ok;
}

void ConfigManager::applyDefaultsIfNeeded() {
    std::lock_guard<std::mutex> lk(mutex_);

    if (config_.interactive_timeout_ms == 0)config_.interactive_timeout_ms = 600000; // 10 min
    if (config_.deepsleep_interval_s == 0)config_.deepsleep_interval_s = 30;

    if (config_.measure_interval_ms < 50) config_.measure_interval_ms = 1000;
    if (config_.display_brightness > 255) config_.display_brightness = 128;
    if (config_.mqtt_port == 0) config_.mqtt_port = 1883;
    if (strlen(config_.mqtt_host) == 0) strcpy(config_.mqtt_host, "broker.local");
    if (strlen(config_.device_name) == 0) strcpy(config_.device_name, "ESP32-Device");
    if (strlen(config_.app_version) == 0) strcpy(config_.app_version, "1.0.0");
}

bool ConfigManager::loadFromPreferences() {
    std::lock_guard<std::mutex> lk(mutex_);
    Preferences prefs;
    if (!prefs.begin("config", true)) return false;

    config_.mqtt_enabled = prefs.getBool("mqtt_enabled", false);
    prefs.getString("mqtt_host", config_.mqtt_host, sizeof(config_.mqtt_host));
    config_.mqtt_port = prefs.getUShort("mqtt_port", 1883);
    prefs.getString("mqtt_user", config_.mqtt_user, sizeof(config_.mqtt_user));
    prefs.getString("mqtt_pass", config_.mqtt_pass, sizeof(config_.mqtt_pass));
    prefs.getString("mqtt_topic", config_.mqtt_topic, sizeof(config_.mqtt_topic));

    config_.measure_interval_ms = prefs.getUInt("measure_interval_ms", 1000);
    config_.measure_offset_cm = prefs.getFloat("measure_offset_cm", 0.0f);

    config_.display_brightness = prefs.getUChar("display_brightness", 128);
    config_.display_refresh_ms = prefs.getUInt("display_refresh_ms", 500);

    prefs.getString("device_name", config_.device_name, sizeof(config_.device_name));
    config_.interactive_timeout_ms = prefs.getUInt("interactive_timeout_ms", 60000);
    config_.deepsleep_interval_s = prefs.getUInt("deepsleep_interval_s", 30);

    prefs.getString("admin_user", config_.admin_user, sizeof(config_.admin_user));
    prefs.getString("admin_pass", config_.admin_pass, sizeof(config_.admin_pass));
    prefs.getString("app_version", config_.app_version, sizeof(config_.app_version));

    prefs.end();
    return true;
}

bool ConfigManager::save() {
    std::lock_guard<std::mutex> lk(mutex_);
    Preferences prefs;
    if (!prefs.begin("config", false)) return false;

    prefs.putBool("mqtt_enabled", config_.mqtt_enabled);
    prefs.putString("mqtt_host", config_.mqtt_host);
    prefs.putUShort("mqtt_port", config_.mqtt_port);
    prefs.putString("mqtt_user", config_.mqtt_user);
    prefs.putString("mqtt_pass", config_.mqtt_pass);
    prefs.putString("mqtt_topic", config_.mqtt_topic);

    prefs.putUInt("measure_interval_ms", config_.measure_interval_ms);
    prefs.putFloat("measure_offset_cm", config_.measure_offset_cm);

    prefs.putUChar("display_brightness", config_.display_brightness);
    prefs.putUInt("display_refresh_ms", config_.display_refresh_ms);

    prefs.putString("device_name", config_.device_name);
    prefs.putUInt("interactive_timeout_ms", config_.interactive_timeout_ms);
    prefs.putUInt("deepsleep_interval_s", config_.deepsleep_interval_s);

    prefs.putString("admin_user", config_.admin_user);
    prefs.putString("admin_pass", config_.admin_pass);
    prefs.putString("app_version", config_.app_version);

    prefs.end();
    return true;
}

String ConfigManager::toJsonString() {
    std::lock_guard<std::mutex> lk(mutex_);
    JsonDocument doc;
    doc["mqtt_enabled"] = config_.mqtt_enabled;
    doc["mqtt_host"] = config_.mqtt_host;
    doc["mqtt_port"] = config_.mqtt_port;
    doc["mqtt_user"] = config_.mqtt_user;
    doc["mqtt_pass"] = config_.mqtt_pass;
    doc["mqtt_topic"] = config_.mqtt_topic;

    doc["measure_interval_ms"] = config_.measure_interval_ms;
    doc["measure_offset_cm"] = config_.measure_offset_cm;

    doc["display_brightness"] = config_.display_brightness;
    doc["display_refresh_ms"] = config_.display_refresh_ms;

    doc["device_name"] = config_.device_name;
    doc["interactive_timeout_ms"] = config_.interactive_timeout_ms;
    doc["deepsleep_interval_s"] = config_.deepsleep_interval_s;

    doc["admin_user"] = config_.admin_user;
    doc["admin_pass"] = "*****";
    doc["app_version"] = config_.app_version;

    String s;
    serializeJson(doc, s);
    return s;
}

bool ConfigManager::updateFromJson(const String &json) {
    JsonDocument doc;
    auto err = deserializeJson(doc, json);
    if (err) return false;

    std::lock_guard<std::mutex> lk(mutex_);
    if (doc["mqtt_enabled"].is<bool>()) config_.mqtt_enabled = doc["mqtt_enabled"].as<bool>();
    if (doc["mqtt_host"].is<const char*>()) strlcpy(config_.mqtt_host, doc["mqtt_host"], sizeof(config_.mqtt_host));
    if (doc["mqtt_port"].is<uint16_t>()) config_.mqtt_port = doc["mqtt_port"];
    if (doc["mqtt_user"].is<const char*>()) strlcpy(config_.mqtt_user, doc["mqtt_user"], sizeof(config_.mqtt_user));
    if (doc["mqtt_pass"].is<const char*>()) strlcpy(config_.mqtt_pass, doc["mqtt_pass"], sizeof(config_.mqtt_pass));
    if (doc["mqtt_topic"].is<const char*>()) strlcpy(config_.mqtt_topic, doc["mqtt_topic"], sizeof(config_.mqtt_topic));

    if (doc["measure_interval_ms"].is<uint32_t>()) config_.measure_interval_ms = doc["measure_interval_ms"];
    if (doc["measure_offset_cm"].is<float>()) config_.measure_offset_cm = doc["measure_offset_cm"];

    if (doc["display_brightness"].is<uint8_t>()) config_.display_brightness = doc["display_brightness"];
    if (doc["display_refresh_ms"].is<uint32_t>()) config_.display_refresh_ms = doc["display_refresh_ms"];

    if (doc["device_name"].is<const char*>()) strlcpy(config_.device_name, doc["device_name"], sizeof(config_.device_name));
    if (doc["interactive_timeout_ms"].is<uint32_t>()) config_.interactive_timeout_ms = doc["interactive_timeout_ms"];
    if (doc["deepsleep_interval_s"].is<uint32_t>()) config_.deepsleep_interval_s = doc["deepsleep_interval_s"];

    if (doc["admin_user"].is<const char*>()) strlcpy(config_.admin_user, doc["admin_user"], sizeof(config_.admin_user));
    if (doc["admin_pass"].is<const char*>()) strlcpy(config_.admin_pass, doc["admin_pass"], sizeof(config_.admin_pass));

    applyDefaultsIfNeeded();
    return save();
}

AppConfig ConfigManager::getConfig() {
    std::lock_guard<std::mutex> lk(mutex_);
    return config_;
}

uint32_t ConfigManager::getMeasureIntervalMs() {
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.measure_interval_ms;
}

float ConfigManager::getMeasureOffsetCm() {
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.measure_offset_cm;
}

uint8_t ConfigManager::getDisplayBrightness() {
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.display_brightness;
}

bool ConfigManager::isMQTTEnabled() {
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.mqtt_enabled;
}

const char* ConfigManager::getAdminUser() {
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.admin_user;
}

const char* ConfigManager::getAdminPass() {
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.admin_pass;
}
