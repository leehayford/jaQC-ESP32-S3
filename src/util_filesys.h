#ifndef UTIL_FILESYS_H
#define UTIL_FILESYS_H

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


esp_err_t filesys_init(void);
esp_err_t filesys_delete(const char *path);
esp_err_t filesys_check_file(const char *path, size_t *out_size);
esp_err_t filesys_get_info(size_t *out_total, size_t *out_used);
esp_err_t filesys_sha256(const char *path, unsigned char out_hash[32]);
esp_err_t filesys_write_text_atomic(const char *final_path, const char *text);
esp_err_t filesys_read_text(const char *path, char **out_text, size_t *out_len);
char* filesys_show_directory(const char *path);
esp_err_t filesys_update_from_stream(
    const char *final_path,
    const char *temp_path,
    size_t expected_len,
    esp_err_t (*read_chunk)(void *ctx, char *buf, size_t buf_sz, int *out_len),
    void *ctx,
    const unsigned char *expected_hash, // may be NULL
    bool verify 
);



void log_hash_hex(const unsigned char *name, const unsigned char *hash, size_t len);
bool hex_to_bytes32(const char *hex, unsigned char out[32]);
bool hashes_equal(const unsigned char a[32], const unsigned char b[32]);


#ifdef __cplusplus
}
#endif

#endif // UTIL_FILESYS_H