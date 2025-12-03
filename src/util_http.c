#include "util_http.h"
#include "util_err.h"
#include "util_net_events.h"
#include "util_wifi.h"
#include "util_html_fb.h"
#include "util_filesys.h"

#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_spiffs.h"
#include "esp_timer.h"

#include "cJSON.h"
#include <string.h>
#include <inttypes.h> // Required for PRIu32

static const char *TAG = "UTIL_HTTP";

static TaskHandle_t s_web_update_task;

static httpd_handle_t server = NULL;

// *** Captiv portal stuff **************************************************

esp_err_t serve_connecting_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");

    return httpd_resp_sendstr(req, fb_connecting_html);
}

esp_err_t captive_portal_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, fb_wifi_html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t captive_redirect_handler(httpd_req_t *req) {
    // While connecting/connected, show connecting page to avoid “flash then revert”
    portal_ui_phase_t p = get_portal_phase();
    if (p == PORTAL_CONNECTING || p == PORTAL_CONNECTED) {
        return serve_connecting_page(req);
    }
    // Otherwise serve your normal captive page
    return captive_portal_handler(req);
}

httpd_uri_t captive_uri                 = {.uri     = "/captive_portal",
    .method    = HTTP_GET,
    .handler   = captive_redirect_handler, 
    .user_ctx  = NULL
};
// Android captive check "...domain.../generate_204 (Android)
httpd_uri_t captive_android_uri         = {.uri     = "/generate_204",
    .method    = HTTP_GET,
    .handler   = captive_redirect_handler, 
    .user_ctx  = NULL
};
// Apple captive check "...domain.../hotspot-detect.html" (iOS)
httpd_uri_t captive_apple_uri           = {.uri     = "/hotspot-detect.html",
    .method    = HTTP_GET,
    .handler   = captive_redirect_handler, 
    .user_ctx  = NULL
};
// Microsoft...
static esp_err_t msft_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/redirect");
    return httpd_resp_sendstr(req, "Redirecting...");
}
// Microsoft captive check 1 "dns.msftncsi.com/ncsi.txt" (Windows 1)
httpd_uri_t captive_msft_1_uri          = {.uri     = "/ncsi.txt",
    .method    = HTTP_GET,
    .handler   = msft_handler, 
    .user_ctx  = NULL
};
// Microsoft captive check 2 "www.msftconnecttest.com/connecttest.txt" (Windows 2)
httpd_uri_t captive_msft_2_uri          = {.uri     = "/connecttest.txt",
    .method    = HTTP_GET,
    .handler   = msft_handler, 
    .user_ctx  = NULL
};
// Microsoft captive check needs to find a route called "redirect"... cuz windows I guess
httpd_uri_t captive_msft_redirect_uri   = {.uri     = "/redirect",
    .method    = HTTP_GET,
    .handler   = captive_redirect_handler, 
    .user_ctx  = NULL
};

// *** END Captiv portal stuff **********************************************


