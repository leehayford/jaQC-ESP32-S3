#include "util_wifi.h"
#include "util_err.h"
#include "util_device.h"
#include "util_tcpip.h"
#include "util_dns.h"
#include "util_http.h"
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
static TaskHandle_t s_net_worker = NULL;

static esp_timer_handle_t s_ap_grace_timer;


static EventGroupHandle_t s_wifi_ev;
static bool s_sw_ap = false;
static esp_netif_ip_info_t s_ip;

static char s_ssid[64] = {0}, s_pass[64] = {0};
static volatile wifi_ui_state_t s_state = WIFI_UI_IDLE;
static wifi_err_t s_last_err = WIFI_ERR_NONE;

static portal_ui_phase_t s_phase = PORTAL_SELECT;

const char* get_wifi_ssid() { return s_ssid; }
const char* get_wifi_pass() { return s_pass; }
esp_err_t wifi_last_err() { return s_last_err; }

wifi_ui_state_t get_wifi_state() { return s_state; }
wifi_ui_state_t set_wifi_state(wifi_ui_state_t s) { s_state = s; return s_state; }

portal_ui_phase_t get_portal_phase() { return s_phase; }
portal_ui_phase_t set_portal_phase(portal_ui_phase_t p) { s_phase = p; return s_phase; }

esp_err_t   wifi_get_ip_str(char *out, size_t out_sz) {
    if (!out || out_sz < 16 || s_ip.ip.addr == 0) return ESP_FAIL;
    snprintf(out, out_sz, IPSTR, IP2STR(&s_ip.ip));
    return ESP_OK;
}


const char *wifi_state_to_str(wifi_ui_state_t s) {
    LOG_INFO(TAG, "wfif_state_to_str(%d)", s);
    switch (s) {
        case WIFI_UI_IDLE:          return "idle";
        case WIFI_UI_CONNECTING:    return "connecting";
        case WIFI_UI_CONNECTED:     return "connected";
        case WIFI_UI_FAILED:        return "failed";
        default:                    return "unknown";
    }
}

const char *portal_phase_to_str(portal_ui_phase_t p) {
    switch (p) {
        case PORTAL_SELECT:         return "select";
        case PORTAL_CONNECTING:     return "connecting";
        case PORTAL_CONNECTED:      return "connected";
        default:                    return "unknown";
    }
}

const char *wifi_err_to_str(wifi_err_t s) {
    switch (s) {
        case WIFI_ERR_NONE:         return "none";
        case WIFI_ERR_AUTH_FAIL:    return "auth failed";
        case WIFI_ERR_NO_AP_FOUND:  return "access point not found";
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


/* WIFI WORKER TASK STUFF ****************************************************************/
static void net_worker_task(void *arg) {
    net_work_msg_t msg;
    esp_err_t err = ESP_OK;
    for (;;) {
        if (xQueueReceive(s_netq, &msg, portMAX_DELAY)) {
            switch (msg.cmd) {
                case NET_WORK_AP_DISABLE: // Heavy operation done here, not in event/timer callback
                    LOG_INFO(TAG, "disabling access point...");
                    err = esp_wifi_set_mode(WIFI_MODE_STA);
                    if (err != ESP_OK) 
                        LOG_ERR(TAG, err, "set wifi mode to STA failed:");
                    else 
                        LOG_INFO(TAG, "access point disabled (station-only mode enabled)");
                    break;
                case NET_WORK_AP_ENABLE:
                    LOG_INFO(TAG, "enabling access point...");
                    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
                    if (err != ESP_OK) 
                        LOG_ERR(TAG, err, "set wifi mode to AP+STA failed:");
                    else 
                        LOG_INFO(TAG, "access point enabled (AP+STA)");
                    break;
                case NET_WORK_RETRY:
                    esp_wifi_connect();
                    break;
                default:
                    break;
            }
        }
    }
}


void net_worker_init(void) {
    s_netq = xQueueCreate(8, sizeof(net_work_msg_t));
    xTaskCreate(net_worker_task, "net_worker", 4096, NULL, 4, &s_net_worker);
}

bool net_worker_post(net_work_cmd_t cmd) {
    net_work_msg_t msg = { .cmd = cmd };
    return xQueueSend(s_netq, &msg, 0) == pdTRUE;
}

static void ap_grace_timer_cb(void *arg) {
    // Do NOT call esp_wifi_set_mode() here!
    net_worker_post(NET_WORK_AP_DISABLE);
}

esp_err_t wifi_init_grace_timer(void) {
    const esp_timer_create_args_t targs = {
        .callback = &ap_grace_timer_cb,
        .arg = NULL,
        .name = "ap_grace_timer"
    };
    return esp_timer_create(&targs, &s_ap_grace_timer);
}

void wifi_start_ap_grace_period(uint64_t us) {
    if (esp_timer_is_active(s_ap_grace_timer)) {
        ESP_ERROR_CHECK(esp_timer_stop(s_ap_grace_timer));
    }
    ESP_ERROR_CHECK(esp_timer_start_once(s_ap_grace_timer, us));
}

/* END -> WIFI WORKER TASK STUFF *********************************************************/

static void on_ip_event(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t*)data;

        s_ip = e->ip_info;
        set_wifi_state(WIFI_UI_CONNECTED);
        set_portal_phase(PORTAL_CONNECTED);

        LOG_INFO(TAG, "STA IP: \x1b[38;5;200m" IPSTR "\x1b[0m  GW: " IPSTR "  MASK: " IPSTR,
            IP2STR(&e->ip_info.ip),
            IP2STR(&e->ip_info.gw),
            IP2STR(&e->ip_info.netmask)
        );
  
        xEventGroupSetBits(s_wifi_ev, WIFI_BIT_STA_GOT_IP);

        // Start grace period (e.g., 12 seconds)
        wifi_start_ap_grace_period(12 * 1000 * 1000ULL);

    }
}


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


