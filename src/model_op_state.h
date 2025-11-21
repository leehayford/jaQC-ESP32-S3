#ifndef MODEL_OPS_H
#define MODEL_OPS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "util_wifi.h"
#include "util_http.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    wifi_ui_state_t     wifi_state;  // UI state for HTTP/MQTT
    uint32_t            wifi_err;    // last reason code (WIFI_EVENT_STA_DISCONNECTED reason)
    uint32_t            ip_addr;     // v4 in network byte order (or store as char[16])
    uint8_t             bssid[6];    // optional: currently associated BSSID
    int8_t              rssi;        // current RSSI when connected (optional)
    portal_ui_phase_t   ui_phase;    // captive portal state connecting / connected to a wifi network
} ops_t;

esp_err_t ops_validate(ops_t *out);
esp_err_t ops_get(ops_t *out); // read current op state
esp_err_t ops_set(const ops_t *in); // write + commit
bool ops_get_ip(const ops_t *in, char *out[16]);

#ifdef __cplusplus
}
#endif

#endif // MODEL_OPS_H