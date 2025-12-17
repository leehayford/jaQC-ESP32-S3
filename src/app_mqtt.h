#ifndef APP_MQTT_H
#define APP_MQTT_H

#include "esp_err.h"

#include "driver/gpio.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


esp_err_t app_mqtt_start(char prefix[10]);


#ifdef __cplusplus
}
#endif

#endif // APP_MQTT_H