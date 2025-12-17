#include "app_TLV320ADC5120.h"
#include "driver_TLV320ADC5120.h"
#include "model_sample.h"
#include "models.h"
#include "util_mqtt.h"
#include "util_net_events.h"
#include "util_err.h"

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"  // only if you want to feed TWDT from tasks later


#include "cJSON.h"

static const char *TAG = "APP_TLV";
static volatile bool s_running = false;
static uint32_t s_seq = 0;

// Queue for handing off 512-byte blocks to the publisher
static QueueHandle_t s_publish_q = NULL;

// Forward declarations
static void sampler_task(void *arg);
static void publisher_task(void *arg);


// Convert I2S 32-bit words containing right-justified 24-bit samples to float volts.
// ADC single-ended full-scale is 1 VRMS; scale to instantaneous voltage as needed. [1](https://www.ti.com/lit/ds/symlink/tlv320adc5120.pdf?ts=1751362068444)
static inline float s32_to_volts(int32_t x) {
    // x: 24-bit sample in low bits of 32-bit slot (right-justified). Sign-extend 24→32.
    x = (x << 8) >> 8;
    // Normalize to [-1, 1] using 23-bit magnitude (signed 24-bit)
    const float norm = (float)x / (float)(1 << 23);
    // If you want peak volts referenced to 1.0 FS, multiply by 1.0 (VRMS scaling is application-specific).
    return norm; // "FS=1.0" units; you can later multiply by actual FS volts if needed
}

// Current from INA outputs: Vout = Gain * I * Rshunt → I = Vout / (Gain * R)
// INA213 gain=50; INA212 gain=1000; R=2Ω. [4](https://www.ti.com/lit/ds/symlink/ina212.pdf)
static inline float volts_to_current_A(float v_out, float gain) {
    const float R = 2.0f; // ohms
    return v_out / (gain * R);
}

static void publish_one_block(const uint8_t raw[512]) {
    sample_t s = {
        .hw_class = DEF_HW_CLASS,
        .hw_version = DEF_HW_VERSION,
        .serial = DEF_SERIAL, // fill with MAC-based ID if available
        .seq = s_seq++,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .sample_rate_hz = 8000, // if you expose s_cfg or pass in value
    };
    
    LOG_INFO(TAG, "Publishing to jaqc/sig/sample");
    memcpy(s.blob, raw, 512);
    cJSON *json = sample_to_json(&s);
    if (json) {
        util_mqtt_publish_json("jaqc/sig/sample", json, 0, false);
        cJSON_Delete(json);
        LOG_INFO(TAG, "Published to jaqc/sig/sample");
    }
    
    // Optional: parse the 512B as 64 stereo frames of 32-bit slots and compute currents
    // for (int i = 0; i < 64; ++i) {
    //     int32_t ch1 = ((int32_t*)block)[2*i + 0];
    //     int32_t ch2 = ((int32_t*)block)[2*i + 1];
    //     float v1 = s32_to_volts(ch1);
    //     float v2 = s32_to_volts(ch2);
    //     float i_lo = volts_to_current_A(v1, 50.0f);    // INA213
    //     float i_hi = volts_to_current_A(v2, 1000.0f);  // INA212
    //     // ... accumulate if needed ...
    // }
}

// Task @125Hz: pop a 512B block and publish
static void sampler_task(void *arg) {
    LOG_INFO(TAG, "sampler_task started");
    uint8_t block[TLV_DMA_BUF_SZ];
    const TickType_t period = pdMS_TO_TICKS(8);
    uint32_t enq = 0; /* TODO: Remove after debug */
    while (s_running) {
        vTaskDelay(period);

        if (tlv320adc5120_pop(block)) {
            
            // Try to enqueue; if queue is full, drop or block briefly
            // if (xQueueSend(s_publish_q, block, 0) == pdPASS) {
            if (xQueueSend(s_publish_q, block, pdMS_TO_TICKS(2)) == pdPASS) {
                if ((++enq % 100) == 0) { /* TODO: Remove after debug */
                    LOG_INFO(TAG, "sampler: %lu blocks enqueued", enq);
                }
            } else {
                LOG_WARN(TAG, ESP_FAIL, "sampler: queue full; dropping");
            }

        }

        taskYIELD();
    }
    vTaskDelete(NULL);
}