const char *FILE_PATH_HTML_INDEX = "/storage/index.html";
// const char *URL_UPDATE_WEB = "http://10.0.0.233:8013/api/jaqc/update_web/index.html"; // Home
const char *URL_UPDATE_WEB = "http://192.168.1.165:8013/api/jaqc/update_web/index.html"; // Work
esp_err_t update_web_files() {
    esp_http_client_config_t config = {
        .url = URL_UPDATE_WEB,
        .timeout_ms = 5000
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        LOG_ERR(TAG, ESP_FAIL, "failed to init HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        LOG_ERR(TAG, err, "failed to open HTTP connection: ");
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0) {
        LOG_WARN(TAG, ESP_FAIL, "invalid content length");
    }

    FILE *f = fopen(FILE_PATH_HTML_INDEX, "wb");
    if (!f) {
        LOG_ERR(TAG, ESP_FAIL, "failed to open file for writing");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    char buffer[512];
    int data_read;
    while ((data_read = esp_http_client_read(client, buffer, sizeof(buffer))) > 0) {
        fwrite(buffer, 1, data_read, f);
    }

    fclose(f);
    esp_http_client_cleanup(client);

    size_t sz = 0;
    if (storage_check_file(FILE_PATH_HTML_INDEX, &sz) != ESP_OK) {
        LOG_ERR(TAG, ESP_FAIL, "verification failed for %s", FILE_PATH_HTML_INDEX);
        return ESP_FAIL;
    }
    LOG_INFO(TAG, "verified %s exists, size=%u bytes", FILE_PATH_HTML_INDEX, (unsigned)sz);
    LOG_INFO(TAG, "file downloaded and saved to %s", FILE_PATH_HTML_INDEX);
    return ESP_OK;

}

static const httpd_uri_t update_web_uri     = {.uri     = "/api/update_web", 
    .method = HTTP_GET, 
    .handler = update_web_files,
    .user_ctx = NULL
};

static esp_err_t status_get_handler(httpd_req_t *req) {
   
    char json[160];

    char ip[16] = {0};

    int n = snprintf(json, sizeof(json), 
        "{\"state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\"}",
        wifi_state_to_str(get_wifi_state()),
        get_wifi_ssid(),
        wifi_get_ip_str(ip, sizeof(ip)) == ESP_OK ? ip : ""
    );

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, n);
    if (err == ESP_OK) {
        LOG_INFO(TAG, "firing NET_EVENT_WIFI_STATUS_SERVED...");
        esp_event_post(NET_EVENT, NET_EVENT_WIFI_STATUS_SERVED, NULL, 0, portMAX_DELAY);
    }
    return err;
}
static const httpd_uri_t status_uri     = {.uri     = "/api/status", 
    .method = HTTP_GET, 
    .handler = status_get_handler,
    .user_ctx = NULL
};

// Handle scan for available wifi access points
static esp_err_t scan_get_handler(httpd_req_t *req) {
    // wifi_scan_json() is implemented in util_wifi.c and returns a malloc'ed JSON string.
    /* e.g., 
    [
        {   
            "ssid":"...",
            "bssid":"..",
            "rssi":-60,
            "chan":1,
            "auth":"wpa2-psk",
            "open":false
        }, 
        ...
    ]    */
    char *json = wifi_scan_json(); 
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scan failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = httpd_resp_sendstr(req, json);
    free(json);
    return r;
}
httpd_uri_t scan_uri                    = {.uri      = "/api/scan",
    .method   = HTTP_GET,
    .handler  = scan_get_handler,
    .user_ctx = NULL
};

esp_err_t connect_post_handler(httpd_req_t *req) {
    LOG_INFO("CONNECT", "POST /connect handler invoked");

    // Read body
    char buf[256];
    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv error");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';

    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }

    const cJSON *ssid_json = cJSON_GetObjectItem(json, "ssid");
    const cJSON *pass_json = cJSON_GetObjectItem(json, "pass");

    char ssid[64] = {0}, pass[64] = {0};
    if (cJSON_IsString(ssid_json) && ssid_json->valuestring) {
        strlcpy(ssid, ssid_json->valuestring, sizeof(ssid));
    }
    if (cJSON_IsString(pass_json) && pass_json->valuestring) {
        strlcpy(pass, pass_json->valuestring, sizeof(pass));
    }
    cJSON_Delete(json);

    // Start STA (non-blocking)
    esp_err_t err = wifi_start_sta(ssid, pass);
    if (err != ESP_OK) {
        LOG_ERR("CONNECT", err, "wifi_start_sta failed: ");
    }

    // Switch UI to "Connecting" and serve the page
    set_portal_phase(PORTAL_CONNECTING);
    return serve_connecting_page(req); 
}
httpd_uri_t connect_uri                 = {.uri      = "/api/connect",
    .method   = HTTP_POST,
    .handler  = connect_post_handler,
    .user_ctx = NULL
};

// Default redirect for unknown paths or foreign Host headers
static esp_err_t catchall_redirect(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_sendstr(req, "Redirecting...");
}
httpd_uri_t catch_all_uri               = {.uri       = "/*",
    .method    = HTTP_GET,
    .handler   = catchall_redirect, 
    .user_ctx  = NULL
};

// Home page... what else can I say?
esp_err_t home_page_handler(httpd_req_t *req) {
    LOG_INFO(TAG, "HOME PAGE HANDLER");
    portal_ui_phase_t p = get_portal_phase();
    if (p == PORTAL_CONNECTING || p == PORTAL_CONNECTED) {
        return serve_connecting_page(req);
    }
    
    // Otherwise serve home page 
    FILE *f = fopen(FILE_PATH_HTML_INDEX, "rb");
    if (f) {
        httpd_resp_set_type(req, "text/html");
        char buf[1024]; 
        size_t n;
        while ((n=fread(buf,1,sizeof(buf),f))>0) {
            httpd_resp_send_chunk(req, buf, n);
        }
        httpd_resp_send_chunk(req, NULL, 0);
        fclose(f);
        return ESP_OK;
    }
    // Fallback
    return captive_portal_handler(req);
}
httpd_uri_t home_uri                    = {.uri       = "/",
    .method    = HTTP_GET,
    .handler   = home_page_handler, 
    .user_ctx  = NULL
};

esp_err_t register_route(const httpd_uri_t *uri_handler) {
    esp_err_t err = httpd_register_uri_handler(server, uri_handler);
    if (err != ESP_OK) {
        LOG_ERR(TAG, err, "failed to register route: %s: ", uri_handler->uri);
        return err;
    }
    LOG_INFO(TAG, "register route OK: %s: ", uri_handler->uri);
    return ESP_OK;
}

esp_err_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

    ESP_ERROR_CHECK(httpd_start(&server, &config));

    // Captive UI routes
    register_route(&captive_android_uri);
    register_route(&captive_apple_uri);
    register_route(&captive_msft_1_uri);
    register_route(&captive_msft_2_uri);
    register_route(&captive_msft_redirect_uri);
    register_route(&captive_uri);
    
    // STA connect routes
    register_route(&scan_uri);
    register_route(&status_uri);
    register_route(&connect_uri);
    register_route(&update_web_uri);

    // Home
    register_route(&catch_all_uri);
    register_route(&home_uri);

    return ESP_OK;
}

