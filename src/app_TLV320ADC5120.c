#include "app_TLV320ADC5120.h"
#include "driver_TLV320ADC5120.h"
#include "model_sample.h"
#include "models.h"
#include "util_mqtt.h"
#include "util_net_events.h"
#include "util_err.h"

// #include "driver/i2c_master.h"
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

/* ---------- Downsample 8:1 (8 x 8ms → one 64ms block @1 kHz) ---------- */

static inline int32_t sign_extend_24(uint32_t x32) { return (int32_t)(x32 << 8) >> 8; }
static inline uint32_t pack_24_right_justified(int32_t v) {
    if (v >  0x7FFFFF) v =  0x7FFFFF;
    if (v < -0x800000) v = -0x800000;
    return (uint32_t)(v & 0x00FFFFFF);
}

/* in8: 8 source blocks, out: one downsampled 512B block (64 frames, 2x32-bit each) */
static void decimate8_to1(const uint8_t in8[8][512], uint8_t out[512]) {
    const uint32_t *src[8];
    for (int k = 0; k < 8; ++k) src[k] = (const uint32_t *)in8[k];

    uint32_t *dst = (uint32_t *)out;
    for (int i = 0; i < 64; ++i) {
        int64_t accL = 0, accR = 0;
        for (int k = 0; k < 8; ++k) {
            accL += sign_extend_24(src[k][2*i + 0]);
            accR += sign_extend_24(src[k][2*i + 1]);
        }
        int32_t dL = (int32_t)(accL / 8);
        int32_t dR = (int32_t)(accR / 8);
        dst[2*i + 0] = pack_24_right_justified(dL);
        dst[2*i + 1] = pack_24_right_justified(dR);
    }
}


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

