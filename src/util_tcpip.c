#include "util_tcpip.h"
#include "util_err.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_check.h"

static const char *TAG = "UTIL_TCPIP";

esp_err_t tcpip_init_once(void) {
    // Ensure we call the following only once
    static bool initialized = false;
    if (initialized) {
        LOG_INFO(TAG, "already initialized");
    } else {    
        ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "failed to initialize TCP/IP stack");
        LOG_INFO( TAG, "TCP/IP stack initialized");
        ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "failed to create default event loop");
        LOG_INFO( TAG, "default event loop created");
        initialized = true;
    }
    return ESP_OK;
}