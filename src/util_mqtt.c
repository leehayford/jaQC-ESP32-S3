#include "util_mqtt.h"
#include "util_err.h"
#include "util_net_events.h"
// #include "util_device.h"

#include "mqtt_client.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "UTIL_MQTT";

// Client & config
static esp_mqtt_client_handle_t     s_client        = NULL;
static esp_mqtt_client_config_t     s_idf_cfg       = {0};
static bool                         s_connected     = false;

// Sub registry (simple linked list)
typedef struct sub_entry {
    char *filter;
    int   qos;
    util_mqtt_cb_t cb;
    void *user_ctx;
    struct sub_entry *next;
} sub_entry_t;

static sub_entry_t *s_subs = NULL;

// Worker queue
typedef enum {
    MQTT_WORK_START,
    MQTT_WORK_STOP,
    MQTT_WORK_SUB,
    MQTT_WORK_UNSUB,
    MQTT_WORK_PUB
} mqtt_work_cmd_t;

typedef struct {
    mqtt_work_cmd_t cmd;
    char *topic;
    char *payload;   // for PUB
    int   len;       // for PUB
    int   qos;
    bool  retain;
} mqtt_work_msg_t;

static QueueHandle_t s_mqtt_q = NULL;
static TaskHandle_t  s_mqtt_task = NULL;

// ---------- helpers ----------
static void free_msg(mqtt_work_msg_t *m) {
    if (!m) return;
    if (m->topic)   free(m->topic);
    if (m->payload) free(m->payload);
}

// ---------- subscription ----------
static void add_sub_entry(const char *filter, int qos, util_mqtt_cb_t cb, void *user_ctx) {
    sub_entry_t *e = calloc(1, sizeof(*e));
    e->filter = strdup(filter);
    e->qos = qos; e->cb = cb; e->user_ctx = user_ctx;
    e->next = s_subs; s_subs = e;
}

static void remove_sub_entry(const char *filter) {
    sub_entry_t **pp = &s_subs;
    while (*pp) {
        if (strcmp((*pp)->filter, filter) == 0) {
            sub_entry_t *del = *pp; *pp = del->next;
            free(del->filter); free(del); return;
        }
        pp = &(*pp)->next;
    }
}

// ---------- MQTT event handler ----------
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
    int32_t event_id, void *event_data
) {
    esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        LOG_INFO(TAG, "MQTT connected");
        esp_event_post(NET_EVENT, NET_EVENT_MQTT_CONNECTED, NULL, 0, portMAX_DELAY);
        // re-subscribe all filters (esp-mqtt does not persist filters across reconnect unless clean session disabled)
        for (sub_entry_t *p = s_subs; p; p = p->next) {
            int msg_id = esp_mqtt_client_subscribe(s_client, p->filter, p->qos);
            LOG_INFO(TAG, "resubscribe %s (qos=%d) -> msg_id=%d", p->filter, p->qos, msg_id);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        LOG_WARN(TAG, ESP_FAIL, "MQTT disconnected");
        esp_event_post(NET_EVENT, NET_EVENT_MQTT_DISCONNECTED, NULL, 0, portMAX_DELAY);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        LOG_INFO(TAG, "subscribed msg_id=%d", e->msg_id);
        esp_event_post(NET_EVENT, NET_EVENT_MQTT_SUBSCRIBED, &e->msg_id, sizeof(e->msg_id), portMAX_DELAY);
        break;

    case MQTT_EVENT_PUBLISHED:
        LOG_INFO(TAG, "published msg_id=%d", e->msg_id);
        esp_event_post(NET_EVENT, NET_EVENT_MQTT_PUBLISHED, &e->msg_id, sizeof(e->msg_id), portMAX_DELAY);
        break;

    case MQTT_EVENT_DATA:
        // Dispatch to matching callbacks
        // (We do a simple prefix/wildcard match; for production you might want a proper filter-matcher)
        for (sub_entry_t *p = s_subs; p; p = p->next) {
            // If broker granted this subscription and topic matches filter, invoke the callback
            // For now, do a naive match: exact strcmp; extend with '+'/'#' later
            if (p->cb && e->topic && strcmp(e->topic, p->filter) == 0) {
                p->cb(e->topic, (const uint8_t*)e->data, e->data_len, p->user_ctx);
            }
        }
        // Broadcast a generic NET_EVENT for anyone else
        esp_event_post(NET_EVENT, NET_EVENT_MQTT_DATA, e, sizeof(*e), portMAX_DELAY);
        break;

    case MQTT_EVENT_ERROR:
        LOG_WARN(TAG, ESP_FAIL, "MQTT_EVENT_ERROR");
        break;

    default:
        break;
    }
}

/* MQTT WORKER TASK ***********************************************************************
 This task does all of the heavy lifting for NET_EVENTs, timers, interrupts
 - Start / stop client, subscribe / unsubscribe from topics 
 - Handlers post to our queue 's_mqtt_q' 
 - No heavy lifting in handlers (will cause stak overflow)
 - Call heavy functions in corresponding case */
