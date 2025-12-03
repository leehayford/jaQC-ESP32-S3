#ifndef UTIL_FILESYS_H
#define UTIL_FILESYS_H

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


esp_err_t filesys_init(void);

esp_err_t storage_check_file(const char *path, size_t *out_size);


#ifdef __cplusplus
}
#endif

#endif // UTIL_FILESYS_H