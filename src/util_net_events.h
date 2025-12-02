#ifndef UTIL_NET_EVENTS_H
#define UTIL_NET_EVENTS_H

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

// Unified network event base for your app
ESP_EVENT_DECLARE_BASE(NET_EVENT);

/* This enum is unifies all of our network type events: WiFi, IP, ethernet, MQTT
   We register handlers for a range of ESP event typs and "translate" to 
   one of our own NET_EVENTs
*/
typedef enum {
// *** Wi-Fi lifecycle ***********************

    // - WIFI_EVENT_STA_START ->  NULL -> esp_wifi_types_generic.h
    NET_EVENT_WIFI_STA_START = 1,

    // - WIFI_EVENT_STA_STOP -> NULL -> esp_wifi_types_generic.h
    NET_EVENT_WIFI_STA_STOP,

    // - WIFI_EVENT_STA_CONNECTED ->  wifi_event_sta_connected_t -> esp_wifi_types_generic.h
    NET_EVENT_WIFI_STA_CONNECTED,

    // - IP_EVENT_STA_GOT_IP -> ip_event_got_ip_t -> esp_netif_types.h
    NET_EVENT_WIFI_STA_GOT_IP,

    // - IP_EVENT_STA_LOST_IP -> NULL -> esp_netif_types.h
    NET_EVENT_WIFI_STA_LOST_IP,

    // - WIFI_EVENT_STA_DISCONNECTED -> wifi_event_sta_disconnected_t -> esp_wifi_types_generic.h
    NET_EVENT_WIFI_STA_DISCONNECTED,

    // - WIFI_EVENT_AP_START -> NULL -> esp_wifi_types_generic.h
    NET_EVENT_WIFI_AP_START,

    // - WIFI_EVENT_AP_STACONNECTED -> wifi_event_ap_staconnected_t -> esp_wifi_types_generic.h
    NET_EVENT_WIFI_AP_STACONNECTED,

    // - WIFI_EVENT_AP_STADISCONNECTED - wifi_event_ap_stadisconnected_t -> esp_wifi_types_generic.h
    NET_EVENT_WIFI_AP_STADISCONNECTED,

    // - WIFI_EVENT_HOME_CHANNEL_CHANGE - wifi_event_home_channel_change_t -> esp_wifi_types_generic.h
    NET_EVENT_WIFI_HOME_CHANNEL_CHANGE,

// *** Ethernet lifecycle ***********************
    
    // NOT IMPLEMENTED - ??? -> NULL -> esp_association_unknown
    NET_EVENT_ETH_STARTED,
    
    // NOT IMPLEMENTED - ??? -> NULL -> esp_association_unknown
    NET_EVENT_ETH_CONNECTED,

    // NOT IMPLEMENTED - IP_EVENT_ETH_GOT_IP -> ip_event_got_ip_t -> esp_netif_types.h
    NET_EVENT_ETH_GOT_IP,
    
    // NOT IMPLEMENTED - IP_EVENT_ETH_LOST_IP -> NULL -> esp_netif_types.h
    NET_EVENT_ETH_LOST_IP,

    // NOT IMPLEMENTED - ??? -> NULL -> esp_association_unknown
    NET_EVENT_ETH_DISCONNECTED,


// *** Optional: app-layer events *****************
    
    // NOT IMPLEMENTED - ORIGIN -> NULL -> esp_association_none
    NET_EVENT_TIME_SYNC_OK,
    
    // NOT IMPLEMENTED - ORIGIN -> NULL -> esp_association_none
    NET_EVENT_TIME_SYNC_FAIL,
    
    // NOT IMPLEMENTED - ORIGIN -> NULL -> esp_association_none
    NET_EVENT_MQTT_START,
    
    // NOT IMPLEMENTED - ORIGIN -> NULL -> esp_association_none
    NET_EVENT_MQTT_CONNECTED,
    
    // NOT IMPLEMENTED - ORIGIN -> NULL -> esp_association_none
    NET_EVENT_MQTT_DISCONNECTED,
    
    // NOT IMPLEMENTED - ORIGIN -> NULL -> esp_association_none
    NET_EVENT_MQTT_RECONNECTING,

} net_event_id_t;

// Ensure default event loop exists and register system handlers once.
esp_err_t net_events_init_once(void);

// Optional: expose current known state (useful for modules to “catch up”)
bool net_events_wifi_has_ip(void);
bool net_events_eth_has_ip(void);

// Subscribe convenience (idempotent register is handled inside esp_event)
static inline esp_err_t net_events_subscribe(int32_t event_id, esp_event_handler_t handler, void *arg) {
    return esp_event_handler_register(NET_EVENT, event_id, handler, arg);
}

// Unsubscribe convenience
static inline esp_err_t net_events_unsubscribe(int32_t event_id, esp_event_handler_t handler) {
    return esp_event_handler_unregister(NET_EVENT, event_id, handler);
}


#ifdef __cplusplus
}
#endif

#endif // UTIL_NET_EVENTS_H