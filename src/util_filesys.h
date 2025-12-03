#ifndef UTIL_FILESYS_H
#define UTIL_FILESYS_H

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


esp_err_t filesys_init(void);

esp_err_t filesys_check_file(const char *path, size_t *out_size);

esp_err_t filesys_update_from_stream(
    const char *final_path,
    const char *temp_path,
    size_t expected_len,
    esp_err_t (*read_chunk)(void *ctx, char *buf, size_t buf_sz, int *out_len),
    void *ctx
);

esp_err_t filesys_delete(const char *path);

esp_err_t filesys_sha256(const char *path, unsigned char out_hash[32]);

void log_hash_hex(const unsigned char *hash, size_t len);

#ifdef __cplusplus
}
#endif

#endif // UTIL_FILESYS_H