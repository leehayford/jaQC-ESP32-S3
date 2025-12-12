#ifndef DRIVER_TLV320ADC5120_H
#define DRIVER_TLV320ADC5120_H

#include "esp_err.h"

#include "driver/i2c.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


// DMA ring: 8 buffers x 512B, matches 125Hz read cadence (8ms per buffer)
#define TLV_DMA_BUF_SZ     512
#define TLV_DMA_BUF_COUNT  8

typedef struct {
    // ring buffer for raw bytes read from I2S
    uint8_t   dma_buf[TLV_DMA_BUF_COUNT][TLV_DMA_BUF_SZ];
    volatile int wr_idx;    // written by reader task
    volatile int rd_idx;    // consumed by tlv320adc5120_pop()
} tlv320adc5120_dma_ring_t;


// ---- I2C + I2S pin/map config ----
typedef struct {
    // I2C
    int i2c_port;          // I2C_NUM_0 / I2C_NUM_1
    int gpio_sda;          // e.g., 8
    int gpio_scl;          // e.g., 9
    uint32_t i2c_freq_hz;  // e.g., 400000
    uint8_t i2c_addr;      // 7-bit TLV320ADC5120 address

    // I2S RX (ESP as slave, ADC provides BCLK/FSYNC)
    int i2s_port;          // I2S_NUM_0 / I2S_NUM_1
    int gpio_bclk;         // e.g., 36
    int gpio_ws;           // e.g., 38  (FSYNC/LRCLK)
    int gpio_din;          // e.g., 35  (SDOUT from ADC)

    // Audio config
    uint32_t sample_rate_hz; // 8000
    int word_bits;           // 24 (ADC word length)
    int slot_bits;           // 32 (I2S slot width on ESP RX; use 32 for alignment)
} tlv320adc5120_bus_cfg_t;

// Public API
void i2c_scan(void);
esp_err_t tlv320adc5120_init(const tlv320adc5120_bus_cfg_t *cfg);
esp_err_t tlv320adc5120_start(void);
esp_err_t tlv320adc5120_stop(void);
esp_err_t tlv320adc5120_deinit(void);

// const uint8_t ADDR = 0x4E; 
#define TLV_I2C_ADDR 0x4E
// Helper to write one register
#define TLV_WR(reg, val) \
    i2c_master_write_to_device(s_cfg.i2c_port, TLV_I2C_ADDR, (uint8_t[]){ (reg), (val) }, 2, pdMS_TO_TICKS(20))


// Ring access
bool tlv320adc5120_pop(uint8_t out[TLV_DMA_BUF_SZ]); // copy one 512B block if available




#ifdef __cplusplus
}
#endif

#endif // DRIVER_TLV320ADC5120_H