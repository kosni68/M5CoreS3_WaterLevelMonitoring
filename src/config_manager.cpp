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

    bool exists = prefs.isKey("mqtt_host") || prefs.isKey("device_name") || prefs.isKey("wifi_ssid");
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

    // --- Divers ---
    if (config_.interactive_timeout_ms == 0)
    {
        config_.interactive_timeout_ms = 600000; // 10 min
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
    if (strlen(config_.admin_user) == 0)
    {
        strcpy(config_.admin_user, "admin");
        Serial.println("  -> admin_user défini à 'admin' (à changer !)");
    }
    if (strlen(config_.admin_pass) == 0)
    {
        strcpy(config_.admin_pass, "admin");
        Serial.println("  -> admin_pass défini à 'admin' (à changer !)");
    }

    // NEW defaults (filtres / moyenne)
    if (config_.avg_alpha <= 0.0f || config_.avg_alpha > 1.0f)
    {
        config_.avg_alpha = 0.25f;
        Serial.println("  -> avg_alpha défini à 0.25");
    }
    if (config_.median_n == 0 || config_.median_n > 15)
    {
        config_.median_n = 5;
        Serial.println("  -> median_n défini à 5");
    }
    if (config_.median_delay_ms > 1000)
    {
        config_.median_delay_ms = 50;
        Serial.println("  -> median_delay_ms défini à 50");
    }
    if (config_.filter_min_cm <= 0.0f)
    {
        config_.filter_min_cm = 2.0f;
        Serial.println("  -> filter_min_cm défini à 2.0");
    }
    if (config_.filter_max_cm < config_.filter_min_cm)
    {
        config_.filter_max_cm = 400.0f;
        Serial.println("  -> filter_max_cm défini à 400.0");
    }

    // Wi-Fi: par défaut, laissé vide => AP fallback dans le serveur web
    // (pas de SSID/PASS hardcodés)
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

    // Wi-Fi
    prefs.getString("wifi_ssid", config_.wifi_ssid, sizeof(config_.wifi_ssid));
    prefs.getString("wifi_pass", config_.wifi_pass, sizeof(config_.wifi_pass));

    // MQTT
    config_.mqtt_enabled = prefs.getBool("mqtt_en", false);
    prefs.getString("mqtt_host", config_.mqtt_host, sizeof(config_.mqtt_host));
    config_.mqtt_port = prefs.getUShort("mqtt_port", 1883);
    prefs.getString("mqtt_user", config_.mqtt_user, sizeof(config_.mqtt_user));
    prefs.getString("mqtt_pass", config_.mqtt_pass, sizeof(config_.mqtt_pass));
    prefs.getString("mqtt_topic", config_.mqtt_topic, sizeof(config_.mqtt_topic));

    // Mesure
    config_.measure_interval_ms = prefs.getUInt("meas_int_ms", 1000);
    config_.measure_offset_cm = prefs.getFloat("meas_off_cm", 0.0f);

    // NEW filtres
    config_.avg_alpha = prefs.getFloat("avg_alpha", 0.25f);
    config_.median_n = prefs.getUShort("median_n", 5);
    config_.median_delay_ms = prefs.getUShort("median_delay_ms", 50);
    config_.filter_min_cm = prefs.getFloat("f_min_cm", 2.0f);
    config_.filter_max_cm = prefs.getFloat("f_max_cm", 400.0f);

    // Divers
    prefs.getString("dev_name", config_.device_name, sizeof(config_.device_name));
    config_.interactive_timeout_ms = prefs.getUInt("int_to_ms", 600000);
    config_.deepsleep_interval_s = prefs.getUInt("deep_int_s", 30);

    prefs.getString("adm_user", config_.admin_user, sizeof(config_.admin_user));
    prefs.getString("adm_pass", config_.admin_pass, sizeof(config_.admin_pass));
    prefs.getString("app_ver", config_.app_version, sizeof(config_.app_version));

    prefs.end();

    Serial.printf("  -> WiFi SSID: %s (%s)\n",
                  (strlen(config_.wifi_ssid) ? config_.wifi_ssid : "<non configuré>"),
                  (strlen(config_.wifi_pass) ? "pass défini" : "pass non défini"));
    Serial.printf("  -> MQTT %s @ %s:%d (user=%s)\n",
                  config_.mqtt_enabled ? "activé" : "désactivé",
                  config_.mqtt_host, config_.mqtt_port, config_.mqtt_user);
    Serial.printf("  -> Device: %s, Intervalle mesure: %lu ms, Offset: %.2f cm\n",
                  config_.device_name, config_.measure_interval_ms, config_.measure_offset_cm);
    Serial.printf("  -> Filtre: alpha=%.2f, N=%u, delay=%u ms, min=%.1f cm, max=%.1f cm\n",
                  config_.avg_alpha, config_.median_n, config_.median_delay_ms,
                  config_.filter_min_cm, config_.filter_max_cm);
    Serial.printf("  -> DeepSleep: %lu s, Timeout interactif: %lu ms\n",
                  (unsigned long)config_.deepsleep_interval_s,
                  (unsigned long)config_.interactive_timeout_ms);
    return true;
}

