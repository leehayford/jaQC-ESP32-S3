#ifndef UTIL_TCPIP_H
#define UTIL_TCPIP_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif



esp_err_t tcpip_init_once(void);



#ifdef __cplusplus
}
#endif

#endif // UTIL_TCPIP_H