static void publisher_task(void *arg) {
    LOG_INFO(TAG, "publisher_task starting (pre-yield)");
    vTaskDelay(1);
    // subscribe and feed TWDT
    esp_task_wdt_add(NULL);

    LOG_INFO(TAG, "publisher_task started");
    uint8_t block[512];
    uint32_t deq = 0, to = 0;  /* TODO: Remove after debug */

    for (;;) {
        if (xQueueReceive(s_publish_q, block, pdMS_TO_TICKS(2000)) == pdPASS) {
            if ((++deq % 100) == 0) {  /* TODO: REmove after debug */
                LOG_INFO(TAG, "publisher: %lu blocks dequeued", deq);
            }
            publish_one_block(block);
            esp_task_wdt_reset();
        } else {
            // woke up after timeout but no block received
            if ((++to % 5) == 0) {  /* TODO: Remove after debug */
                LOG_INFO(TAG, "publisher: timeout (no blocks)");
            }
            esp_task_wdt_reset();
        }
    }

}

static void startup_task(void *arg) {
    // Safe to log here; this task has a bigger stack than main
    BaseType_t rc;
    // Start the sampler task on core 1, moderate priority (3)
    // This task will ONLY pop from the ring and enqueue to s_publish_q.
    rc = xTaskCreatePinnedToCore(sampler_task, "tlv_samp", 4096, NULL, 3, NULL, 1);
    LOG_INFO(TAG, "sampler_task create rc=%ld", (long)rc);

    // Start the publisher task on core 1, lower priority (2)
    // This task will do the heavier JSON/Base64/MQTT work.
    rc = xTaskCreatePinnedToCore(publisher_task, "tlv_pub", 6144, NULL, 2, NULL, 0);
    LOG_INFO(TAG, "publisher_task create rc=%ld", (long)rc);

    // vTaskDelay(pdMS_TO_TICKS(1));

    vTaskDelete(NULL);
}

esp_err_t app_tlv_start(void) {

    // Bus config (pins from your wiring)
    tlv320adc5120_bus_cfg_t cfg = {
        .i2c_port = I2C_NUM_0,
        .gpio_sda = 8, .gpio_scl = 9,
        .i2c_freq_hz = 400000,
        .i2c_addr = TLV_I2C_ADDR,
        .i2s_port = I2S_NUM_0,
        .gpio_bclk = 36, .gpio_ws = 38, .gpio_din = 35,

        .sample_rate_hz = 8000,
        .word_bits = 24,
        .slot_bits = 32,
    };

    ESP_ERROR_CHECK(tlv320adc5120_init(&cfg));
    ESP_ERROR_CHECK(tlv320adc5120_start());


    // Create a queue that holds 512-byte samples (set depth to e.g. 16)
    if (!s_publish_q) {
        s_publish_q = xQueueCreate(/*depth*/16, /*item size*/512);
    }
    // configASSERT(s_publish_q != NULL);
    
    // Signal tasks to run BEFORE creating them
    s_running = true;

    // xTaskCreatePinnedToCore(sampler_task, "tlv_samp", 4096, NULL, 3, NULL, /*core*/ 1));
    // xTaskCreatePinnedToCore(publisher_task, "tlv_pub", 4096, NULL, 2, NULL, /*core*/ 1));

    xTaskCreatePinnedToCore(startup_task, "startup", 6144, NULL, 5, NULL, 1);
    
    return ESP_OK;
}

esp_err_t app_tlv_stop(void) {
    s_running = false;
    ESP_ERROR_CHECK(tlv320adc5120_stop());
    ESP_ERROR_CHECK(tlv320adc5120_deinit());
    return ESP_OK;
}
