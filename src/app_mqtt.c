#include "util_mqtt.h"
#include "util_err.h"
#include "models.h"
#include "cJSON.h"

static const char *TAG = "APP_MQTT";

static void on_cmd_toggle(const char *topic, const uint8_t *data, int len, void *ctx) {
    LOG_INFO(TAG, "CMD TOGGLE: %.*s", len, (const char*)data);
    // Do something...
}

static void on_cmd_set(const char *topic, const uint8_t *data, int len, void *ctx) {
    // Parse JSON to set parameters
    cJSON *root = cJSON_ParseWithLength((const char*)data, len);
    if (!root) return;
    const cJSON *val = cJSON_GetObjectItem(root, "value");
    if (cJSON_IsNumber(val)) {
        // apply value
    }
    cJSON_Delete(root);
}

void app_mqtt_start(char prefix[10]) {
    char mqtt_id[23] = "";
    make_mqtt_client_id(prefix, mqtt_id);
    LOG_INFO(TAG, "MQTT client id: %s", mqtt_id);
    util_mqtt_cfg_t cfg = {
        .uri = "mqtt://143.198.50.152:1883",
        .client_id = mqtt_id, // "esp32s3-" /* + mac suffix if desired */,
        .username = "JaQCAPI", 
        .password = "im2#1*2n2",
        .clean_session = true, 
        .keepalive_sec = 60,
        .lwt_topic = "devices/esp32s3/status",
        .lwt_msg   = "offline", .lwt_qos = 1, .lwt_retain = true,
    };
    ESP_ERROR_CHECK(util_mqtt_init(&cfg));

    util_mqtt_subscribe("devices/esp32s3/cmd/toggle", /*qos*/1, on_cmd_toggle, NULL);
    util_mqtt_subscribe("devices/esp32s3/cmd/set",    /*qos*/1, on_cmd_set,    NULL);

    // Publish an “online” retained status on connect (or rely on LWT retained offline)
    cJSON *hello = cJSON_CreateObject();
    cJSON_AddStringToObject(hello, "status", "online");
    util_mqtt_publish_json("devices/esp32s3/status", hello, /*qos*/1, /*retain*/true);
    cJSON_Delete(hello);
}
