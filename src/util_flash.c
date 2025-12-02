#include "util_flash.h"
#include "util_err.h"

#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h" 

#include <string.h>

static const char *TAG = "UTIL_FLASH";

esp_err_t flash_init(void) {

    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES 
    ||  err == ESP_ERR_NVS_NEW_VERSION_FOUND
    ) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_RETURN_ON_ERROR(err, TAG, "flash_init");
    return ESP_OK;
}

esp_err_t flash_open(char *name, nvs_handle_t *handle) {
    return nvs_open(name, NVS_READWRITE, handle);
}

esp_err_t flash_commit(nvs_handle_t handle) {
    return nvs_commit(handle);
}

esp_err_t flash_list_namespase_keys(const char *namespace) {
    nvs_iterator_t it = NULL; 
    esp_err_t err = nvs_entry_find(FLASH_PARTITION, namespace, NVS_TYPE_ANY, &it);
    if (err != ESP_OK) {
        LOG_WARN(TAG, err, "failed to find NVS entry '%s'", namespace);
    } else {
        while (it != NULL) {
            nvs_entry_info_t info;
            ESP_RETURN_ON_ERROR(nvs_entry_info(it, &info), TAG, "failed to read NSV entry");
            LOG_INFO(TAG, "Key: %s, Type: %d", info.key, info.type);
            
            err = nvs_entry_next(&it);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                err = ESP_OK;
                break;
            } else if (err != ESP_OK) {
                LOG_ERR(TAG, err, "failed to advance NVS iterator");
            }
        }
        nvs_release_iterator(it);
    }
    return err;
}


// === GETTERS ===

esp_err_t flash_get_bool(nvs_handle_t handle, const char *key, bool *dest, size_t size) {
    (void)size; // unused
    uint8_t val;
    esp_err_t err = nvs_get_u8(handle, key, &val);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *dest = false;
        return ESP_OK;
    }
    *dest = val ? true : false;
    return err;
}

esp_err_t flash_get_int(nvs_handle_t handle, const char *key, int *dest, size_t size) {
    (void)size; // unused
    int32_t temp;
    esp_err_t err = nvs_get_i32(handle, key, &temp);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *dest = 0;
        return ESP_OK;
    }
    *dest = (int)temp;
    return err;
}

esp_err_t flash_get_i8(nvs_handle_t handle, const char *key, int8_t *dest, size_t size) {
    (void)size;
    esp_err_t err = nvs_get_i8(handle, key, dest);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *dest = 0;
        return ESP_OK;
    }
    return err;
}

esp_err_t flash_get_i32(nvs_handle_t handle, const char *key, int32_t *dest, size_t size) {
    (void)size;
    esp_err_t err = nvs_get_i32(handle, key, dest);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *dest = 0;
        return ESP_OK;
    }
    return err;
}

esp_err_t flash_get_u8(nvs_handle_t handle, const char *key, uint8_t *dest, size_t size) {
    (void)size;
    esp_err_t err = nvs_get_u8(handle, key, dest);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *dest = 0;
        return ESP_OK;
    }
    return err;
}

esp_err_t flash_get_u32(nvs_handle_t handle, const char *key, uint32_t *dest, size_t size) {
    (void)size;
    esp_err_t err = nvs_get_u32(handle, key, dest);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *dest = 0;
        return ESP_OK;
    }
    return err;
}

esp_err_t flash_get_float(nvs_handle_t handle, const char *key, float *dest, size_t size) {
    (void)size;
    esp_err_t err = nvs_get_blob(handle, key, dest, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *dest = 0.0f;
        return ESP_OK;
    }
    return err;
}

esp_err_t flash_get_str(nvs_handle_t handle, const char *key, char *dest, size_t size) {
    size_t required_size = size; // ESP_LOGI(TAG, "Attempting to read key '%s' with buffer size %d", key, size);
    esp_err_t err = nvs_get_str(handle, key, dest, &required_size); // ESP_LOGI(TAG, "Required size for 'serial': %d", required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        dest[0] = '\0';
        return ESP_OK;
    } else if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        LOG_ERR(TAG, err, "Invalid length for key '%s'", key);
        dest[0] = '\0';
        return err;
    } else if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}


// === SETTERS ===

esp_err_t flash_set_bool(nvs_handle_t handle, const char *key, bool value) {
    return nvs_set_u8(handle, key, value ? 1 : 0);
}

esp_err_t flash_set_int(nvs_handle_t handle, const char *key, int value) {
    return nvs_set_i32(handle, key, (int32_t)value);
}

esp_err_t flash_set_i32(nvs_handle_t handle, const char *key, int32_t value) {
    return nvs_set_i32(handle, key, value);
}

esp_err_t flash_set_float(nvs_handle_t handle, const char *key, float value) {
    return nvs_set_blob(handle, key, &value, sizeof(float));
}

esp_err_t flash_set_str(nvs_handle_t handle, const char *key, const char *value) {
    return nvs_set_str(handle, key, value);
}
