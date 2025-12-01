
#include "util_net_events.h"
#include "util_err.h"

#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_log.h"
#include "esp_check.h"

#include <string.h>


ESP_EVENT_DEFINE_BASE(NET_EVENT);
static const char *TAG = "NET_EVENTS";

// State guards
static bool s_wifi_has_ip = false;
static bool s_eth_has_ip  = false;

// Optional: remember which netif is Wi-Fi STA and which is Ethernet
static esp_netif_t *s_wifi_sta_netif = NULL;
static esp_netif_t *s_eth_netif      = NULL;

bool net_events_wifi_has_ip(void) { return s_wifi_has_ip; }
bool net_events_eth_has_ip(void)  { return s_eth_has_ip;  }


static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    
    switch(id) {

        case WIFI_EVENT_STA_START: { 

            esp_event_post(NET_EVENT, NET_EVENT_WIFI_STA_START, NULL, 0, portMAX_DELAY);

            break;
        }

        case WIFI_EVENT_STA_CONNECTED: {

            esp_event_post(NET_EVENT, NET_EVENT_WIFI_STA_CONNECTED, 
                (wifi_event_sta_connected_t *)data, 
                sizeof(*(wifi_event_sta_connected_t *)data), 
                portMAX_DELAY);

            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED: {

            s_wifi_has_ip = false; // connectivity lost -> assume no IP
            
            esp_event_post(NET_EVENT, NET_EVENT_WIFI_STA_DISCONNECTED, 
                (wifi_event_sta_disconnected_t *)data, 
                sizeof(*(wifi_event_sta_disconnected_t *)data), 
                portMAX_DELAY);

            break;
        }

        case WIFI_EVENT_AP_START: {

            esp_event_post(NET_EVENT, WIFI_EVENT_AP_START,  NULL, 0, portMAX_DELAY);

            break;
        }

        case WIFI_EVENT_AP_STACONNECTED: {

            esp_event_post(NET_EVENT, NET_EVENT_WIFI_AP_STACONNECTED, 
                (wifi_event_ap_staconnected_t *)data, 
                sizeof(*(wifi_event_ap_staconnected_t *)data), 
                portMAX_DELAY);

            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {

            esp_event_pot(NET_EVENT, NET_EVENT_WIFI_AP_STADISCONNECTED, 
                (wifi_event_ap_stadisconnected_t *)data, 
                sizeof(*(wifi_event_ap_stadisconnected_t *)data), 
                portMAX_DELAY);

            break;
        }

        default: { /* TODO: Comment out after testing */
            LOG_INFO(TAG, "unhandled WIFI_EVENT id=%" PRId32, id);
            break;
        }

    }
}


static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    
    switch (id) {
        
        case IP_EVENT_STA_GOT_IP: {
            
            if (!s_wifi_has_ip) {

                s_wifi_has_ip = true;

                esp_event_post(NET_EVENT, NET_EVENT_WIFI_STA_GOT_IP, 
                    (ip_event_got_ip_t *)data,
                    sizeof(*(ip_event_got_ip_t *)data), 
                    portMAX_DELAY); 
            }
            break;
        } 
        case IP_EVENT_STA_LOST_IP: {
            
            if (s_wifi_has_ip) {

                s_wifi_has_ip = false;

                esp_event_post(NET_EVENT, NET_EVENT_WIFI_STA_LOST_IP, 
                    NULL, 0, portMAX_DELAY);
            }
            break;
        }
        default: { /* TODO: Comment out after testing */
            LOG_INFO(TAG, "unhandled IP_EVENT id=%" PRId32, id);
            break;
        }

    }
}

esp_err_t net_events_init_once(void) {
    LOG_INFO(TAG, "checking tcp/ip stack & default event loop init status...");
    
    esp_err_t err = ESP_OK;
    
    // Ensure we call the following only once
    static bool initialized = false;

    if (initialized) {

        LOG_INFO(TAG, "already initialized");

    } else {  

        err = esp_netif_init();
        if (err != ESP_OK ) {
            LOG_ERR(TAG, err, "failed to initialize TCP/IP stack: ");
            return err;
        }
        LOG_INFO( TAG, "TCP/IP stack initialized");

        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            LOG_ERR(TAG, err, "failed to create event loop: ");
            return err;
        }
        LOG_INFO( TAG, "default event loop created");

        // call wifi_event_handler for every WIFI_EVENT (see wifi_event_t in esp_wifi_types_generic.h)
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
        if (err != ESP_OK ) {
            LOG_ERR(TAG, err, "failed to register wifi event handler: ");
            return err;
        }
        LOG_INFO( TAG, "wifi event handler ergistered");

        // call ip_event_handler for every IP_EVENT (see ip_event_t in esp_netif_types.h) 
        err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler, NULL);
        if (err != ESP_OK ) {
            LOG_ERR(TAG, err, "failed to register ip event handler: ");
            return err;
        }
        LOG_INFO( TAG, "ip event handler registered");

        initialized = true;
    }

    return err;
}