static void mqtt_worker(void *arg) {
    mqtt_work_msg_t msg;

    for (;;) {
        if (!xQueueReceive(s_mqtt_q, &msg, portMAX_DELAY)) continue;

        switch (msg.cmd) {
        case MQTT_WORK_START: {
            LOG_INFO(TAG, "starting MQTT client...");
            if (s_client) esp_mqtt_client_start(s_client); // asynchronous
            break;
        }
        case MQTT_WORK_STOP: {
            LOG_INFO(TAG, "stopping MQTT client...");
            if (s_client) esp_mqtt_client_stop(s_client);
            break;
        }
        case MQTT_WORK_SUB: {
            if (s_client) {
                int mid = esp_mqtt_client_subscribe(s_client, msg.topic, msg.qos);
                LOG_INFO(TAG, "subscribe %s qos=%d -> msg_id=%d", msg.topic, msg.qos, mid);
            }
            break;
        }
        case MQTT_WORK_UNSUB: {
            if (s_client) {
                int mid = esp_mqtt_client_unsubscribe(s_client, msg.topic);
                LOG_INFO(TAG, "unsubscribe %s -> msg_id=%d", msg.topic, mid);
            }
            break;
        }
        case MQTT_WORK_PUB: {
            if (s_client) {
                int mid = esp_mqtt_client_publish(s_client, msg.topic, msg.payload, msg.len, msg.qos, msg.retain);
                LOG_INFO(TAG, "publish %s len=%d qos=%d retain=%d -> msg_id=%d", msg.topic, msg.len, msg.qos, msg.retain, mid);
            }
            break;
        }
        }
        free_msg(&msg);
    }
}

// ---------- NET_EVENT subscribers ----------
static void on_wifi_sta_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
    // Start MQTT only if we actually have IP (guard is redundant but explicit)
    if (net_events_wifi_has_ip()) { 
        esp_event_post(NET_EVENT, NET_EVENT_MQTT_START, NULL, 0, portMAX_DELAY); 
    }
}

static void on_wifi_sta_lost_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
    esp_event_post(NET_EVENT, NET_EVENT_MQTT_STOP, NULL, 0, portMAX_DELAY);
}

static void on_mqtt_start(void *arg, esp_event_base_t base, int32_t id, void *data) {
    mqtt_work_msg_t m = { .cmd = MQTT_WORK_START };
    xQueueSend(s_mqtt_q, &m, 0);
}

static void on_mqtt_stop(void *arg, esp_event_base_t base, int32_t id, void *data) {
    mqtt_work_msg_t m = { .cmd = MQTT_WORK_STOP };
    xQueueSend(s_mqtt_q, &m, 0);
}



// ---------- public API ----------
bool util_mqtt_is_connected(void) { return s_connected; }

bool util_mqtt_is_ready(void) { return s_client != NULL && s_connected; }

void make_mqtt_client_id(char prefix[9], char out[23]) {
    char mac6[13]; 
    make_mac6(mac6);
    snprintf(out, 23, "%s-%s", prefix, mac6);
}

int util_mqtt_subscribe(const char *filter, int qos, util_mqtt_cb_t cb, void *user_ctx) {
    if (!filter) return -1;
    add_sub_entry(filter, qos, cb, user_ctx);

    mqtt_work_msg_t m = {0};
    m.cmd = MQTT_WORK_SUB;
    m.topic = strdup(filter);
    m.qos = qos;
    return xQueueSend(s_mqtt_q, &m, 0) == pdTRUE ? 0 : -1;
}

int util_mqtt_unsubscribe(const char *filter) {
    if (!filter) return -1;
    remove_sub_entry(filter);

    mqtt_work_msg_t m = {0};
    m.cmd = MQTT_WORK_UNSUB;
    m.topic = strdup(filter);
    return xQueueSend(s_mqtt_q, &m, 0) == pdTRUE ? 0 : -1;
}

int util_mqtt_publish(const char *topic, const void *payload, int len, int qos, bool retain) {
    if (!topic || !payload || len <= 0) return -1;
    mqtt_work_msg_t m = {0};
    m.cmd = MQTT_WORK_PUB;
    m.topic = strdup(topic);
    m.payload = malloc(len);
    memcpy(m.payload, payload, len);
    m.len = len; m.qos = qos; m.retain = retain;
    return xQueueSend(s_mqtt_q, &m, 0) == pdTRUE ? 0 : -1;
}

int util_mqtt_publish_str(const char *topic, const char *str, int qos, bool retain) {
    return util_mqtt_publish(topic, str, (int)strlen(str), qos, retain);
}

int util_mqtt_publish_json(const char *topic, cJSON *obj, int qos, bool retain) {
    if (!obj) return -1;
    char *s = cJSON_PrintUnformatted(obj);
    int rc = (s) ? util_mqtt_publish_str(topic, s, qos, retain) : -1;
    if (s) free(s);
    return rc;
}

