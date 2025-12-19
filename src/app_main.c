#include "app_admin.h"
#include "app_TLV320ADC5120.h"
#include "driver_TLV320ADC5120.h"
#include "models.h"
#include "util_err.h"
#include "util_filesys.h"
#include "util_wifi.h"
#include "util_http.h"
#include "util_mqtt.h"

#include "esp_log.h"
#include "esp_task_wdt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

// TODO: move to app_io.h/.c
#include "driver/gpio.h"

void io_init(uint64_t pin_def) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin_def), // Bit mask for the pin
        .mode = GPIO_MODE_OUTPUT,           // Set as output
        .pull_up_en = GPIO_PULLUP_DISABLE,  // No pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // No pull-down
        .intr_type = GPIO_INTR_DISABLE      // No interrupt
    };
    gpio_config(&io_conf);
}


// TODO: move to app_http.h/.c
#include "esp_http_server.h"
static esp_err_t whatever_get_handler(httpd_req_t *req) {
   
    char json[160];

    char ip[16] = {0};

    int n = snprintf(json, sizeof(json), 
        "{\"state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\"}",
        wifi_state_to_str(get_wifi_state()),
        get_wifi_ssid(),
        wifi_get_ip_str(ip, sizeof(ip)) == ESP_OK ? ip : ""
    );

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, n);
}
static const httpd_uri_t whatever_uri = {
    .uri = "/api/whatever", 
    .method = HTTP_GET, 
    .handler = whatever_get_handler,
    .user_ctx = NULL
};


static const char *TAG = "JAQC";

/* DEBUG / TEST *******************************************************************/

// extern cfg_t s_cfg;
// extern ops_t s_ops;

#include "util_flash.h"
void confirm_flash_init() {
    // esp_err_t err = flash_list_namespase_keys(NS_CFG);
    // if (err != ESP_OK) {
    //     LOG_ERR(TAG, err, "flash_list_namespase_keys(NS_CFG) failed...");
    // }
    LOG_INFO(TAG, "s_cfg.serial %s", s_cfg.serial);
    LOG_INFO(TAG, "s_cfg.hw_prefix %s", s_cfg.hw_prefix);
    LOG_INFO(TAG, "s_cfg.wifi_ssid %s", s_cfg.wifi_ssid);
    LOG_INFO(TAG, "s_cfg.wifi_pass %s", s_cfg.wifi_pass);
}

void confirm_filesys_init() {
    esp_err_t err = filesys_init();
    if (err != ESP_OK) {
        LOG_ERR(TAG, err, "file system test failed...");
    }
}

#include "util_net_events.h"
void confirm_net_events_init() {
    esp_err_t err = net_events_init_once();
    if (err != ESP_OK) {
        LOG_ERR(TAG, err, "net events init test failed...");
    }
}

void confirm_wifi_init_task() {
    esp_err_t err = wifi_init(s_cfg.hw_prefix, s_cfg.wifi_ssid, s_cfg.wifi_pass);
    // char ap_ssid[33] = {0};
    // esp_err_t err = wifi_start_sw_ap(ap_ssid);
    if (    err != ESP_OK
    &&      err != ESP_FAIL
    ) {
        LOG_ERR(TAG, err, "wifi test failed...");
    } 
    // else {
    //     LOG_INFO(TAG, "Accesp Point SSID: %s", ap_ssid);
    // }
    vTaskDelete(NULL);
}

void confirm_start_webserver() {
    esp_err_t err = start_webserver();
    if (err != ESP_OK) {
        LOG_ERR(TAG, err, "web server test failed...");
    } else {
        register_route(&whatever_uri);
    }

}

#include "app_mqtt.h"

static void log_mqtt_buffers(void) {
#if defined(CONFIG_MQTT_BUFFER_SIZE)
    int work = CONFIG_MQTT_BUFFER_SIZE;
#else
    int work = -1;
#endif

#if defined(CONFIG_MQTT_TRANSPORT_SEND_BUFFER)
    int send = CONFIG_MQTT_TRANSPORT_SEND_BUFFER;
#else
    int send = -1;
#endif

#if defined(CONFIG_MQTT_TRANSPORT_RECV_BUFFER)
    int recv = CONFIG_MQTT_TRANSPORT_RECV_BUFFER;
#else
       int recv = -1;
#endif

    LOG_INFO(TAG, "MQTT buffers: work=%d send=%d recv=%d", work, send, recv);

    if (work < 0 || send < 0) {
        LOG_WARN(TAG, ESP_FAIL,
                 "MQTT buffer macros not visible; PlatformIO likely hasn't indexed sdkconfig.h yet. "
                 "Run 'pio run' or 'pio run -t menuconfig' to verify Kconfig.");
    }
}
static bool mqtt_initialized = false;
void confirm_start_mqtt() {
    // char mqtt_id[23] = "";
    // char prefix[9] = "JAQC";
    // make_mqtt_client_id(prefix, mqtt_id);
    // LOG_INFO(TAG, "MQTT client id: %s", mqtt_id);
    // util_mqtt_cfg_t cfg = {
    //     .uri = "mqtt://143.198.50.152:1883",
    //     .client_id = mqtt_id, // "esp32s3-" /* + mac suffix if desired */,
    //     .username = "JaQCAPI", 
    //     .password = "im2#1*2n2",
    //     .clean_session = true, 
    //     .keepalive_sec = 60,
    //     .lwt_topic = "devices/esp32s3/status",
    //     .lwt_msg   = "offline", .lwt_qos = 1, .lwt_retain = true,
    // };
    // esp_err_t err = util_mqtt_init(&cfg);
    // if (err != ESP_OK) {
    //     LOG_ERR(TAG, err, "mqtt test failed...");
    // }
    
    log_mqtt_buffers();

    char prefix[10] = "JAQC";
    esp_err_t err = app_mqtt_start(prefix);
    if (err != ESP_OK) {
        LOG_ERR(TAG, err, "mqtt test failed...");
    }
}

