#include "util_wifi.h"
#include "util_err.h"
#include "util_net_events.h"
#include "util_device.h"
#include "util_dns.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_check.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "cJSON.h"

static const char *TAG = "UTIL_WIFI";

static QueueHandle_t s_netq = NULL;
static TaskHandle_t s_wifi_worker = NULL;
static wifi_config_t s_ap_conf;
static wifi_config_t s_sta_conf;
static char s_ssid[64] = {0};
static char s_pass[64] = {0};
static esp_netif_ip_info_t s_ip;

static volatile wifi_ui_state_t s_state = WIFI_UI_IDLE;
static portal_ui_phase_t s_phase = PORTAL_SELECT;

// Access point shutdown grace period timer, started upon NET_EVENT_WIFI_STA_GOT_IP
static esp_timer_handle_t s_ap_grace_timer = NULL; 

const char* get_wifi_ssid() { return s_ssid; }
const char* get_wifi_pass() { return s_pass; }
esp_err_t   wifi_get_ip_str(char *out, size_t out_sz) {
    if (!out || out_sz < 16 || s_ip.ip.addr == 0) return ESP_FAIL;
    snprintf(out, out_sz, IPSTR, IP2STR(&s_ip.ip));
    return ESP_OK;
}

wifi_ui_state_t get_wifi_state() { return s_state; }
wifi_ui_state_t set_wifi_state(wifi_ui_state_t s) { s_state = s; return s_state; }

portal_ui_phase_t get_portal_phase() { return s_phase; }
portal_ui_phase_t set_portal_phase(portal_ui_phase_t p) { s_phase = p; return s_phase; }



const char *wifi_state_to_str(wifi_ui_state_t s) { // LOG_INFO(TAG, "wfif_state_to_str(%d)", s);
    switch (s) {
        case WIFI_UI_IDLE:          return "idle";
        case WIFI_UI_CONNECTING:    return "connecting";
        case WIFI_UI_CONNECTED:     return "connected";
        case WIFI_UI_FAILED:        return "failed";
        default:                    return "unknown";
    }
}

const char *portal_phase_to_str(portal_ui_phase_t s) { // LOG_INFO(TAG, "portal_ui_phase_t(%d)", s);
    switch (s) {
        case PORTAL_SELECT:         return "select";
        case PORTAL_CONNECTING:     return "connecting";
        case PORTAL_CONNECTED:      return "connected";
        case PORTAL_STA_MODE:       return "STA mode";
        case PORTAL_ERROR:          return "error";
        case PORTAL_IDLE:           return "idle";
        default:                    return "unknown";
    }
}

static const char* authmode_to_str(wifi_auth_mode_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN: return "open";
        case WIFI_AUTH_WEP: return "wep";
        case WIFI_AUTH_WPA_PSK: return "wpa-psk";
        case WIFI_AUTH_WPA2_PSK: return "wpa2-psk";
        case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2-psk";
        case WIFI_AUTH_WPA3_PSK: return "wpa3-psk";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3-psk";
        case WIFI_AUTH_WAPI_PSK: return "wapi-psk";
        default: return "unknown";
    }
}

/* WIFI WORKER TASK ***********************************************************************
 This task does all of the heavy lifting for NET_EVENTs, timers, interrupts
 - Register handlers (callback) for NET_EVENTs, timers, interrupts 
 - Handlers post to our queue 's_netq' 
 - No heavy lifting in handlers (will cause stak overflow)
 - Call heavy functions in corresponding case */
