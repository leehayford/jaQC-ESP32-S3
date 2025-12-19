#ifndef UTIL_MQTT_H
#define UTIL_MQTT_H

#include "esp_event.h"
#include "esp_err.h"

#include "mqtt_client.h"      // ESP-MQTT
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
    // Either use uri OR host+port
    const char *uri;          // e.g. "mqtt://broker:1883" or "mqtts://broker:8883"
    const char *host;         // alternative to uri
    int         port;         // alternative to uri
    const char *client_id;    // optional; auto-generated if NULL
    const char *username;     // optional
    const char *password;     // optional

    // TLS fields (esp-mqtt uses mbedTLS under the hood)
    const char *cert_pem;     // CA cert (PEM)
    const char *client_cert_pem; // client cert (mutual TLS)
    const char *client_key_pem;  // client key (mutual TLS)

    // Session & keepalive
    bool        clean_session;      // default true
    int         keepalive_sec;      // default 60

    // Last Will & Testament (LWT)
    const char *lwt_topic;    
    const char *lwt_msg;      
    int         lwt_qos;      
    bool        lwt_retain;   

    // Auto-reconnect (ESP-MQTT can handle this internally)
    bool        disable_auto_reconnect; // default false
} util_mqtt_cfg_t;


typedef void (*util_mqtt_cb_t)(
    const char *topic,
    const uint8_t *data, int data_len,
    void *user_ctx
);


// Init/Deinit
esp_err_t util_mqtt_init(const util_mqtt_cfg_t *cfg);
esp_err_t util_mqtt_deinit(void);
bool util_mqtt_is_ready(void);

// Subscriptions & Publishing
int util_mqtt_subscribe(const char *topic_filter, int qos, util_mqtt_cb_t cb, void *user_ctx);
int util_mqtt_unsubscribe(const char *topic_filter);

int util_mqtt_publish(const char *topic, const void *payload, int len, int qos, bool retain);
int util_mqtt_publish_str(const char *topic, const char *str, int qos, bool retain);
int util_mqtt_publish_json(const char *topic, cJSON *obj, int qos, bool retain);
esp_err_t util_mqtt_publish_bytes(const char *topic, const uint8_t *data, size_t len, int qos, bool retain);

int util_mqtt_enqueue_bytes(const char *topic, const uint8_t *data, size_t len, int qos, bool retain);

// Status
bool util_mqtt_is_connected(void);
void make_mqtt_client_id(char prefix[9], char out[23]);

#ifdef __cplusplus
}
#endif

#endif // UTIL_MQTT_H