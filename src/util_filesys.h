#ifndef UTIL_FILESYS_H
#define UTIL_FILESYS_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif



esp_err_t filesys_init(void);



#ifdef __cplusplus
}
#endif

#endif // UTIL_FILESYS_H