/* DUAL MODE ACCESS POINT / STATION ****************************************************************/
static void on_wifi_event(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base != WIFI_EVENT) return;
    switch (id) {

        case WIFI_EVENT_STA_START: {
            LOG_INFO(TAG, "STA started");
            break;
        }
        case WIFI_EVENT_STA_CONNECTED: { 
            LOG_INFO(TAG, "STA connected (waiting for IP)"); 
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED: {
            LOG_INFO(TAG, "STA disconnected");
            wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
            LOG_INFO(TAG, "STA disconnected -> reason: %d", e->reason);

            if (e->reason == WIFI_REASON_AUTH_FAIL 
            ||  e->reason == WIFI_REASON_HANDSHAKE_TIMEOUT
            ||  e->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
                // Wrong password or auth failure
                set_wifi_state(WIFI_UI_FAILED);
                set_portal_phase(PORTAL_ERROR);
                s_last_err = WIFI_ERR_AUTH_FAIL;
            } else if (e->reason == WIFI_REASON_NO_AP_FOUND) {
                set_wifi_state(WIFI_UI_FAILED);
                set_portal_phase(PORTAL_ERROR);
                s_last_err = WIFI_ERR_NO_AP_FOUND;
            } else {
                set_wifi_state(WIFI_UI_DISCONNECTED);
                set_portal_phase(PORTAL_IDLE);
                s_last_err = WIFI_ERR_UNKNOWN;
            }

            xEventGroupSetBits(s_wifi_ev, WIFI_BIT_STA_FAILED);
            break;
        }
        case WIFI_EVENT_AP_START: {
            ip4_addr_t ap = wifi_get_ap_ip();
            LOG_INFO(TAG, "Software Access Point started at \x1b[38;5;200m %s \x1b[0m", ip4addr_ntoa(&ap));
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t* e = (wifi_event_ap_staconnected_t*)data;
            LOG_INFO(TAG, "STA joined: " MACSTR, MAC2STR(e->mac));
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t* e = (wifi_event_ap_stadisconnected_t*)data;
            LOG_INFO(TAG, "STA left: " MACSTR, MAC2STR(e->mac));
            break;
        }
        default: break;
    }
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
esp_err_t wifi_driver_init_once(void) {
    static bool initialized = false;
    if (initialized) return ESP_OK;

    // Ensure TCP/IP stack and default event loop are initialized
    LOG_INFO(TAG, "checking tcp/ip stack init status...");
    ESP_ERROR_CHECK(tcpip_init_once());
    LOG_INFO(TAG, "tcp/ip stack init status OK");
    
    LOG_INFO(TAG, "loading WIFI_INIT_CONFIG_DEFAULT...");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    LOG_INFO(TAG, "wifi driver initializing...");
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // LOG_INFO(TAG, "creating default wifi access point...");
    esp_netif_create_default_wifi_ap();
    LOG_INFO(TAG, "wifi access point created OK");

    
    // LOG_INFO(TAG, "creating default wifi station...");
    esp_netif_create_default_wifi_sta();
    LOG_INFO(TAG, "wifi station created OK");

    
    // LOG_INFO(TAG, "registering event handlers...");
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL));
    LOG_INFO(TAG, "wifi event handlers registered OK");

    initialized = true;
    return ESP_OK;
}

