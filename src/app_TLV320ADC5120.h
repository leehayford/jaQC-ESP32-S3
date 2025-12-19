#ifndef APP_TLV320ADC5120_H
#define APP_TLV320ADC5120_H

#include "esp_err.h"

#include "driver/gpio.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_tlv_start(void);
#ifdef __cplusplus
}
#endif

#endif // APP_TLV320ADC5120_H