static uint32_t blocks_published = 0;
static void publish_one_block(const uint8_t raw[512]) {
    sample_t s = {
        .hw_class = DEF_HW_CLASS,
        .hw_version = DEF_HW_VERSION,
        .serial = DEF_SERIAL, // fill with MAC-based ID if available
        .seq = s_seq++,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .sample_rate_hz = 8000, // if you expose s_cfg or pass in value
    };
    
    // LOG_INFO(TAG, "Publishing to jaqc/sig/sample");
    memcpy(s.blob, raw, 512);
    
    if (!util_mqtt_is_ready()) {
        // LOG_WARN(TAG, ESP_FAIL, "MQTT not ready; drop sample seq=%u", (unsigned)s.seq);
        return;
    }

    if (blocks_published % 125 == 0) {
        cJSON *json = sample_to_json(&s);
        if (!json) {
            LOG_ERR(TAG, ESP_FAIL, "sample_to_json failed");
            return;
        }

        esp_err_t perr = util_mqtt_publish_json("jaqc/sig/sample", json, 0, false);
        cJSON_Delete(json);
        if (perr) {
            LOG_ERR(TAG, perr, "publish falied");
            return;
        } 
    
        LOG_INFO(TAG, "%lu Blocks published to jaqc/sig/sample", 
            blocks_published);
    }
    
    blocks_published++;
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

static uint32_t s_blk_count = 0;
static uint32_t s_missed_blk_count = 0;
// Task @125Hz: pop a 512B block and publish
static void sampler_task(void *arg) {
    LOG_INFO(TAG, "sampler_task started");
    uint8_t block[TLV_DMA_BUF_SZ];
    const TickType_t period = pdMS_TO_TICKS(8);
    // uint32_t enq = 0; /* TODO: Remove after debug */
    while (s_running) {
        vTaskDelay(period);

        if (tlv320adc5120_pop(block)) {

            // Try to enqueue; if queue is full, drop or block briefly
            if (xQueueSend(s_publish_q, block, 0) != pdPASS) {
            // if (xQueueSend(s_publish_q, block, pdMS_TO_TICKS(2)) == pdPASS) {
                // if ((++enq % 100) == 0) { /* TODO: Remove after debug */
                //     LOG_INFO(TAG, "sampler: %lu blocks enqueued", enq);
                // }
            // } else {
            //     LOG_WARN(TAG, ESP_FAIL, "sampler: queue full; dropping");
                s_missed_blk_count++;
            }
            s_blk_count++;

            if(s_blk_count % 64 == 0) {
                LOG_INFO(TAG, "%lu / %lu failed", s_missed_blk_count, s_blk_count);
            }
        }

        taskYIELD();
    }
    vTaskDelete(NULL);
}

// static void publisher_task(void *arg) {
//     LOG_INFO(TAG, "publisher_task starting (pre-yield)");
//     vTaskDelay(1);
//     // subscribe and feed TWDT
//     esp_task_wdt_add(NULL);

//     LOG_INFO(TAG, "publisher_task started");
//     uint8_t block[512];
//     uint32_t deq = 0, to = 0;  /* TODO: Remove after debug */

//     for (;;) {
//         if (xQueueReceive(s_publish_q, block, pdMS_TO_TICKS(2000)) == pdPASS) {
//             if ((++deq % 100) == 0) {  /* TODO: REmove after debug */
//                 LOG_INFO(TAG, "publisher: %lu blocks dequeued", deq);
//             }
//             publish_one_block(block);
//             esp_task_wdt_reset();
//         } else {
//             // woke up after timeout but no block received
//             if ((++to % 5) == 0) {  /* TODO: Remove after debug */
//                 LOG_INFO(TAG, "publisher: timeout (no blocks)");
//             }
//             esp_task_wdt_reset();
//         }
//     }

// }

/* RAW DATA MESSAGE V1 **********************************************/
// typedef struct __attribute__((packed)) {
//     uint8_t  magic[4];        // "JQBR" = JaQC Binary Raw
//     uint8_t  version;         // 0x01
//     uint8_t  flags;           // bit0: LE=1, bit1: Philips-I2S=1
//     uint16_t hdr_len;         // bytes in header (including this field) = 24
//     uint32_t seq;             // running sequence
//     uint32_t ts_ms;           // wall time in ms
//     uint16_t sample_rate;     // e.g., 8000
//     uint8_t  ch_count;        // 2
//     uint8_t  word_bits;       // 24
//     uint8_t  slot_bits;       // 32
//     uint8_t  reserved;        // align/reserved
//     uint32_t dev_id;          // 32-bit device id (e.g., MAC hash or NVS id)
// } sample_hdr_v1_t;

// static uint32_t s_dev_id = 0xA1B2C3D4; // TODO: derive from MAC/NVS

// static char s_topic_raw[64]; // e.g., "jaqc/sig/sample/raw/v1/ABC123"
// static inline void make_raw_topic(void) {
//     snprintf(s_topic_raw, sizeof(s_topic_raw), "jaqc/sig/sample/raw/v1/%08X", (unsigned)s_dev_id);
// }

// static void publisher_task(void *arg) {
//     make_raw_topic();
//     // (optional) esp_task_wdt_add(NULL);
//     uint8_t block[512];
//     for (;;) {
//         if (xQueueReceive(s_publish_q, block, pdMS_TO_TICKS(2000)) == pdPASS) {
//             // Build header
//             sample_hdr_v1_t hdr;
//             memcpy(hdr.magic, "JQBR", 4);
//             hdr.version     = 0x01;
//             hdr.flags       = 0x03;   // LE + Philips-I2S
//             hdr.hdr_len     = (uint16_t)sizeof(sample_hdr_v1_t);
//             hdr.seq         = s_seq++;
//             hdr.ts_ms       = (uint32_t)(esp_timer_get_time() / 1000);
//             hdr.sample_rate = 8000;
//             hdr.ch_count    = 2;
//             hdr.word_bits   = 24;
//             hdr.slot_bits   = 32;
//             hdr.reserved    = 0;
//             hdr.dev_id      = s_dev_id;

//             // Serialize: header + block
//             uint8_t payload[sizeof(sample_hdr_v1_t) + 512];
//             memcpy(payload, &hdr, sizeof(hdr));
//             memcpy(payload + sizeof(hdr), block, 512);

//             // Gate on MQTT ready (no throttling, every block)
//             if (util_mqtt_is_ready()) {
//                 esp_err_t perr = util_mqtt_publish_bytes(s_topic_raw, payload, sizeof(payload), /*qos*/0, /*retain*/false);
//                 if (perr != ESP_OK) {
//                     LOG_ERR(TAG, perr, "publish raw failed (seq=%u)", (unsigned)hdr.seq);
//                 }
//             } else {
//                 // If not ready, you *must* decide: drop or buffer? See back-pressure section.
//                 LOG_WARN(TAG, ESP_FAIL, "MQTT not ready; drop raw seq=%u", (unsigned)hdr.seq);
//             }
//         } else {
//             // timeout: optional TWDT feed
//         }
//         taskYIELD();
//     }
// }

/* END RAW DATA MESSAGE V1 ******************************************/

/* RAW DATA MESSAGE V2 **********************************************/
// Little-Endian V2 header
typedef struct __attribute__((packed)) {
    uint8_t  magic[4];     // "JQMB"
    uint8_t  version;      // 0x02
    uint8_t  flags;        // bit0: LE=1, bit1: Philips-I2S=1
    uint16_t hdr_len;      // sizeof this header
    uint32_t seq_first;    // sequence of the first block in this batch
    uint16_t block_count;  // N blocks in this payload
    uint16_t block_size;   // bytes per block (512)
    uint32_t ts_ms;        // timestamp of first block (or batch time)
    uint16_t sample_rate;  // e.g., 8000
    uint8_t  ch_count;     // 2
    uint8_t  word_bits;    // 24
    uint8_t  slot_bits;    // 32
    uint8_t  reserved;     // align/reserved
    uint32_t dev_id;       // device id (32-bit)
} sample_mb_hdr_v2_t;

#define DS_N_SOURCE         8       // batch size
#define DS_BLOCK_BYTES      512
#define BATCH_DS_BLOCKS     4       // publish 4 ds blocks per MQTT msg
#define TOPIC_MAX           64

static uint32_t s_dev_id = 0xA1B2C3D4; // TODO: derive from MAC/NVS
static char s_topic_raw[TOPIC_MAX];

static inline void make_raw_topic(void) {
    snprintf(s_topic_raw, sizeof(s_topic_raw), "jaqc/sig/sample/raw/v2/%08X", (unsigned)s_dev_id);
}

static void publisher_task(void *arg) {
    LOG_INFO(TAG, "publisher_task started");
    make_raw_topic();

    /* header template */
    sample_mb_hdr_v2_t hdr = {0};
    memcpy(hdr.magic, "JQMB", 4);
    hdr.version     = 0x02;
    hdr.flags       = 0x03;
    hdr.hdr_len     = sizeof(hdr);
    hdr.sample_rate = 1000;
    hdr.ch_count    = 2;
    hdr.word_bits   = 24;
    hdr.slot_bits   = 32;
    hdr.reserved    = 0;
    hdr.dev_id      = s_dev_id;
    hdr.block_size  = DS_BLOCK_BYTES;

    /* buffers */
    uint8_t src8[DS_N_SOURCE][DS_BLOCK_BYTES];   // accumulate 8 raw blocks
    uint8_t ds_block[DS_BLOCK_BYTES];            // one downsampled block
    uint8_t payload[sizeof(hdr) + BATCH_DS_BLOCKS * DS_BLOCK_BYTES];

    uint8_t *pay_body = payload + sizeof(hdr);

    while (1) {
        /* Build one downsampled block from 8 source blocks */
        int got_src = 0;
        while (got_src < DS_N_SOURCE) {
            if (xQueueReceive(s_publish_q, src8[got_src], pdMS_TO_TICKS(16)) == pdPASS) {
                got_src++;
            } else {
                taskYIELD();
            }
        }
        decimate8_to1(src8, ds_block);

        /* Batch 4 ds blocks per publish */
        static int   ds_in_batch = 0;
        static uint32_t first_seq = 0;
        static uint32_t ts0_ms    = 0;

        if (ds_in_batch == 0) {
            first_seq = s_seq;
            ts0_ms    = (uint32_t)(esp_timer_get_time() / 1000);
        }

        memcpy(pay_body + ds_in_batch * DS_BLOCK_BYTES, ds_block, DS_BLOCK_BYTES);
        ds_in_batch++;
        s_seq++;

        if (ds_in_batch == BATCH_DS_BLOCKS) {
            /* finalize header */
            hdr.seq_first   = first_seq;
            hdr.block_count = BATCH_DS_BLOCKS;
            hdr.ts_ms       = ts0_ms;

            /* serialize hdr + body */
            memcpy(payload, &hdr, sizeof(hdr));

            /* publish (QoS0, retain=false) */
            if (util_mqtt_is_ready()) {
                esp_err_t perr = util_mqtt_publish_bytes(
                    s_topic_raw, payload, sizeof(payload), 0, false);
                if (perr != ESP_OK) {
                    LOG_ERR(TAG, perr, "publish failed (seq_first=%u)", (unsigned)first_seq);
                }
            }

            /* reset batch */
            ds_in_batch = 0;
        }

        taskYIELD();
    }

}


/* END RAW DATA MESSAGE V2 ******************************************/




void startup_task(void *arg) {
    // Safe to log here; this task has a bigger stack than main
    BaseType_t rc;
    // Start the sampler task on core 1, moderate priority (3)
    // This task will ONLY pop from the ring and enqueue to s_publish_q.
    rc = xTaskCreatePinnedToCore(sampler_task, "tlv_samp", 4096, NULL, 3, NULL, 1);
    LOG_INFO(TAG, "sampler_task create rc=%ld", (long)rc);

    // Start the publisher task on core 1, lower priority (2)
    // This task will do the heavier JSON/Base64/MQTT work.
    rc = xTaskCreatePinnedToCore(publisher_task, "tlv_pub", 8192, NULL, 2, NULL, 0);
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
        .gpio_bclk = 36, 
        .gpio_ws = 38, 
        .gpio_din = 35,

        .sample_rate_hz = 8000,
        .word_bits = 24,
        .slot_bits = 32,
    };

    ESP_ERROR_CHECK(tlv320adc5120_init(&cfg));
    ESP_ERROR_CHECK(tlv320adc5120_start());


    // Create a queue that holds 512-byte samples (set depth to e.g. 16)
    if (!s_publish_q) {
        s_publish_q = xQueueCreate(/*depth*/256, /*item size*/BLOCK_BYTES);
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
