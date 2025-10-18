#include "config_manager.h"
#include <Preferences.h>
#include <ArduinoJson.h>

ConfigManager &ConfigManager::instance()
{
    static ConfigManager mgr;
    return mgr;
}

bool ConfigManager::begin()
{
    Serial.println("[ConfigManager] Initialisation...");

    Preferences prefs;
    if (!prefs.begin("config", true))
    {
        Serial.println("[ConfigManager] Erreur: impossible d’ouvrir les preferences en lecture.");
        return false;
    }

    bool exists = prefs.isKey("mqtt_host") || prefs.isKey("device_name");
    prefs.end();

    if (!exists)
    {
        Serial.println("[ConfigManager] Aucune configuration trouvée. Application des valeurs par défaut...");
        applyDefaultsIfNeeded();
        save();
        return true;
    }

    bool ok = loadFromPreferences();
    applyDefaultsIfNeeded();
    Serial.printf("[ConfigManager] Chargement %s\n", ok ? "réussi" : "échoué");
    return ok;
}

void ConfigManager::applyDefaultsIfNeeded()
{
    std::lock_guard<std::mutex> lk(mutex_);
    Serial.println("[ConfigManager] Vérification des valeurs par défaut...");

    if (config_.interactive_timeout_ms == 0)
    {
        config_.interactive_timeout_ms = 600000;
        Serial.println("  -> interactive_timeout_ms défini à 600000");
    }
    if (config_.deepsleep_interval_s == 0)
    {
        config_.deepsleep_interval_s = 30;
        Serial.println("  -> deepsleep_interval_s défini à 30");
    }
    if (config_.measure_interval_ms < 50)
    {
        config_.measure_interval_ms = 1000;
        Serial.println("  -> measure_interval_ms défini à 1000");
    }
    if (config_.mqtt_port == 0)
    {
        config_.mqtt_port = 1883;
        Serial.println("  -> mqtt_port défini à 1883");
    }
    if (strlen(config_.mqtt_host) == 0)
    {
        strcpy(config_.mqtt_host, "broker.local");
        Serial.println("  -> mqtt_host défini à broker.local");
    }
    if (strlen(config_.device_name) == 0)
    {
        strcpy(config_.device_name, "ESP32-Device");
        Serial.println("  -> device_name défini à ESP32-Device");
    }
    if (strlen(config_.app_version) == 0)
    {
        strcpy(config_.app_version, "1.0.0");
        Serial.println("  -> app_version défini à 1.0.0");
    }
}

bool ConfigManager::loadFromPreferences()
{
    std::lock_guard<std::mutex> lk(mutex_);
    Serial.println("[ConfigManager] Chargement depuis Preferences...");

    Preferences prefs;
    if (!prefs.begin("config", true))
    {
        Serial.println("  -> Erreur: impossible d’ouvrir les preferences en lecture.");
        return false;
    }

    config_.mqtt_enabled = prefs.getBool("mqtt_enabled", false);
    prefs.getString("mqtt_host", config_.mqtt_host, sizeof(config_.mqtt_host));
    config_.mqtt_port = prefs.getUShort("mqtt_port", 1883);
    prefs.getString("mqtt_user", config_.mqtt_user, sizeof(config_.mqtt_user));
    prefs.getString("mqtt_pass", config_.mqtt_pass, sizeof(config_.mqtt_pass));
    prefs.getString("mqtt_topic", config_.mqtt_topic, sizeof(config_.mqtt_topic));

    config_.measure_interval_ms = prefs.getUInt("measure_interval_ms", 1000);
    config_.measure_offset_cm = prefs.getFloat("measure_offset_cm", 0.0f);

    prefs.getString("device_name", config_.device_name, sizeof(config_.device_name));
    config_.interactive_timeout_ms = prefs.getUInt("interactive_timeout_ms", 60000);
    config_.deepsleep_interval_s = prefs.getUInt("deepsleep_interval_s", 30);

    prefs.getString("admin_user", config_.admin_user, sizeof(config_.admin_user));
    prefs.getString("admin_pass", config_.admin_pass, sizeof(config_.admin_pass));
    prefs.getString("app_version", config_.app_version, sizeof(config_.app_version));

    prefs.end();

    Serial.printf("  -> MQTT %s @ %s:%d (user=%s)\n",
                  config_.mqtt_enabled ? "activé" : "désactivé",
                  config_.mqtt_host, config_.mqtt_port, config_.mqtt_user);
    Serial.printf("  -> Device: %s, Intervalle mesure: %lu ms, Offset: %.2f cm\n",
                  config_.device_name, config_.measure_interval_ms, config_.measure_offset_cm);
    Serial.printf("  -> DeepSleep: %lu s, Timeout interactif: %lu ms\n",
                  (unsigned long)config_.deepsleep_interval_s,
                  (unsigned long)config_.interactive_timeout_ms);
    return true;
}

