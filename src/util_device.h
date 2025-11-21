#ifndef UTIL_DEVICE_H
#define UTIL_DEVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void make_mac6(char out[13]);

/* TODO: MOVE TO util_mqtt.h */
// static void make_mqtt_id(char serial[10], char out[23]);

#ifdef __cplusplus
}
#endif

#endif // UTIL_DEVICE_H