static void wifi_worker_task(void *arg) {
    
    net_work_msg_t msg;
    esp_err_t err = ESP_OK;

    for (;;) {

        if (xQueueReceive(s_netq, &msg, portMAX_DELAY)) {

            switch (msg.cmd) {
                
                // Heavy operation done here, not in event/timer callback
                case NET_WORK_AP_DISABLE: {
                    
                    LOG_INFO(TAG, "disabling access point...");

                    wifi_mode_t mode;
                    err = esp_wifi_get_mode(&mode);
                    if (err != ESP_OK) {
                         LOG_ERR(TAG, err, "get current wifi mode failed:");
                    } 
                    else if (mode == WIFI_MODE_STA) {
                        LOG_INFO(TAG, "AP already disabled (STA-only mode)");
                    } else {
                        err = esp_wifi_set_mode(WIFI_MODE_STA);
                        if (err != ESP_OK) LOG_ERR(TAG, err, "set wifi mode to STA failed:");
                        else {
                            LOG_INFO(TAG, "AP disabled (STA-only mode enabled)");
                            set_portal_phase(PORTAL_STA_MODE);
                        }
                    }
                    break;
                }

                case NET_WORK_AP_ENABLE: {
                    
                    LOG_INFO(TAG, "enabling access point...");
                    
                    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
                    if (err != ESP_OK) LOG_ERR(TAG, err, "set wifi mode to AP+STA failed:");
                    else LOG_INFO(TAG, "access point enabled (AP+STA)");

                    break;
                }

                case NET_WORK_CONNECT: {
                    
                    LOG_INFO(TAG, "trying wifi connect...");
                    
                    err = esp_wifi_connect();
                    if (err != ESP_OK) LOG_ERR(TAG, err, "wifi connect attempt failed:");
                    else LOG_INFO(TAG, "wifi connect attempt success");

                    break;
                }

                case NET_WORK_START: {
                    
                    LOG_INFO(TAG, "starting wifi...");
                    
                    err = esp_wifi_start();
                    if (err != ESP_OK) LOG_ERR(TAG, err, "wifi start attempt failed:");
                    else LOG_INFO(TAG, "wifi started");

                    break;
                }

                case NET_WORK_STOP: {
                    
                    LOG_INFO(TAG, "stopping wifi...");
                    
                    err = esp_wifi_stop();
                    if (err != ESP_OK) LOG_ERR(TAG, err, "wifi stop attempt failed:");
                    else LOG_INFO(TAG, "wifi stopped");

                    break;
                }

                case NET_WORK_SET_CONFIG_AP: { 
                    
                    LOG_INFO(TAG, "configuring wifi access point...");
                    
                    if (msg.arg) {
                        wifi_config_t *cfg = (wifi_config_t *)msg.arg;
                        err = esp_wifi_set_config(WIFI_IF_AP, cfg);
                    } 
                    if (err != ESP_OK) LOG_ERR(TAG, err, "wifi access point config attempt failed:");
                    else LOG_INFO(TAG, "wifi access point configured");

                    break;
                }

                case NET_WORK_SET_CONFIG_STA: {
                    
                    LOG_INFO(TAG, "configuring wifi for station mode...");
                    
                    if (msg.arg) {
                        wifi_config_t *cfg = (wifi_config_t *)msg.arg;
                        err = esp_wifi_set_config(WIFI_IF_STA, cfg); 
                    }
                    if (err != ESP_OK) LOG_ERR(TAG, err, "wifi access point config attempt failed:");
                    else LOG_INFO(TAG, "wifi configured for station mode");

                    break;
                }
                
                default: 
                    // log line for testing case block size on stack 
                    // LOG_WARN(TAG, ESP_OK, "Disable AP: HighWater sys_evt: %u", uxTaskGetStackHighWaterMark(NULL));
                    break;
            }
        }
    }
}

void wifi_worker_init(void) {
    s_netq = xQueueCreate(8, sizeof(net_work_msg_t));
    xTaskCreate(wifi_worker_task, "wifi_worker", 4096, NULL, 4, &s_wifi_worker);
}

bool wifi_worker_post(net_work_cmd_t cmd, void *arg) {
    net_work_msg_t msg = { 
        .cmd = cmd,
        .arg = arg
    };
    return xQueueSend(s_netq, &msg, 0) == pdTRUE;
}

/* END -> WIFI WORKER TASK STUFF *********************************************************/



char* wifi_scan_json(void) {
    wifi_scan_config_t cfg = {
        .ssid = NULL, .bssid = NULL, .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = { .min = 100, .max = 300 },
    };
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) return NULL;

    uint16_t num = 0;
    if (esp_wifi_scan_get_ap_num(&num) != ESP_OK) return NULL;

    wifi_ap_record_t *aps = (wifi_ap_record_t*)calloc(num ? num : 1, sizeof(*aps));
    if (!aps) return NULL;

    if (esp_wifi_scan_get_ap_records(&num, aps) != ESP_OK) {
        free(aps);
        return NULL;
    }

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < num; ++i) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", (const char*)aps[i].ssid);
        char bssid[18];
        snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
                 aps[i].bssid[0], aps[i].bssid[1], aps[i].bssid[2],
                 aps[i].bssid[3], aps[i].bssid[4], aps[i].bssid[5]);
        cJSON_AddStringToObject(o, "bssid", bssid);
        cJSON_AddNumberToObject(o, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(o, "chan", aps[i].primary);
        cJSON_AddStringToObject(o, "auth", authmode_to_str(aps[i].authmode));
        cJSON_AddBoolToObject(o, "open", aps[i].authmode == WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr, o);
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    free(aps);
    return json; // caller frees
}

static void make_ap_ssid(char prefix[9], char out[33]) {
    char mac6[13]; make_mac6(mac6);
    snprintf(out, 33, "%s-%s", prefix, mac6);
}

