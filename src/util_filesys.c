#include "util_filesys.h"
#include "util_err.h"

#include "esp_spiffs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "UTIL_FILESYS";


static esp_err_t filesys_sha256_stream_md_init(mbedtls_md_context_t *md) {
    if (!md) return ESP_ERR_INVALID_ARG;
    mbedtls_md_init(md);
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return ESP_FAIL;
    if (mbedtls_md_setup(md, info, 0) != 0) return ESP_FAIL;
    if (mbedtls_md_starts(md) != 0) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t filesys_sha256_stream_md_update(mbedtls_md_context_t *md,
                                                 const unsigned char *data, size_t len) {
    if (!md || (!data && len)) return ESP_ERR_INVALID_ARG;
    if (mbedtls_md_update(md, data, len) != 0) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t filesys_sha256_stream_md_finish(mbedtls_md_context_t *md,
                                                 unsigned char out_hash[32]) {
    if (!md || !out_hash) return ESP_ERR_INVALID_ARG;
    if (mbedtls_md_finish(md, out_hash) != 0) return ESP_FAIL;
    mbedtls_md_free(md);
    return ESP_OK;
}


esp_err_t filesys_update_from_stream(
    const char *final_path,
    const char *temp_path,
    size_t expected_len,
    esp_err_t (*read_chunk)(void *ctx, char *buf, size_t buf_sz, int *out_len),
    void *ctx
) {
    if (!final_path || !temp_path || !read_chunk) return ESP_ERR_INVALID_ARG;

    FILE *f = fopen(temp_path, "wb");
    if (!f) {
        LOG_ERR(TAG, ESP_FAIL, "failed to open temp file: %s", temp_path);
        return ESP_FAIL;
    }

    // Use heap buffer to reduce stack pressure
    size_t buf_sz = 1024; // you can keep 1024 now because it's on heap
    char *buffer = (char *)malloc(buf_sz);
    if (!buffer) {
        ESP_LOGE(TAG, "malloc(%u) failed", (unsigned)buf_sz);
        fclose(f);
        unlink(temp_path);
        return ESP_ERR_NO_MEM;
    }

    
    // Start MD (SHA-256)
    mbedtls_md_context_t md;
    ESP_ERROR_CHECK_WITHOUT_ABORT(filesys_sha256_stream_md_init(&md));  // don't abort, handle errors

    LOG_INFO(TAG, "HTTPD task high-water: %u", uxTaskGetStackHighWaterMark(NULL));
    size_t total_written = 0;
    int chunk_len = 0;

    esp_err_t err = ESP_OK;
    while ((err = read_chunk(ctx, buffer, buf_sz, &chunk_len)) == ESP_OK 
    &&      chunk_len > 0
    ) {
        size_t w = fwrite(buffer, 1, chunk_len, f);
        if (w != (size_t)chunk_len) {
            LOG_ERR(TAG, ESP_FAIL, "partial write detected: %u -> %u", 
                (unsigned)chunk_len, (unsigned)w);
            free(buffer);
            fclose(f);
            unlink(temp_path);
            return ESP_FAIL;
        }
        total_written += w;

        // Stream into digest:
        err = filesys_sha256_stream_md_update(&md, (unsigned char *)buffer, (size_t)chunk_len);
        if (err != ESP_OK) {
            // Digest update failed; rollback temp file and bail
            free(buffer);
            fclose(f);
            unlink(temp_path);
            mbedtls_md_free(&md);
            return ESP_FAIL;
        }

    }

    fflush(f);                      // call regardless of error
    fsync(fileno(f));               // call regardless of error
    fclose(f);                      // call regardless of error
    if (err != ESP_OK               // check error after flush, sync, and close
    &&  err != ESP_ERR_NOT_FOUND    // EOF is sinaled by ESP_ERR_NOT_FOUND
    ) { 
        LOG_ERR(TAG, err, "read_chunk failed: ");
        free(buffer);
        unlink(temp_path);
        return err;
    }

    if (expected_len > 0            // Validate size if expected_len > 0
    &&  total_written != expected_len 
    ) {
        LOG_WARN(TAG, ESP_FAIL, "size mismatch: wrote=%u expected=%u",
            (unsigned)total_written, (unsigned)expected_len);
        // Decide: rollback or keep file. Here we rollback:
        unlink(temp_path);
        return ESP_FAIL;
    }

    // Finalize hash
    unsigned char hash[32];
    err = filesys_sha256_stream_md_finish(&md, hash);
    free(buffer);                   // regardless of error we are done with the buffer
    if (err != ESP_OK) {
        unlink(temp_path);
        return ESP_FAIL;
    }
    log_hash_hex(hash, sizeof(hash));

    // Atomic rename (SPIFFS cannot overwrite existing file)
    unlink(final_path); // ignore error if file doesn't exist
    if (rename(temp_path, final_path) != 0) {
        LOG_ERR(TAG, ESP_FAIL, "atomic rename failed");
        unlink(temp_path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "update complete: %s (%u bytes)", final_path, (unsigned)total_written);
    return ESP_OK;
}


// The Quick and Dirty
// Check if a file exists at some path, and if that file's size is > 0
esp_err_t filesys_check_file(const char *path, size_t *out_size) {

    struct stat st;
    if (stat(path, &st) != 0) {
        // errno will tell you why (ENOENT etc.)
        return ESP_FAIL;
    }

    if (out_size) *out_size = (size_t)st.st_size;
    return ESP_OK;
}

esp_err_t filesys_get_info(size_t *out_total, size_t *out_used) {

    if (!out_total || !out_used) return ESP_ERR_INVALID_ARG;

    esp_err_t err = esp_spiffs_info("storage", out_total, out_used);
    if (err != ESP_OK) {
        LOG_ERR(TAG, err, "failed to get SPIFFS info: ");
        return err;
    }

    LOG_INFO(TAG, "SPIFFS info: used=%u bytes, total=%u bytes",
             (unsigned)*out_used, (unsigned)*out_total);
    return ESP_OK;
}

esp_err_t filesys_delete(const char *path) {
    if (!path) return ESP_ERR_INVALID_ARG;
    int rc = unlink(path);
    if (rc != 0) {
        LOG_ERR(TAG, ESP_FAIL, "unlink failed for %s", path);
        return ESP_FAIL;
    }
    LOG_INFO(TAG, "deleted %s", path);
    return ESP_OK;
}

esp_err_t filesys_sha256(const char *path, unsigned char out_hash[32]) {

    if (!path || !out_hash) return ESP_ERR_INVALID_ARG;

    FILE *f = fopen(path, "rb");
    if (!f) return ESP_FAIL;

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256 (not SHA-224)

    unsigned char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        mbedtls_sha256_update(&ctx, buf, n);
    }

    fclose(f);
    mbedtls_sha256_finish(&ctx, out_hash);
    mbedtls_sha256_free(&ctx);
    return ESP_OK;

}

void log_hash_hex(const unsigned char *hash, size_t len) {
    char hex[65] = {0};
    for (size_t i = 0; i < len; ++i) {
        sprintf(hex + (i * 2), "%02x", hash[i]);
    }
    LOG_INFO(TAG, "SHA256 = \x1b[38;5;200m%s\x1b[0m", hex);
}

esp_err_t filesys_init(void) {

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/storage",
        .partition_label = "storage",
        .max_files=8,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) 
        LOG_ERR(TAG, err, "failed to mount file system");

    else {

        size_t total = 0, used = 0;
        err = filesys_get_info(&total, &used);
        if ( err != ESP_OK) 
            LOG_ERR(TAG, err, "failed to verify file system");

        else 
            LOG_INFO(TAG, "Free space: %.1f KB", (total - used) / 1024.0);

    }
    
    return err;
}
