#ifndef JAQC_ADMIN_H
#define JAQC_ADMIN_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif // JAQC_ADMIN_H