#ifndef UTIL_ERR_H
#define UTIL_ERR_H


#include <stdio.h>
#include <stdarg.h>
#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

void log_err(const char *TAG, const char *context, esp_err_t err);
#define LOG_ERR(tag, err, fmt, ...) do { \
    char _msg[512]; \
    snprintf(_msg, sizeof(_msg), fmt, ##__VA_ARGS__); \
    log_err((tag), (_msg), (err)); \
} while(0)

void log_warn(const char *TAG, const char *context, esp_err_t err);
#define LOG_WARN(tag, err, fmt, ...) do { \
    char _msg[512]; \
    snprintf(_msg, sizeof(_msg), fmt, ##__VA_ARGS__); \
    log_warn((tag), (_msg), (err)); \
} while(0)

void log_info(const char *TAG, const char *context);
#define LOG_INFO(tag, fmt, ...) do { \
    char _msg[512]; \
    snprintf(_msg, sizeof(_msg), fmt, ##__VA_ARGS__); \
    log_info((tag), (_msg)); \
} while(0)

#ifdef __cplusplus
}
#endif


#endif //UTIL_ERR_H