esp_err_t wifi_start_ap(char *prefix) {
    char ssid[33] = "";
    make_ap_ssid(prefix, ssid); 
    wifi_config_t ap_conf = {
        .ap = {
            .ssid_len = strlen(ssid), 
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        }
    };
    
    // Copy SSID into the struct's array
    strncpy((char *)ap_conf.ap.ssid, ssid, sizeof(ap_conf.ap.ssid));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_conf));
    return ESP_OK;
}

esp_err_t wifi_start_sta(const char *ssid, const char *pass) {
    esp_err_t err = ESP_OK;
    wifi_config_t sta_conf = {0};
    strncpy((char *)sta_conf.sta.ssid, ssid, sizeof(sta_conf.sta.ssid));
    strncpy((char *)sta_conf.sta.password, pass, sizeof(sta_conf.sta.password));
    
    set_wifi_state(WIFI_UI_CONNECTING);  

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA)); // Keep AP active during connect
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_conf));
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        LOG_ERR(TAG, err, "failed to start wifi station mode");
        return err;
    }

    strncpy(s_ssid, ssid, sizeof(s_ssid)); 
    strncpy(s_pass, pass, sizeof(s_pass));  

    return err;
}

esp_err_t wifi_switch_to_sta() {
    esp_err_t err = ESP_OK;
    LOG_INFO(TAG, "switching to STA only mode...");
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) { 
        LOG_WARN(TAG, err, "failed to switch to STA only mode: ");
        set_wifi_state(WIFI_UI_FAILED);
    }
    set_portal_phase(PORTAL_CONNECTED);
    return err;
}

esp_err_t wifi_disable_ap_if_sta_connected(uint32_t timeout_ms) {
    if (!s_wifi_ev) {
        LOG_WARN(TAG, ESP_FAIL, "Event group not initialized");
        return ESP_FAIL;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_ev,
        WIFI_BIT_STA_GOT_IP | WIFI_BIT_STA_FAILED,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_BIT_STA_GOT_IP) {
        LOG_INFO(TAG, "STA connected successfully, disabling AP to save power");
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        s_sw_ap = false;
        return ESP_OK;
    } else {
        LOG_WARN(TAG, ESP_FAIL, "STA failed or timed out, keeping AP active");
        return ESP_FAIL;
    }
}

esp_err_t wifi_init(char prefix[10], char ssid[33], char pass[65]) {
    ESP_ERROR_CHECK(wifi_driver_init_once());

    net_worker_init();
    ESP_ERROR_CHECK(wifi_init_grace_timer());

    // Create event group for STA monitoring
    if (!s_wifi_ev) s_wifi_ev = xEventGroupCreate();

    ESP_ERROR_CHECK(wifi_start_ap(prefix));
    s_sw_ap = true;
    start_dns_server();

    bool have_sta = (ssid[0] != '\0');
    if (have_sta) {
        ESP_ERROR_CHECK(wifi_start_sta(ssid, pass));
    }
    
    // Start Wi-Fi after both configs applied
    ESP_ERROR_CHECK(esp_wifi_start());
   
    return wifi_disable_ap_if_sta_connected(WIFI_WAIT_FOR_IP_ms);
}