bool ConfigManager::save()
{
    std::lock_guard<std::mutex> lk(mutex_);
    Preferences prefs;
    if (!prefs.begin("config", false))
        return false;

    Serial.println("[ConfigManager] Sauvegarde dans Preferences...");

    // Wi-Fi
    prefs.putString("wifi_ssid", config_.wifi_ssid);
    prefs.putString("wifi_pass", config_.wifi_pass);

    // MQTT
    prefs.putBool("mqtt_en", config_.mqtt_enabled);
    prefs.putString("mqtt_host", config_.mqtt_host);
    prefs.putUShort("mqtt_port", config_.mqtt_port);
    prefs.putString("mqtt_user", config_.mqtt_user);
    prefs.putString("mqtt_pass", config_.mqtt_pass);
    prefs.putString("mqtt_topic", config_.mqtt_topic);

    // Mesure
    prefs.putUInt("meas_int_ms", config_.measure_interval_ms);
    prefs.putFloat("meas_off_cm", config_.measure_offset_cm);

    // NEW
    prefs.putFloat("avg_alpha", config_.avg_alpha);
    prefs.putUShort("median_n", config_.median_n);
    prefs.putUShort("median_delay_ms", config_.median_delay_ms);
    prefs.putFloat("f_min_cm", config_.filter_min_cm);
    prefs.putFloat("f_max_cm", config_.filter_max_cm);

    // Divers
    prefs.putString("dev_name", config_.device_name);
    prefs.putUInt("int_to_ms", config_.interactive_timeout_ms);
    prefs.putUInt("deep_int_s", config_.deepsleep_interval_s);

    prefs.putString("adm_user", config_.admin_user);
    prefs.putString("adm_pass", config_.admin_pass);
    prefs.putString("app_ver", config_.app_version);

    prefs.end();
    Serial.println(" Configuration sauvegardée avec succès !");
    return true;
}

