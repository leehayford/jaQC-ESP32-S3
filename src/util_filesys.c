#include "util_filesys.h"
#include "util_err.h"

#include "esp_spiffs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>
#include <errno.h>
#include "cJSON.h"


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

static esp_err_t filesys_sha256_stream_md_update(mbedtls_md_context_t *md,const unsigned char *data, size_t len) {
    if (!md || (!data && len)) return ESP_ERR_INVALID_ARG;
    if (mbedtls_md_update(md, data, len) != 0) return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t filesys_sha256_stream_md_finish(mbedtls_md_context_t *md, unsigned char out_hash[32]) {
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
    void *ctx,
    const unsigned char *expected_hash, // may be NULL
    bool verify                         // false -> skip
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

    // LOG_INFO(TAG, "HTTPD task high-water: %u", uxTaskGetStackHighWaterMark(NULL));
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
    log_hash_hex((const unsigned char *)"server hash", expected_hash, 32);
    log_hash_hex((const unsigned char *)"local hash", hash, 32);

    if (verify && expected_hash) {
        if (!hashes_equal(expected_hash, hash)) {
            LOG_ERR(TAG, ESP_FAIL, "SHA256 mismatch; aborting update");
            unlink(temp_path);
            return ESP_FAIL;
        }
    }

    // Atomic rename (SPIFFS cannot overwrite existing file)
    unlink(final_path); // ignore error if file doesn't exist
    if (rename(temp_path, final_path) != 0) {
        LOG_ERR(TAG, ESP_FAIL, "atomic rename failed");
        unlink(temp_path);
        return ESP_FAIL;
    }

    LOG_INFO(TAG, "update complete: %s (%u bytes)", final_path, (unsigned)total_written);
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

esp_err_t filesys_check_file(const char *path, size_t *out_size) {
// The Quick and Dirty
// Check if a file exists at some path, and if that file's size is > 0
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

// --- Write small text atomically ---
esp_err_t filesys_write_text_atomic(const char *final_path, const char *text) {
    if (!final_path || !text) return ESP_ERR_INVALID_ARG;

    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s.tmp", final_path);

    FILE *f = fopen(tmp, "wb");
    if (!f) {
        LOG_ERR(TAG, ESP_FAIL, "open temp for write failed: %s (errno=%d)", tmp, errno);
        return ESP_FAIL;
    }
    size_t len = strlen(text);
    size_t w   = fwrite(text, 1, len, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    if (w != len) {
        LOG_ERR(TAG, ESP_FAIL, "partial write: %u/%u", (unsigned)w, (unsigned)len);
        unlink(tmp);
        return ESP_FAIL;
    }
    // SPIFFS cannot overwrite existing file, unlink first
    unlink(final_path);
    if (rename(tmp, final_path) != 0) {
        LOG_ERR(TAG, ESP_FAIL, "atomic rename failed: %s -> %s", tmp, final_path);
        unlink(tmp);
        return ESP_FAIL;
    }
    LOG_INFO(TAG, "wrote text file: %s (%u bytes)", final_path, (unsigned)len);
    return ESP_OK;
}

// --- Read whole text file into malloc'ed buffer ---
esp_err_t filesys_read_text(const char *path, char **out_text, size_t *out_len){

    if (!path || !out_text) return ESP_ERR_INVALID_ARG;
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_ERR(TAG, ESP_FAIL, "open for read failed: %s (errno=%d)", path, errno);
        return ESP_FAIL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return ESP_FAIL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return ESP_FAIL; }
    rewind(f);

    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }

    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';

    *out_text = buf;
    if (out_len) *out_len = n;
    return ESP_OK;

}

// --- Build a JSON array of files in a directory ---
char* filesys_show_directory(const char *path) {
    if (!path) return NULL;

    DIR *dir = opendir(path);
    if (!dir) {
        LOG_ERR(TAG, ESP_FAIL, "opendir failed: %s (errno=%d)", path, errno);
        return NULL;
    }

    cJSON *arr = cJSON_CreateArray();
    if (!arr) { closedir(dir); return NULL; }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        // Skip . and ..
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        char full[258];
        snprintf(full, sizeof(full), "%s/%s", path, name);

        struct stat st;
        if (stat(full, &st) != 0) {
            // Could be dangling entry; skip but log
            LOG_WARN(TAG, ESP_FAIL, "stat failed: %s (errno=%d)", full, errno);
            continue;
        }

        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", name);
        cJSON_AddNumberToObject(o, "size", (double)st.st_size);
        cJSON_AddStringToObject(
            o, "type",
            S_ISDIR(st.st_mode) ? "dir" : "file"
        );
        cJSON_AddItemToArray(arr, o);
    }
    closedir(dir);

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return json; // caller frees
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



void log_hash_hex(const unsigned char *name, const unsigned char *hash, size_t len) {
    char hex[65] = {0};
    for (size_t i = 0; i < len; ++i) {
        sprintf(hex + (i * 2), "%02x", hash[i]);
    }
    LOG_INFO(TAG, "SHA256 [%s] = \x1b[38;5;200m%s\x1b[0m", name, hex);
}

bool hex_to_bytes32(const char *hex, unsigned char out[32]) {
    if (!hex || !out) return false;
    size_t len = strlen(hex);
    if (len != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
        unsigned int v;
        if (sscanf(&hex[i*2], "%2x", &v) != 1) return false;
        out[i] = (unsigned char)v;
    }
    return true;
}

bool hashes_equal(const unsigned char a[32], const unsigned char b[32]) {
    // constant-time-ish compare
    unsigned char acc = 0;
    for (int i = 0; i < 32; ++i) acc |= (a[i] ^ b[i]);
    return acc == 0;
}