ip4_addr_t wifi_get_ap_ip(void) {
    ip4_addr_t zero = { .addr = 0 };
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap) return zero;

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(ap, &ip) != ESP_OK) return zero;

    ip4_addr_t lw = { .addr = ip.ip.addr };  // convert esp_ip4_addr_t -> lwIP ip4_addr_t
    return lw;
}

/* TRY STATION MODE / FAIL OUT TO ACCESS POINT MODE ********************************/

esp_err_t wifi_start_ap(char *prefix) {

    wifi_worker_post(NET_WORK_AP_ENABLE, NULL);

    char ssid[33] = "";
    make_ap_ssid(prefix, ssid); 

    wifi_config_t ap_conf = {0};
    ap_conf.ap.ssid_len = strlen(ssid); 
    strncpy((char *)ap_conf.ap.ssid, ssid, sizeof(ap_conf.ap.ssid));
    ap_conf.ap.channel = 1; 
    ap_conf.ap.max_connection = 4; 
    ap_conf.ap.authmode = WIFI_AUTH_OPEN;

    s_ap_conf = ap_conf; // copy to static/global
    wifi_worker_post(NET_WORK_SET_CONFIG_AP, &s_ap_conf);

    return ESP_OK;
}

esp_err_t wifi_start_sta(const char *ssid, const char *pass) {

    wifi_worker_post(NET_WORK_AP_ENABLE, NULL); // Keep AP active during connect

    wifi_config_t sta_conf = {0};
    strncpy((char *)sta_conf.sta.ssid, ssid, sizeof(sta_conf.sta.ssid));
    strncpy((char *)sta_conf.sta.password, pass, sizeof(sta_conf.sta.password));

    strncpy(s_ssid, ssid, sizeof(s_ssid)); 
    strncpy(s_pass, pass, sizeof(s_pass));  

    s_sta_conf = sta_conf;
    wifi_worker_post(NET_WORK_SET_CONFIG_STA, &s_sta_conf);
    
    wifi_worker_post(NET_WORK_CONNECT, NULL);

    set_wifi_state(WIFI_UI_CONNECTING); 

    return ESP_OK;
}



/* log line for testing stack impact of handlers 
LOG_WARN(TAG, ESP_OK, "IP EVENT: HighWater sys_evt: %u", uxTaskGetStackHighWaterMark(NULL));
*/

/* STA event handlers **************************************************************/
static void on_wifi_sta_start(void *arg, esp_event_base_t base, int32_t id, void *data) {
    LOG_INFO(TAG, "STA started");
}

static void on_wifi_sta_connected(void *arg, esp_event_base_t base, int32_t id, void *data) {
    LOG_INFO(TAG, "STA connected (waiting for IP)");
}

static void on_wifi_sta_disconnected(void* arg, esp_event_base_t base, int32_t id, void *data) {
    
    if (esp_timer_is_active(s_ap_grace_timer)) {
        // stop s_ap_grace_timer immediately to ensure access point remains active 
        ESP_ERROR_CHECK(esp_timer_stop(s_ap_grace_timer));
        LOG_WARN(TAG, ESP_FAIL, "unexpected STA disconnect");
    }

    wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
    LOG_INFO(TAG, 
        "STA disconnected -> reason: %d (see: esp_wifi_types_generic.h -> wifi_err_reason_t)", 
        e->reason
    );

    if (e->reason == WIFI_REASON_AUTH_FAIL 
    ||  e->reason == WIFI_REASON_HANDSHAKE_TIMEOUT
    ||  e->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
        // Wrong password or auth failure
        set_wifi_state(WIFI_UI_FAILED);
        set_portal_phase(PORTAL_ERROR);
        // s_last_err = WIFI_ERR_AUTH_FAIL;
    } else if (e->reason == WIFI_REASON_NO_AP_FOUND) {
        set_wifi_state(WIFI_UI_FAILED);
        set_portal_phase(PORTAL_ERROR);
        // s_last_err = WIFI_ERR_NO_AP_FOUND;
    } else {
        set_wifi_state(WIFI_UI_DISCONNECTED);
        set_portal_phase(PORTAL_IDLE);
        // s_last_err = WIFI_ERR_UNKNOWN;
    }
    wifi_worker_post(NET_WORK_CONNECT, NULL); // auto-reconnect
}

// AP shutdown grace period timer callback; 
static void ap_grace_timer_cb(void *arg) {
    // Post to worker queue, shutdown AP / enable STA only mode
    LOG_INFO(TAG, "AP shutdown grace period expired → disabling AP...");
    wifi_worker_post(NET_WORK_AP_DISABLE, NULL);
}

