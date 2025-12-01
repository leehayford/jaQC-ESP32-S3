#include "model_op_state.h"
#include "util_flash.h"
#include "util_err.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <string.h>

#include "esp_netif.h"  // for IPSTR, IP2STR
#include "lwip/ip4_addr.h"   // for ip4_addr_t

static const char *TAG = "MODEL_OPS";

extern nvs_handle_t s_ops_nvs;

esp_err_t ops_get(ops_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;

    LOG_INFO(TAG, "retrieving operational state data from flash...");
    
    // FLASH_CHECK(s_ops_nvs, "wifi_state", &out->wifi_state);
    FLASH_CHECK(s_ops_nvs, "wifi_err", &out->wifi_err);
    FLASH_CHECK(s_ops_nvs, "ip_addr", &out->ip_addr);
    FLASH_CHECK(s_ops_nvs, "bssid", out->bssid);
    FLASH_CHECK(s_ops_nvs, "rssi", &out->rssi);

    return ESP_OK;
}

// bool ops_get_ip(ops_t *in, char *out[16]) {
    
//     if (in->ip_addr) {
//         snprintf(&out, sizeof(out), IPSTR, IP2STR((const ip4_addr_t*)in->ip_addr));
//         return true;
//     }
//     return false;
// }