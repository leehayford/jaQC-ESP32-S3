#include "util_device.h"

#include "esp_mac.h"

#include <string.h>


void make_mac6(char out[13]) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, 13, "%02x%02x%02x", mac[3], mac[4], mac[5]);
}


/* TODO: MOVE TO util_mqtt.c */
// static void make_mqtt_id(char serial[10], char out[23]) {
//     char mac6[13]; make_mac6(mac6);
//     snprintf(out, 23, "%s-%s", serial, mac6);
// }