String ConfigManager::toJsonString()
{
    std::lock_guard<std::mutex> lk(mutex_);
    JsonDocument doc;

    // Wi-Fi
    doc["wifi_ssid"] = config_.wifi_ssid;
    doc["wifi_pass"] = "*****"; // masqué

    // MQTT
    doc["mqtt_enabled"] = config_.mqtt_enabled;
    doc["mqtt_host"] = config_.mqtt_host;
    doc["mqtt_port"] = config_.mqtt_port;
    doc["mqtt_user"] = config_.mqtt_user;
    doc["mqtt_pass"] = "*****"; // masqué
    doc["mqtt_topic"] = config_.mqtt_topic;

    // Mesure
    doc["measure_interval_ms"] = config_.measure_interval_ms;
    doc["measure_offset_cm"] = config_.measure_offset_cm;

    // NEW
    doc["avg_alpha"] = config_.avg_alpha;
    doc["median_n"] = config_.median_n;
    doc["median_delay_ms"] = config_.median_delay_ms;
    doc["filter_min_cm"] = config_.filter_min_cm;
    doc["filter_max_cm"] = config_.filter_max_cm;

    // Divers
    doc["device_name"] = config_.device_name;
    doc["interactive_timeout_ms"] = config_.interactive_timeout_ms;
    doc["deepsleep_interval_s"] = config_.deepsleep_interval_s;

    doc["admin_user"] = config_.admin_user;
    doc["admin_pass"] = "*****"; // masqué
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
        std::lock_guard<std::mutex> lk(mutex_);

        // Wi-Fi
        if (doc["wifi_ssid"].is<const char *>())
            strlcpy(config_.wifi_ssid, doc["wifi_ssid"], sizeof(config_.wifi_ssid));
        if (doc["wifi_pass"].is<const char *>())
        {
            const char *wp = doc["wifi_pass"];
            if (wp && strcmp(wp, "*****") != 0 && strlen(wp) > 0)
                strlcpy(config_.wifi_pass, wp, sizeof(config_.wifi_pass));
        }

        // MQTT
        if (doc["mqtt_enabled"].is<bool>())
            config_.mqtt_enabled = doc["mqtt_enabled"].as<bool>();
        if (doc["mqtt_host"].is<const char *>())
            strlcpy(config_.mqtt_host, doc["mqtt_host"], sizeof(config_.mqtt_host));
        if (doc["mqtt_port"].is<uint16_t>())
            config_.mqtt_port = doc["mqtt_port"];
        if (doc["mqtt_user"].is<const char *>())
            strlcpy(config_.mqtt_user, doc["mqtt_user"], sizeof(config_.mqtt_user));
        if (doc["mqtt_pass"].is<const char *>())
        {
            const char *mp = doc["mqtt_pass"];
            if (mp && strcmp(mp, "*****") != 0 && strlen(mp) > 0)
                strlcpy(config_.mqtt_pass, mp, sizeof(config_.mqtt_pass));
        }
        if (doc["mqtt_topic"].is<const char *>())
            strlcpy(config_.mqtt_topic, doc["mqtt_topic"], sizeof(config_.mqtt_topic));

        // Mesure
        if (doc["measure_interval_ms"].is<uint32_t>())
            config_.measure_interval_ms = doc["measure_interval_ms"];
        if (doc["measure_offset_cm"].is<float>())
            config_.measure_offset_cm = doc["measure_offset_cm"];

        // NEW filtres
        if (doc["avg_alpha"].is<float>())
            config_.avg_alpha = doc["avg_alpha"];
        if (doc["median_n"].is<uint16_t>())
            config_.median_n = doc["median_n"];
        if (doc["median_delay_ms"].is<uint16_t>())
            config_.median_delay_ms = doc["median_delay_ms"];
        if (doc["filter_min_cm"].is<float>())
            config_.filter_min_cm = doc["filter_min_cm"];
        if (doc["filter_max_cm"].is<float>())
            config_.filter_max_cm = doc["filter_max_cm"];

        // Divers
        if (doc["device_name"].is<const char *>())
            strlcpy(config_.device_name, doc["device_name"], sizeof(config_.device_name));
        if (doc["interactive_timeout_ms"].is<uint32_t>())
            config_.interactive_timeout_ms = doc["interactive_timeout_ms"];
        if (doc["deepsleep_interval_s"].is<uint32_t>())
            config_.deepsleep_interval_s = doc["deepsleep_interval_s"];

        if (doc["admin_user"].is<const char *>())
            strlcpy(config_.admin_user, doc["admin_user"], sizeof(config_.admin_user));
        if (doc["admin_pass"].is<const char *>())
        {
            const char *ap = doc["admin_pass"];
            if (ap && strcmp(ap, "*****") != 0 && strlen(ap) > 0)
                strlcpy(config_.admin_pass, ap, sizeof(config_.admin_pass));
        }
    }

    Serial.println("  -> Mise à jour de la configuration en mémoire OK.");

    applyDefaultsIfNeeded();

    // Sauvegarde asynchrone
    xTaskCreate([](void *)
                {
        Serial.println("[ConfigManager][Task] Sauvegarde asynchrone en cours...");
        ConfigManager::instance().save();
        Serial.println("[ConfigManager][Task] Sauvegarde terminée !");
        vTaskDelete(NULL); },
                "saveConfigAsync", 4096, NULL, 1, NULL);

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

// NEW getters
float ConfigManager::getRunningAverageAlpha()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.avg_alpha;
}

uint16_t ConfigManager::getMedianSamples()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.median_n;
}

uint16_t ConfigManager::getMedianSampleDelayMs()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.median_delay_ms;
}

float ConfigManager::getFilterMinCm()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.filter_min_cm;
}

float ConfigManager::getFilterMaxCm()
{
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.filter_max_cm;
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
