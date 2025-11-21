#ifndef MODEL_CFG_H
#define MODEL_CFG_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEF_SERIAL "0000000000"
#define DEF_CLASS "000"
#define DEF_HW_VERSION "000"
#define DEF_FW_VERSION "9999.9999.9999"
#define DEF_HW_PREFIX "JaQC"
#define DEF_WIFI_SSID ""
#define DEF_WIFI_PASS ""
#define DEF_AP_MODE true
#define DEF_MQTT_URI "mqtt://broker.local"
#define DEF_MQTT_PORT (int32_t)1883
#define DEF_MQTT_USER ""
#define DEF_MQTT_PASS ""

typedef struct {
    char serial[11];
    char hw_class[4];
    char hw_version[4];
    char fw_version[15];
    char hw_prefix[10];

    char wifi_ssid[33];
    char wifi_pass[65];
    bool ap_mode;

    char mqtt_uri[128];
    int32_t mqtt_port;
    char mqtt_user[64];
    char mqtt_pass[64];

} cfg_t;

esp_err_t cfg_validate(cfg_t *out);
esp_err_t cfg_get(cfg_t *out); // read current config
esp_err_t cfg_set(const cfg_t *in); // write + commit

void cfg_defaults(cfg_t *cfg);

// MQTT stuff
// esp_err_t cfg_set_mqtt(const char *ssid, const char *pass);
// esp_err_t cfg_clear_mqtt(void);


#ifdef __cplusplus
}
#endif

#endif // MODEL_CFG_H