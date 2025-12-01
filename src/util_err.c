#include "esp_err.h"
#include "esp_log.h"
#include "esp_console.h"
#include <string.h>

#define ANSI_COLOR_INFO     "\x1b[38;5;79m"
#define ANSI_COLOR_WARN     "\x1b[38;5;208m"
#define ANSI_COLOR_ERROR    "\x1b[48;5;88m"
#define ANSI_COLOR_RESET    "\x1b[0m"

void log_err(const char *TAG, const char *context, esp_err_t err) {
    char tag[50] = ANSI_COLOR_ERROR;
    strcat(tag, TAG);
    strcat(tag, ANSI_COLOR_RESET);
    ESP_LOGE(tag, "%s: %s", context, esp_err_to_name(err));
}

void log_warn(const char *TAG, const char *context, esp_err_t err) {
    char tag[50] = ANSI_COLOR_WARN;
    strcat(tag, TAG);
    strcat(tag, ANSI_COLOR_RESET);
    ESP_LOGW(tag, "%s: %s", context, esp_err_to_name(err));
}

void log_info(const char *TAG, const char *context) {
    char tag[50] = ANSI_COLOR_INFO;
    strcat(tag, TAG);
    strcat(tag, ANSI_COLOR_RESET);
    ESP_LOGI(tag, "%s", context);
}
