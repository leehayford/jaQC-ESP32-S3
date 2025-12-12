#ifndef JAQC_ADMIN_H
#define JAQC_ADMIN_H

#include "esp_err.h"

#include "driver/gpio.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LED_OFF                     1
#define LED_ON                      0

#define PIN_OUT_WIFI_BLUE_LED       GPIO_NUM_3
#define PIN_OUT_WIFI_RED_LED        GPIO_NUM_2

esp_err_t factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif // JAQC_ADMIN_H