void confirm_ADC_start() {
    i2c_scan();
    esp_err_t err = app_tlv_start();
    if (err) {
        LOG_ERR(TAG, err, "adc initialization failed");
    }
    LOG_INFO(TAG, "adc initialization OK");
}

/* END DEBUG / TEST ***************************************************************/


void app_main(void) {

    /*** WATCHDOG STUFF *************************************************/
    // Disable any auto-init
    esp_task_wdt_deinit();

    // Reconfigure: 15s timeout, DO NOT monitor idle cores
    esp_task_wdt_config_t cfg = {
        .timeout_ms     = 15000,
        .idle_core_mask = 0,        // 0 => don't watch IDLE0/IDLE1
        .trigger_panic  = true
    };
    esp_task_wdt_init(&cfg);

    /*** END WATCHDOG STUFF *********************************************/

    // Some delay so I can watch the startuip log
    vTaskDelay(pdMS_TO_TICKS(100));
    LOG_INFO(TAG, "\n\n\n\n *************************************************************** \n starting up...");

    // Initialize non-volatile storage and open model namespaces
    ESP_ERROR_CHECK(models_init()); 
    confirm_flash_init();

    // Initialize file system
    ESP_ERROR_CHECK(filesys_init()); // confirm_filesys_init();

    // Initialize TCP/IP stack & default event loop for Wi-Fi & IP events
    // ESP_ERROR_CHECK(net_events_init_once());
    confirm_net_events_init();
    
    // Initialize Wi-Fi
    // ESP_ERROR_CHECK(wifi_init());
    xTaskCreate(confirm_wifi_init_task, "confirm_wifi_init_task", 8192, NULL, 5, NULL);
    
    // Run web server
    // ESP_ERROR_CHECK(web_serve());
    confirm_start_webserver();

    // Connect to MQTT broker & subscribe to .../cmd, .../diag
    // ESP_ERROR_CHECK(mqtt_connect()); 

    io_init(PIN_OUT_WIFI_BLUE_LED);
    io_init(PIN_OUT_WIFI_RED_LED);

    // confirm_ADC_start();

    int count = 0;
    // Do tasks
    while(1) {
        count++;
        vTaskDelay(pdMS_TO_TICKS(500));

        wifi_ui_state_t wifi_state = get_wifi_state();
        switch (wifi_state) {
            case WIFI_UI_IDLE: 
            case WIFI_UI_FAILED:
            case WIFI_UI_DISCONNECTED:
                gpio_set_level(PIN_OUT_WIFI_BLUE_LED, LED_OFF);
                gpio_set_level(PIN_OUT_WIFI_RED_LED, count % 2);
                break;

            case WIFI_UI_CONNECTING: 
                gpio_set_level(PIN_OUT_WIFI_BLUE_LED, count % 2);
                gpio_set_level(PIN_OUT_WIFI_RED_LED, LED_OFF);
                break;

            case WIFI_UI_CONNECTED: 
                gpio_set_level(PIN_OUT_WIFI_BLUE_LED, LED_ON);
                gpio_set_level(PIN_OUT_WIFI_RED_LED, LED_OFF);
                break;
            
        }

        if (count % 10 == 0){
            LOG_INFO(TAG, "WIFI Status %s", wifi_state_to_str(wifi_state));
            
            if(mqtt_initialized) {
                util_mqtt_publish_str("jaqc/sig/test", wifi_state_to_str(wifi_state), /*qos*/0, /*retain*/false);
            } else if (wifi_state == WIFI_UI_CONNECTED && !mqtt_initialized) {
                confirm_start_mqtt();
                mqtt_initialized = true;
                confirm_ADC_start();
            }
        }
    }
}