static void on_wifi_sta_got_ip(void* arg, esp_event_base_t base, int32_t id, void *data) {
    
    ip_event_got_ip_t *e = (ip_event_got_ip_t*)data;
    s_ip = e->ip_info;

    LOG_INFO(TAG, "STA IP: \x1b[38;5;200m" IPSTR "\x1b[0m  GW: " IPSTR "  MASK: " IPSTR,
        IP2STR(&e->ip_info.ip),
        IP2STR(&e->ip_info.gw),
        IP2STR(&e->ip_info.netmask)
    );

    set_wifi_state(WIFI_UI_CONNECTED);
    set_portal_phase(PORTAL_CONNECTED);

    LOG_INFO(TAG, "STA got IP → starting AP shutdown grace period (%ums)", AP_GRACE_ms);
    ESP_ERROR_CHECK(esp_timer_start_once(s_ap_grace_timer, AP_GRACE_ms * 1000));
}

static void on_wifi_status_served(void *arg, esp_event_base_t base, int32_t id, void *data) {
    // If AP grace timer is active, shorten it; no use standin' about like one o'clock half struck
    if (esp_timer_is_active(s_ap_grace_timer)) {
        LOG_INFO(TAG, "Status served → shortening AP shutdown grace period to %ums", AP_GRACE_SHORT_ms);
        ESP_ERROR_CHECK(esp_timer_stop(s_ap_grace_timer));
        ESP_ERROR_CHECK(esp_timer_start_once(s_ap_grace_timer, AP_GRACE_SHORT_ms * 1000)); // short tail
    }
}


/* AP event handlers ***************************************************************/
static void on_wifi_ap_start(void *arg, esp_event_base_t base, int32_t id, void *data) {
    LOG_INFO(TAG, "AP started");
    ip4_addr_t ap = wifi_get_ap_ip();
    LOG_INFO(TAG, "Software Access Point started at \x1b[38;5;200m %s \x1b[0m", ip4addr_ntoa(&ap)); 
}

static void on_wifi_ap_staconnected(void *arg, esp_event_base_t base, int32_t id, void *data) {
    wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
    LOG_INFO(TAG, "STA joined: " MACSTR, MAC2STR(e->mac));
}

static void on_wifi_ap_stadisconnected(void *arg, esp_event_base_t base, int32_t id, void *data) {
    wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
    LOG_INFO(TAG, "STA left: " MACSTR, MAC2STR(e->mac));
}



esp_err_t wifi_init(char prefix[10], char ssid[33], char pass[65]) {
    
    // Ensure TCP/IP stack & default event loop are initialized
    ESP_ERROR_CHECK(net_events_init_once());
    LOG_INFO(TAG, "tcp/ip stack & default event loop init status OK");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    LOG_INFO(TAG, "wifi driver resources allocated OK");

    esp_netif_create_default_wifi_ap();
    LOG_INFO(TAG, "wifi default access point created OK");

    esp_netif_create_default_wifi_sta();
    LOG_INFO(TAG, "wifi default station created OK");

    wifi_worker_init();

    esp_timer_create_args_t targs = {
        .callback = ap_grace_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ap_grace"
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_ap_grace_timer));

    // NET_EVENT handlers
    net_events_subscribe(NET_EVENT_WIFI_STA_START, on_wifi_sta_start, NULL);
    net_events_subscribe(NET_EVENT_WIFI_STA_CONNECTED, on_wifi_sta_connected, NULL);
    net_events_subscribe(NET_EVENT_WIFI_STA_DISCONNECTED, on_wifi_sta_disconnected, NULL);
    net_events_subscribe(NET_EVENT_WIFI_STA_GOT_IP, on_wifi_sta_got_ip, NULL);
    net_events_subscribe(NET_EVENT_WIFI_STATUS_SERVED, on_wifi_status_served, NULL);
    
    net_events_subscribe(NET_EVENT_WIFI_AP_START, on_wifi_ap_start, NULL);
    net_events_subscribe(NET_EVENT_WIFI_AP_STACONNECTED, on_wifi_ap_staconnected, NULL);
    net_events_subscribe(NET_EVENT_WIFI_AP_STADISCONNECTED, on_wifi_ap_stadisconnected, NULL);

    ESP_ERROR_CHECK(wifi_start_ap(prefix));
    start_dns_server();

    bool have_sta = (ssid[0] != '\0');
    if (have_sta) {
        ESP_ERROR_CHECK(wifi_start_sta(ssid, pass));
    } else  {
        LOG_WARN(TAG, ESP_FAIL, "STA failed or timed out, keeping AP active");
    }
    
    // Start Wi-Fi after both configs applied
    wifi_worker_post(NET_WORK_START, NULL); // ESP_ERROR_CHECK(esp_wifi_start());
   
    return ESP_OK;
}





