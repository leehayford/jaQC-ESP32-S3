#include "model_config.h"
#include "util_flash.h"
#include "util_err.h"

#include "esp_check.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "MODEL_CFG";

extern nvs_handle_t s_cfg_nvs;

esp_err_t cfg_get(cfg_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ESP_OK;

    LOG_INFO(TAG, "retrieving configuration data from flash...");

    err = flash_get_str(s_cfg_nvs, "serial", out->serial, sizeof(out->serial));
    if (err != ESP_OK ) { 
        LOG_ERR(TAG, err, "failed to get serial"); 
        return err; 
    }

    FLASH_CHECK(s_cfg_nvs, "serial", out->serial);
    FLASH_CHECK(s_cfg_nvs, "hw_class", out->hw_class);
    FLASH_CHECK(s_cfg_nvs, "hw_version", out->hw_version);
    FLASH_CHECK(s_cfg_nvs, "fw_version", out->fw_version);
    FLASH_CHECK(s_cfg_nvs, "hw_prefix", out->hw_prefix);

    FLASH_CHECK(s_cfg_nvs, "wifi_ssid", out->wifi_ssid);
    FLASH_CHECK(s_cfg_nvs, "wifi_pass", out->wifi_pass);
    FLASH_CHECK(s_cfg_nvs, "ap_mode", &out->ap_mode); // bool

    FLASH_CHECK(s_cfg_nvs, "mqtt_uri", out->mqtt_uri);
    FLASH_CHECK(s_cfg_nvs, "mqtt_user", out->mqtt_user);
    FLASH_CHECK(s_cfg_nvs, "mqtt_port", &out->mqtt_port); // int
    FLASH_CHECK(s_cfg_nvs, "mqtt_pass", out->mqtt_pass);

    return ESP_OK;
}

esp_err_t cfg_set(const cfg_t *in) {
    if (!in) return ESP_ERR_INVALID_ARG;

    FLASH_TRY_SET(s_cfg_nvs, "serial", in->serial);
    FLASH_TRY_SET(s_cfg_nvs, "hw_class", in->hw_class);
    FLASH_TRY_SET(s_cfg_nvs, "hw_version", in->hw_version);
    FLASH_TRY_SET(s_cfg_nvs, "fw_version", in->fw_version);
    FLASH_TRY_SET(s_cfg_nvs, "hw_prefix", in->hw_prefix);

    FLASH_TRY_SET(s_cfg_nvs, "wifi_ssid", in->wifi_ssid);
    FLASH_TRY_SET(s_cfg_nvs, "wifi_pass", in->wifi_pass);
    FLASH_TRY_SET(s_cfg_nvs, "ap_mode", in->ap_mode); 

    FLASH_TRY_SET(s_cfg_nvs, "mqtt_uri", in->mqtt_uri);
    FLASH_TRY_SET(s_cfg_nvs, "mqtt_user", in->mqtt_user);
    FLASH_TRY_SET(s_cfg_nvs, "mqtt_port", in->mqtt_port); 
    FLASH_TRY_SET(s_cfg_nvs, "mqtt_pass", in->mqtt_pass);

    LOG_INFO(TAG, "commiting cfg->serial: %s", in->serial);
    return flash_commit(s_cfg_nvs);
}

void cfg_defaults(cfg_t *cfg) {
    memset(cfg, 0, sizeof(cfg_t)); // Clear everything

    strncpy(cfg->serial, DEF_SERIAL, sizeof(cfg->serial));
    strncpy(cfg->hw_class, DEF_CLASS, sizeof(cfg->hw_class));
    strncpy(cfg->hw_version, DEF_HW_VERSION, sizeof(cfg->hw_version));
    strncpy(cfg->fw_version, DEF_FW_VERSION, sizeof(cfg->fw_version));
    strncpy(cfg->hw_prefix, DEF_HW_PREFIX, sizeof(cfg->hw_prefix));

    strncpy(cfg->wifi_ssid, DEF_WIFI_SSID, sizeof(cfg->wifi_ssid));
    strncpy(cfg->wifi_pass, DEF_WIFI_PASS, sizeof(cfg->wifi_pass));
    cfg->ap_mode = DEF_AP_MODE;

    strncpy(cfg->mqtt_uri, DEF_MQTT_URI, sizeof(cfg->mqtt_uri));
    cfg->mqtt_port = DEF_MQTT_PORT;
    strncpy(cfg->mqtt_user, DEF_MQTT_USER, sizeof(cfg->mqtt_user));
    strncpy(cfg->mqtt_pass, DEF_MQTT_PASS, sizeof(cfg->mqtt_pass));
}

esp_err_t cfg_validate(cfg_t *cfg) {
    esp_err_t err = cfg_get(cfg);

    /* TODO: Much more thorough validation */
    if (err != ESP_OK || cfg->serial[0] == '\0') {
        LOG_INFO(TAG, "cfg->serial: %s", cfg->serial);
        LOG_INFO(TAG, "no valid config found; writing defaults");
        cfg_defaults(cfg);
        ESP_RETURN_ON_ERROR(cfg_set(cfg), TAG, "failed to write default config");
    } else {
        LOG_INFO(TAG, "config loaded successfully");
    }

    return ESP_OK;
}


