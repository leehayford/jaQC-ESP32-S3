#ifndef MODEL_SAMPLE_H
#define MODEL_SAMPLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Device metadata
    const char *hw_class;     // e.g., "JAQC"
    const char *hw_version;   // e.g., "S3-RevA"
    const char *serial;       // e.g., MAC suffix

    // Test metadata
    uint32_t seq;             // incrementing
    uint32_t timestamp_ms;    // uptime-based or RTC
    uint32_t sample_rate_hz;  // 8000

    // Raw DMA block
    uint8_t  blob[512];
} sample_t;

// Serialize to JSON (blob base64 to keep MQTT-friendly)
cJSON *sample_to_json(const sample_t *s);

#ifdef __cplusplus
}
#endif

#endif // MODEL_SAMPLE_H