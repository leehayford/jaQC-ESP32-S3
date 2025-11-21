#ifndef UTIL_WIFI_H
#define UTIL_WIFI_H

#include <stdbool.h>
#include "esp_err.h"
#include "lwip/ip4_addr.h"   // for ip4_addr_t

#ifdef __cplusplus
extern "C" {
#endif

// Bits for our local eventgroup
#define     WIFI_BIT_STA_GOT_IP     (1<<0)
#define     WIFI_BIT_STA_FAILED     (1<<1)
#define     WIFI_WAIT_FOR_IP_ms     5000

typedef enum {
    WIFI_UI_IDLE = 0,
    WIFI_UI_CONNECTING,
    WIFI_UI_CONNECTED,
    WIFI_UI_DISCONNECTED,
    WIFI_UI_FAILED
} wifi_ui_state_t;
wifi_ui_state_t     get_wifi_state(void);
wifi_ui_state_t     set_wifi_state(wifi_ui_state_t s);
const char*         wifi_state_to_str(wifi_ui_state_t s);

const char*         get_wifi_ssid(void);
const char*         get_wifi_pass(void);

typedef enum {
    PORTAL_SELECT = 0,     // list SSIDs
    PORTAL_CONNECTING,     // show "Connecting..." page
    PORTAL_CONNECTED,      // show IP + instructions
    PORTAL_ERROR,          // wrong password and shit...
    PORTAL_IDLE
} portal_ui_phase_t;
portal_ui_phase_t   get_portal_phase(void);
portal_ui_phase_t   set_portal_phase(portal_ui_phase_t p);
const char*         portal_phasse_to_str(portal_ui_phase_t p);


typedef enum {
    WIFI_ERR_NONE = 0,
    WIFI_ERR_AUTH_FAIL,
    WIFI_ERR_NO_AP_FOUND,
    WIFI_ERR_UNKNOWN
} wifi_err_t;
const char*         wifi_err_to_str(wifi_err_t e);

typedef enum {
    NET_WORK_AP_DISABLE,
    NET_WORK_AP_ENABLE,
    NET_WORK_RETRY
} net_work_cmd_t;

typedef struct {
    net_work_cmd_t cmd;
    void *arg;
} net_work_msg_t;



esp_err_t       wifi_init(char prefix[10], char ssid[33], char pass[65]);
esp_err_t       wifi_start_sta(const char *ssid, const char *pass);
esp_err_t       wifi_switch_to_sta(void);
esp_err_t       wifi_disable_ap_if_sta_connected(uint32_t timeout_ms);
ip4_addr_t      wifi_get_ap_ip(void);

// New façade used by HTTP
char*           wifi_scan_json(void);          // returns malloc'ed JSON (caller frees)
esp_err_t       wifi_get_ip_str(char *out, size_t out_sz);
esp_err_t       wifi_last_err(void);

#ifdef __cplusplus
}
#endif

#endif // UTIL_WIFI_H