bool ConfigManager::save()
{
    std::lock_guard<std::mutex> lk(mutex_);
    Serial.println("[ConfigManager] Sauvegarde dans Preferences...");

    Preferences prefs;
    if (!prefs.begin("config", false))
    {
        Serial.println("  -> Erreur: impossible d’ouvrir les preferences en écriture.");
        return false;
    }

    prefs.putBool("mqtt_enabled", config_.mqtt_enabled);
    prefs.putString("mqtt_host", config_.mqtt_host);
    prefs.putUShort("mqtt_port", config_.mqtt_port);
    prefs.putString("mqtt_user", config_.mqtt_user);
    prefs.putString("mqtt_pass", config_.mqtt_pass);
    prefs.putString("mqtt_topic", config_.mqtt_topic);
    yield();

    prefs.putUInt("measure_interval_ms", config_.measure_interval_ms);
    prefs.putFloat("measure_offset_cm", config_.measure_offset_cm);
    yield();

    prefs.putString("device_name", config_.device_name);
    prefs.putUInt("interactive_timeout_ms", config_.interactive_timeout_ms);
    prefs.putUInt("deepsleep_interval_s", config_.deepsleep_interval_s);
    yield();

    prefs.putString("admin_user", config_.admin_user);
    prefs.putString("admin_pass", config_.admin_pass);
    prefs.putString("app_version", config_.app_version);
    yield();

    prefs.end();
    Serial.println("  -> Configuration sauvegardée avec succès !");
    return true;
}

String ConfigManager::toJsonString()
{
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

    doc["device_name"] = config_.device_name;
    doc["interactive_timeout_ms"] = config_.interactive_timeout_ms;
    doc["deepsleep_interval_s"] = config_.deepsleep_interval_s;

    doc["admin_user"] = config_.admin_user;
    doc["admin_pass"] = "*****";
    doc["app_version"] = config_.app_version;

    String s;
    serializeJson(doc, s);
    Serial.println("[ConfigManager] Conversion en JSON effectuée.");
    return s;
}
bool ConfigManager::updateFromJson(const String &json)
{
    Serial.println("[ConfigManager] Mise à jour depuis JSON...");

    JsonDocument doc;
    auto err = deserializeJson(doc, json);
    if (err)
    {
        Serial.println("[ConfigManager][ERR] JSON invalide !");
        return false;
    }

    {
        // Limiter la portée du verrou au strict minimum
        std::lock_guard<std::mutex> lk(mutex_);

        if (doc["mqtt_enabled"].is<bool>())
            config_.mqtt_enabled = doc["mqtt_enabled"].as<bool>();
        if (doc["mqtt_host"].is<const char *>())
            strlcpy(config_.mqtt_host, doc["mqtt_host"], sizeof(config_.mqtt_host));
        if (doc["mqtt_port"].is<uint16_t>())
            config_.mqtt_port = doc["mqtt_port"];
        if (doc["mqtt_user"].is<const char *>())
            strlcpy(config_.mqtt_user, doc["mqtt_user"], sizeof(config_.mqtt_user));
        if (doc["mqtt_pass"].is<const char *>())
            strlcpy(config_.mqtt_pass, doc["mqtt_pass"], sizeof(config_.mqtt_pass));
        if (doc["mqtt_topic"].is<const char *>())
            strlcpy(config_.mqtt_topic, doc["mqtt_topic"], sizeof(config_.mqtt_topic));

        if (doc["measure_interval_ms"].is<uint32_t>())
            config_.measure_interval_ms = doc["measure_interval_ms"];
        if (doc["measure_offset_cm"].is<float>())
            config_.measure_offset_cm = doc["measure_offset_cm"];

        if (doc["device_name"].is<const char *>())
            strlcpy(config_.device_name, doc["device_name"], sizeof(config_.device_name));
        if (doc["interactive_timeout_ms"].is<uint32_t>())
            config_.interactive_timeout_ms = doc["interactive_timeout_ms"];
        if (doc["deepsleep_interval_s"].is<uint32_t>())
            config_.deepsleep_interval_s = doc["deepsleep_interval_s"];

        if (doc["admin_user"].is<const char *>())
            strlcpy(config_.admin_user, doc["admin_user"], sizeof(config_.admin_user));
        if (doc["admin_pass"].is<const char *>())
            strlcpy(config_.admin_pass, doc["admin_pass"], sizeof(config_.admin_pass));
    } // 🔓 le lock est libéré ici

    Serial.println("  -> Mise à jour de la configuration en mémoire OK.");

    applyDefaultsIfNeeded();

    // Sauvegarde asynchrone après libération du lock
    xTaskCreate([](void *)
                {
        Serial.println("[ConfigManager][Task] Sauvegarde asynchrone en cours...");
        ConfigManager::instance().save();
        Serial.println("[ConfigManager][Task] Sauvegarde terminée !");
        vTaskDelete(NULL); }, "saveConfigAsync", 4096, NULL, 1, NULL);

    return true;
}

AppConfig ConfigManager::getConfig()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_;
}

uint32_t ConfigManager::getMeasureIntervalMs()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.measure_interval_ms;
}

float ConfigManager::getMeasureOffsetCm()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.measure_offset_cm;
}

bool ConfigManager::isMQTTEnabled()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.mqtt_enabled;
}

const char *ConfigManager::getAdminUser()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.admin_user;
}

const char *ConfigManager::getAdminPass()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.admin_pass;
}
