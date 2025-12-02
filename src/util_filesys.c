#include "util_filesys.h"
#include "util_err.h"

#include "esp_spiffs.h"


static const char *TAG = "FILESYS";

esp_err_t filesys_init(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/storage",
        .partition_label = "storage",
        .max_files=8,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK) {
        size_t total = 0, used = 0;
        esp_spiffs_info(conf.partition_label, &total, &used);
        LOG_INFO(TAG, 
            "SPIFFS mounted at %s (%.1f KB used / %.1f KB total)", 
            conf.base_path, used/1024.0, 
            total/1024.0
        );
    } else {
        LOG_ERR(TAG, err, "failed to mount file system");
    }
    return err;
}