esp_err_t util_mqtt_publish_bytes(const char *topic, const uint8_t *data, size_t len, int qos, bool retain) {
    if (!s_client || !s_connected) return ESP_ERR_INVALID_STATE;
    // esp_mqtt_client_publish copies payload into its internal outbox; data can be transient
    int msg_id = esp_mqtt_client_publish(s_client, topic, (const char *)data, (int)len, qos, retain);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

// int util_mqtt_enqueue_bytes(const char *topic, const uint8_t *data, size_t len, int qos, bool retain) {
//     if (!s_client || !s_connected) return -1;
//     // enqueue: always queues to outbox; returns msg_id or -1
//     return esp_mqtt_client_enqueue(s_client, topic, (const char*)data, (int)len, qos, retain, /*store*/true);
// }


esp_err_t util_mqtt_init(const util_mqtt_cfg_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;

    // Create worker
    s_mqtt_q = xQueueCreate(8, sizeof(mqtt_work_msg_t));
    xTaskCreate(mqtt_worker, "mqtt_worker", 4096, NULL, 4, &s_mqtt_task);

    // Fill esp_mqtt_client_config_t
    memset(&s_idf_cfg, 0, sizeof(s_idf_cfg));

    // --- Broker address ---
    if (cfg->uri) {
        s_idf_cfg.broker.address.uri = cfg->uri;           // e.g. "mqtt://broker:1883" or "mqtts://broker:8883"
    } else {
        s_idf_cfg.broker.address.hostname = cfg->host;     // alternative to uri
        s_idf_cfg.broker.address.port     = cfg->port;     // alternative to uri
        // Optional: s_idf_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP / MQTT_TRANSPORT_OVER_SSL / MQTT_TRANSPORT_OVER_WS / MQTT_TRANSPORT_OVER_WSS
    }

    // --- Credentials ---
    s_idf_cfg.credentials.client_id = cfg->client_id;
    s_idf_cfg.credentials.username  = cfg->username;
    s_idf_cfg.credentials.authentication.password = cfg->password;

    // --- Session & keepalive ---
    s_idf_cfg.session.disable_clean_session = !cfg->clean_session;  // true -> Clean Session disabled
    s_idf_cfg.session.keepalive = cfg->keepalive_sec ? cfg->keepalive_sec : 60;

    // --- MQTT buffers ---
    s_idf_cfg.buffer.size = 8192;    
    s_idf_cfg.buffer.out_size = 8192;
    s_idf_cfg.outbox.limit = 256 * 1024;
    s_idf_cfg.task.priority = 5;
    s_idf_cfg.task.stack_size = 8192;

    // LWT
    s_idf_cfg.session.last_will.topic  = cfg->lwt_topic;
    s_idf_cfg.session.last_will.msg    = cfg->lwt_msg;
    s_idf_cfg.session.last_will.qos    = cfg->lwt_qos;
    s_idf_cfg.session.last_will.retain = cfg->lwt_retain;

    // // --- TLS / Verification (CA cert etc.) ---
    // s_idf_cfg.broker.verification.certificate    = cfg->cert_pem;          // CA bundle or specific CA (PEM)
    // s_idf_cfg.broker.verification.certificate_len= 0;                      // 0 for PEM strings (IDF expects NUL-terminated PEM) 
    // // If you use mutual TLS:
    // s_idf_cfg.credentials.authentication.certificate   = cfg->client_cert_pem;
    // s_idf_cfg.credentials.authentication.certificate_len = 0;
    // s_idf_cfg.credentials.authentication.key           = cfg->client_key_pem;
    // s_idf_cfg.credentials.authentication.key_len       = 0;

    // // --- Network ---
    // s_idf_cfg.network.disable_auto_reconnect = cfg->disable_auto_reconnect;

    s_client = esp_mqtt_client_init(&s_idf_cfg);
    if (!s_client) return ESP_FAIL;

    // Register handler
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    // Subscribe to NET_EVENTs
    net_events_subscribe(NET_EVENT_WIFI_STA_GOT_IP, on_wifi_sta_got_ip, NULL);
    net_events_subscribe(NET_EVENT_WIFI_STA_LOST_IP, on_wifi_sta_lost_ip, NULL);
    net_events_subscribe(NET_EVENT_MQTT_START, on_mqtt_start, NULL);
    net_events_subscribe(NET_EVENT_MQTT_STOP,  on_mqtt_stop,  NULL);

    LOG_INFO(TAG, "initialized");
    
    // Start MQTT only if we actually have IP (guard is redundant but explicit)
    if (net_events_wifi_has_ip()) { 
        esp_event_post(NET_EVENT, NET_EVENT_MQTT_START, NULL, 0, portMAX_DELAY); 
    }

    return ESP_OK;
}

esp_err_t util_mqtt_deinit(void) {
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    // TODO: free subs list
    LOG_INFO(TAG, "deinitialized");
    return ESP_OK;
}


