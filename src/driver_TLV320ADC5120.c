#include "driver_TLV320ADC5120.h"
#include "util_err.h"

#include "esp_mac.h"
#include "esp_err.h"
#include "esp_check.h"

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include <string.h>


static const char *TAG = "TLV320DRV";

static tlv320adc5120_bus_cfg_t s_cfg;
static i2s_chan_handle_t s_rx_chan = NULL;
static tlv320adc5120_dma_ring_t s_ring;


// ---------------- I2C helpers ----------------
static esp_err_t i2c_write_reg(uint8_t reg, const uint8_t *data, size_t len) {
    return  
    i2c_master_write_to_device(
        s_cfg.i2c_port, s_cfg.i2c_addr, (uint8_t[]){reg}, 1, 10 / portTICK_PERIOD_MS) 
    ||     
    i2c_master_write_to_device(
        s_cfg.i2c_port, s_cfg.i2c_addr, data, len, 10 / portTICK_PERIOD_MS);
}

static esp_err_t i2c_read_reg(uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(
        s_cfg.i2c_port, s_cfg.i2c_addr, &reg, 1, data, len, 10 / portTICK_PERIOD_MS);
}

void i2c_scan(void) {
    // 1) Configure the bus (SDA=IO8, SCL=IO9, 400kHz)
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 8,  .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = 9,  .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    // 2) Probe addresses 0x03..0x77 using a command link
    for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        // 7-bit address, write phase (LSB=0)
        ESP_ERROR_CHECK(i2c_master_start(cmd));
        ESP_ERROR_CHECK(i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true /*expect ACK*/));
        ESP_ERROR_CHECK(i2c_master_stop(cmd));
        esp_err_t r = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(20));
        i2c_cmd_link_delete(cmd);

        if (r == ESP_OK) {
            LOG_INFO(TAG, "Found I2C device at 0x%02X", addr);
        }
    }

    i2c_driver_delete(I2C_NUM_0);
}

static void tlv_dump_regs(void) {
    uint8_t v;

    LOG_INFO(TAG, "TLV Page 0:");
    TLV_WR(0x00, 0x00);
    i2c_read_reg(0x02, &v, 1); LOG_INFO(TAG, "TLV 0x02 SLEEP_CFG = 0x%02X", v);
    i2c_read_reg(0x05, &v, 1); LOG_INFO(TAG, "TLV 0x05 SHDN_CFG = 0x%02X", v);
    i2c_read_reg(0x07, &v, 1); LOG_INFO(TAG, "TLV 0x07 ASI_CFG0 = 0x%02X", v);
    
    i2c_read_reg(0x0C, &v, 1); LOG_INFO(TAG, "TLV 0x0C ASI_CH2 = 0x%02X", v);
    i2c_read_reg(0x0D, &v, 1); LOG_INFO(TAG, "TLV 0x0D ASI_CH3 = 0x%02X", v);
    i2c_read_reg(0x0E, &v, 1); LOG_INFO(TAG, "TLV 0x0E ASI_CH4 = 0x%02X", v);

    i2c_read_reg(0x13, &v, 1); LOG_INFO(TAG, "TLV 0x13 MST_CFG0 = 0x%02X", v);
    i2c_read_reg(0x1F, &v, 1); LOG_INFO(TAG, "TLV 0x1F PDMCLK_CFG = 0x%02X", v);

    i2c_read_reg(0x3C, &v, 1); LOG_INFO(TAG, "TLV 0x3C CH1_CFG0 = 0x%02X", v);
    i2c_read_reg(0x41, &v, 1); LOG_INFO(TAG, "TLV 0x41 CH2_CFG0 = 0x%02X", v);

    i2c_read_reg(0x6B, &v, 1); LOG_INFO(TAG, "TLV 0x6B DSP_CFG0 = 0x%02X", v);
    i2c_read_reg(0x74, &v, 1); LOG_INFO(TAG, "TLV 0x74 ASI_OUT_CH_EN = 0x%02X", v);
    
    LOG_INFO(TAG, "TLV Page 1:");
    TLV_WR(0x00, 0x01);
    i2c_read_reg(0x1E, &v, 1); LOG_INFO(TAG, "TLV 0x1E VAD_CFG1 = 0x%02X", v);
    i2c_read_reg(0x1F, &v, 1); LOG_INFO(TAG, "TLV 0x1F VAD_CFG2 = 0x%02X", v);
    
    LOG_INFO(TAG, "TLV Page 4:");
    TLV_WR(0x00, 0x04);
    i2c_read_reg(0x48, &v, 1); LOG_INFO(TAG, "TLV 0x48 N0 coefficient byte[31:24] = 0x%02X", v);
    i2c_read_reg(0x49, &v, 1); LOG_INFO(TAG, "TLV 0x49 N0 coefficient byte[23:16] = 0x%02X", v);
    i2c_read_reg(0x4A, &v, 1); LOG_INFO(TAG, "TLV 0x4A N0 coefficient byte[15:8] = 0x%02X", v);
    i2c_read_reg(0x4B, &v, 1); LOG_INFO(TAG, "TLV 0x4B N0 coefficient byte[7:0] = 0x%02X", v);
    
    i2c_read_reg(0x4C, &v, 1); LOG_INFO(TAG, "TLV 0x4C N1 coefficient byte[31:24] = 0x%02X", v);
    i2c_read_reg(0x4D, &v, 1); LOG_INFO(TAG, "TLV 0x4D N1 coefficient byte[23:16] = 0x%02X", v);
    i2c_read_reg(0x4E, &v, 1); LOG_INFO(TAG, "TLV 0x4E N1 coefficient byte[15:8] = 0x%02X", v);
    i2c_read_reg(0x4F, &v, 1); LOG_INFO(TAG, "TLV 0x4F N1 coefficient byte[7:0] = 0x%02X", v);
    
    i2c_read_reg(0x50, &v, 1); LOG_INFO(TAG, "TLV 0x50 D1 coefficient byte[31:24] = 0x%02X", v);
    i2c_read_reg(0x51, &v, 1); LOG_INFO(TAG, "TLV 0x51 D1 coefficient byte[23:16] = 0x%02X", v);
    i2c_read_reg(0x52, &v, 1); LOG_INFO(TAG, "TLV 0x52 D1 coefficient byte[15:8] = 0x%02X", v);
    i2c_read_reg(0x53, &v, 1); LOG_INFO(TAG, "TLV 0x53 D1 coefficient byte[7:0] = 0x%02X", v);
}

static esp_err_t tlv_reset_device_pg0() {
    esp_err_t err = ESP_OK;

    /* 0x00 - PAGE_CFG Register SELECT PAGE 0
    7-0 00000000    Set device page to zero 0  
    */ 
   err |= TLV_WR(0x00, 0x00); // 0000 0000 - 0x00
    
    /* 0x01 - SW_RESET Register (Reset everything before we start writing our stuff)
    7-1 0000000     RESERVED; Write only reset value (0000000b)
    0   1           Reset all registers to their reset values              
    */ 
   err |= TLV_WR(0x01, 0x01); // 0000 0001 - 0x01

    if (err == ESP_OK) {
        LOG_INFO(TAG, "TLV320ADC5120 device reset script applied (addr 0x4E).");
        LOG_INFO(TAG, "All values set to default.");
        // tlv_dump_regs();
    } else {
        LOG_ERR(TAG, ESP_FAIL, "TLV320ADC5120 device reset script failed");
    }
    return err;
}

// ---------------- TLV320ADC5120 register init (I2C) ----------------
static esp_err_t tlv_cfg_device(void) {
    esp_err_t err = ESP_OK;

    // Start by resetting all page 0 registers to default. 
    // Use tlv_reset_device_pg0();
    // Then write only to those we want to change.

    /* 0x00 - PAGE_CFG Register SELECT PAGE 0 ***************************************************
    7-0 00000000    Set device page to zero 0   
    */ 
    err |= TLV_WR(0x00, 0x00); // 0000 0000 - 0x00
    
    /* 0x02 - SLEEP_CFG Register
    7   1           Internally generated 1.8-V AREG supply using an on-chip regulator (use this setting when AVDD is 3.3 V)
    6-5 00          RESERVED; Write only reset value (00b) 
    4-3 00          VREF quick-charge duration of 3.5 ms   
    2   0           I2C broadcast mode disabled
    1   0           RESERVED; Write only reset value (0b)
    0   1           Device is not in sleep mode
    */ 
   err |= TLV_WR(0x02, 0x81); // 1000 0001 - 0x81 

    /* 0x05 - SHDN_CFG Register
    7-6 00          RESERVED; Write only reset value (00b)
    5-4 00          INxP, INxM quick-charge duration of 2.5 ms
    3-2 00          RESERVED; Write only reset value (01b) But PPC3 told me to write 00b so... 
    1-0 01          RESERVED; Write only reset value 01b)
    */ 
   err |= TLV_WR(0x05, 0x01); // 0000 0001 - 0x01

    /* 0x07 - ASI_CFG0 Register
    7-6 01          I2S model
    5-4 11          Slot width 32 bits
    3   0           Default FSYNC polarity 
    2   0           Default BCLK polarity
    1   0           Default transmit edge ()
    0   0           Transmit 0 for unused cycles
    */ 
   err |= TLV_WR(0x07, 0x70); // 0111 0000 - 0x70 - I2S - 32 bit slot
//    err |= TLV_WR(0x07, 0x60); // 0011 0000 - 0x70 - TDM - 32 bit slot
    
    /* 0x0B - ASI_CH1 Register
    7-6 00          RESERVED; Write only reset value (00b)
    5-0 000000      Ch1 is registered to I2S left slot 0
    */ 
   err |= TLV_WR(0x0B, 0x00); // 0000 0000 - 0x00

    /* 0x0C - ASI_CH2 Register
    7-6 00          RESERVED; Write only reset value (00b)
    5-0 000001      Ch2 is registered to I2S left slot 1
    */ 
   err |= TLV_WR(0x0C, 0x01); // 0000 0001 - 0x01

    /* 0x0D - ASI_CH3 Register
    7-6 00          RESERVED; Write only reset value (00b)
    5-0 000000      Ch3 is NOT USED but PPC3 wants it registered to I2S left slot 0
    */ 
   err |= TLV_WR(0x0D, 0x00); // 0000 0000 - 0x00
    
    /* 0x0E - ASI_CH4 Register
    7-6 00          RESERVED; Write only reset value (00b)
    5-0 000000      Ch4 is NOT USED but PPC3 wants it registered to I2S left slot 0
    */ 
   err |= TLV_WR(0x0E, 0x00); // 0000 0000- 0x00
    
    /* 0x13 - MST_CFG0 Register
    7   0           SLAVE MODE; both BCLK and FSYNC are inputs to ADC
    6   0           Auto clock config enabled 
    5   0           Auto clock setting enables PLL
    4   0           Do not 'gate' BCLK and FSYNC (because ADC is slave)
    3   0           Sample rate is multible of 48 kHz (doesn't mater because ADC is slave)
    2-0 000       12 MHz MCLK ignored because ADC is slave         
    */ 
   err |= TLV_WR(0x13, 0x00); // 0000 0000 - 0x00 - SLAVE MODE

    /* 0x1F - PDMCLK_CFG Register (Pulse Density Modulation Clock)
    7   0           RESERVED; Write only reset value (0b)
    6-2 00100       RESERVED; Write only reset value (10000b) But PPC3 told me to write 00010b so... 
    1-2 00          PDMCLK is 2.8224 MHz or 3.072 MHz
    */
    err |= TLV_WR(0x1F, 0x08); // 0000 1000 - 0x08
    
    /* 0x3C - CH1_CFG0 Register - CHANNEL 1 CONFIG
    7   1           Line input type
    6-5 01          Analog single-ended input
    4   1           DC-coupled input
    3-2 00          2.5-kΩ input impedance
    1   0           RESERVED; Write only reset value (0b)
    0   0           DRE / AGC / DRC disabled
    */
    err |= TLV_WR(0x3C, 0xB0); // 1011 0000

    /* 0x41 - CH2_CFG0 Register - CHANNEL 2 CONFIG
    7   1           Line input type
    6-5 01          Analog single-ended input
    4   1           DC-coupled input
    3-2 00          2.5-kΩ input impedance
    1   0           RESERVED; Write only reset value (0b)
    0   0           DRE / AGC / DRC disabled
    */
    err |= TLV_WR(0x41, 0xB0); // 1010 0000

    /* 0x6B - DSP_CFG0 - "all-pass" (HPF OFF), linear-phase
    7   0           Digital volume control changes supported while ADC is powered-on
    6   0           Standard DRE/AGC/DRC algorithms (IGNORED; DISABLED FOR BOTH CHANNELS)
    5-4 00          Linear phase decimation filter
    3-2 00          Channel summation mode is disabled
    1-0 00          Set as the all-pass filter (with default coefficient values in P4_R72 to P4_R83) 
                    See Page 4 writes below
    */
    err |= TLV_WR(0x6B, 0x00); // 0000 0000 - 0x00 all-pass

    /* 0x74 - ASI_OUT_CH_EN Register - 
    7   1           Channel 1 output slot is enabled
    6   1           Channel 2 output slot is enabled
    5   1           Channel 3 output slot is a tri-state condition
    4   1           Channel 4 output slot is a tri-state condition
    3-0 0000        RESERVED; Write only reset value (0000b)
    */
    err |= TLV_WR(0x74, 0xC0); // 1100 0000

    /* 0x75 - PWR_CFG Register - 
    7   0           Power down MICBIAS
    6   1           Power up all enabled ADC and PDM channels
    5   1           Power up the PLL
    4   1           Dynamic channel power-up, power-down enabled; can be powered up or down individually, even if channel recording is on
    3-2 00          Channel 1 & 2 dynamic channel power-up, power-down feature enabled
    1   0           RESERVED; Write only reset value (0b)
    0   0           VAD Voice activity detection disabled
    */
    err |= TLV_WR(0x75, 0x70); // 0111 0000 YUP
/******** */



    /* 0x00 - PAGE_CFG Register SELECT PAGE 1 ***************************************************
    7-0 00000001    Set device page to 1  
    */ 
    err |= TLV_WR(0x00, 0x01); // 0000 0001 - 0x01

    /* 0x1E - VAD_CFG1 Register
    7-6 00          User initiated ADC power-up and ADC power-down
    5-4 00          Channel 1 is monitored for VAD activity (IGNORED: VAD NOT USED)
    3-2 00          VAD processing using internal oscillator clock (IGNORED: VAD NOT USED)
    1-0 00          External clock is 3.072 MHz (IGNORED: VAD NOT USED)
    */
    err |= TLV_WR(0x1E, 0x00); // 0000 0000

    /* 0x1F - VAD_CFG2 Register
    7   0           RESERVED; Write only reset value (0b)
    6   0           SDOUT pin is not enabled for interrupt function
    5   0           RESERVED; Write only reset value (0b)
    4   0           RESERVED; Write only reset value (0b)
    3   0           VAD processing is not enabled during ADC recording
    2-0 000         RESERVED; Write only reset value (000b)
    */
    err |= TLV_WR(0x1F, 0x00); // 0000 0000


    /* 0x00 - PAGE_CFG Register SELECT PAGE 4 ***************************************************
    7-0 00000001    Set device page to 4  
    */ 
    err |= TLV_WR(0x00, 0x04); // 0000 0100 - 0x04
    
    /* Programmable first-order IIR coefficients ************************************************
    8.6.4 Programmable Coefficient Registers
    8.6.4.3 Programmable Coefficient Registers: Page 4 
    Reset Programmable first-order IIR coefficients to Defaults 
    */
    err |= TLV_WR(0x01, 0x01); 
    /* N0 coefficient */
    // IIR_N0_BYT1 Register - N0 coefficient byte[31:24]     - P4_R72 - 0x48 = 0x7F
    // IIR_N0_BYT2 Register - N0 coefficient byte[23:16]     - P4_R73 - 0x49 = 0xFF
    // IIR_N0_BYT3 Register - N0 coefficient byte[15:8]      - P4_R74 - 0x4A = 0xFF
    // IIR_N0_BYT4 Register - N0 coefficient byte[7:0]       - P4_R75 - 0x4B = 0xFF
    
    /* N1 coefficient */
    // IIR_N1_BYT1 Register - N1 coefficient byte[31:24]     - P4_R76 - 0x4C = 0x00
    // IIR_N1_BYT2 Register - N1 coefficient byte[23:16]     - P4_R77 - 0x4D = 0x00
    // IIR_N1_BYT3 Register - N1 coefficient byte[15:8]      - P4_R78 - 0x4E = 0x00
    // IIR_N1_BYT4 Register - N1 coefficient byte[7:0]       - P4_R79 - 0x4F = 0x00
    
    /* D1 coefficient */
    // IIR_D1_BYT1 Register - D1 coefficient byte[31:24]     - P4_R80 - 0x50 = 0x00
    // IIR_D1_BYT2 Register - D1 coefficient byte[23:16]     - P4_R81 - 0x51 = 0x00
    // IIR_D1_BYT3 Register - D1 coefficient byte[15:8]      - P4_R82 - 0x52 = 0x00
    // IIR_D1_BYT4 Register - D1 coefficient byte[7:0]       - P4_R83 - 0x53 = 0x00

    if (err == ESP_OK) {
        LOG_INFO(TAG, "TLV320ADC5120 init script applied (addr 0x4E).");
        LOG_INFO(TAG, "Expect I²S master with 32-bit slots; verify FS≈8kHz, BCLK≈FS*2*32.");
        tlv_dump_regs();
    } else {
        LOG_ERR(TAG, ESP_FAIL, "TLV320ADC5120 init script failed");
    }
    return err;
}

// ---------------- I2S RX setup (ESP32-S3) ----------------
// Use the new I2S STD driver (IDF v5.x). ESP is master generating BCLK/FSYNC.
// Slot width 32 allows simple 32-bit packing; word length is 24 from ADC. [2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2s.html)[6](https://github.com/espressif/esp-idf/blob/master/components/esp_driver_i2s/include/driver/i2s_std.h)
static esp_err_t i2s_setup(void) {
    esp_err_t err = ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(s_cfg.i2s_port, I2S_ROLE_MASTER);

    err = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan); // RX only
    if (err != ESP_OK) return err;

    // Build Philips I2S 24-bit stereo slot config for ESP32‑S3
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_24BIT,
        I2S_SLOT_MODE_STEREO
    );
    // Use 32-bit slots for easy alignment on the ESP side; make WS width match the slot width.
    slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    slot_cfg.ws_width       = 24;
    slot_cfg.bit_shift = true;   // Philips I²S one-bit delay

    // Standard clock config
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(8000);
    // NOTE in IDF: set mclk_multiple=384 when using 24-bit data for accurate rates if needed
    clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256; // leave default unless you see small ppm drift

    i2s_std_config_t std_cfg = {
        .clk_cfg  = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,     //  MCLK unused; ESP will output BCLK/WS as master
            .bclk = 36,                 
            .ws   = 38,
            .dout = I2S_GPIO_UNUSED,
            .din  = 35,
            .invert_flags = { 
                .mclk_inv=false, 
                .bclk_inv=false,     
                .ws_inv=false,       
            }
        },
    };

    err = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (err) {
        LOG_ERR(TAG, err, "I2S initialization failed: ");
        return err;
    }

    LOG_INFO(TAG, "I2S RX configured (MASTER @ %lu Hz, 24/32-bit)", s_cfg.sample_rate_hz);
    return ESP_OK;
}


// ---------------- DMA reader task ----------------
static TaskHandle_t s_reader = NULL;
static void reader_task(void *arg) {
    LOG_INFO(TAG, "reader_task started");
    size_t bytes_read = 0;
    uint32_t ok = 0; /* TODO: Remove after debug */
    while (1) {
        // Read exactly one DMA frame (512 bytes) every time we're called
        esp_err_t r = i2s_channel_read(s_rx_chan, s_ring.dma_buf[s_ring.wr_idx], 
            TLV_DMA_BUF_SZ, &bytes_read, pdMS_TO_TICKS(200));

        if (r == ESP_OK && bytes_read == TLV_DMA_BUF_SZ) {
            s_ring.wr_idx = (s_ring.wr_idx + 1) % TLV_DMA_BUF_COUNT;
            
            if ((++ok % 100) == 0) { /* TODO: Remove after debug */
                LOG_INFO(TAG, "reader: %lu blocks read", ok);
            }
            taskYIELD();
        } else {
            LOG_WARN(TAG, r, "i2s read err=%d bytes=%u", r, (unsigned)bytes_read);
            taskYIELD();
        }
    }
}

esp_err_t tlv320adc5120_init(const tlv320adc5120_bus_cfg_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;
    memset(&s_ring, 0, sizeof(s_ring));

    LOG_INFO(TAG, "ring config: COUNT=%d SZ=%d", /* TODO: Remove after debug */
        TLV_DMA_BUF_COUNT, TLV_DMA_BUF_SZ);
    LOG_INFO(TAG, "ring idx init: rd=%d wr=%d",  /* TODO: Remove after debug */
        (int)s_ring.rd_idx, (int)s_ring.wr_idx);

    // I2C master config (pins are configurable; ESP32-S3 routes via GPIO matrix) [7](https://embeddedexplorer.com/esp32-i2c-tutorial/)[3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html)
    i2c_config_t i2c = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = s_cfg.gpio_sda,
        .scl_io_num = s_cfg.gpio_scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = s_cfg.i2c_freq_hz,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(s_cfg.i2c_port, &i2c), TAG, "i2c_param_config");
    ESP_RETURN_ON_ERROR(i2c_driver_install(s_cfg.i2c_port, I2C_MODE_MASTER, 0, 0, 0), TAG, "i2c_driver_install");

    ESP_RETURN_ON_ERROR(tlv_reset_device_pg0(), TAG, "ADC page 0 reset");
    ESP_RETURN_ON_ERROR(tlv_cfg_device(), TAG, "ADC cfg");

    ESP_RETURN_ON_ERROR(i2s_setup(), TAG, "i2s setup");

    LOG_INFO(TAG, "driver init OK");
    return ESP_OK;
}

esp_err_t tlv320adc5120_start(void) {
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_chan), TAG, "i2s_channel_enable");
    
    LOG_INFO(TAG, "I2S RX enabled; waiting for DMA...");

    // if (!s_reader) xTaskCreate(reader_task, "tlv_reader", 4096, NULL, 5, &s_reader);
    xTaskCreatePinnedToCore(reader_task, "tlv_reader", 4096, NULL, 1, &s_reader, /*core*/ 1);
    
    LOG_INFO(TAG, "driver start OK");
    return ESP_OK;
}

esp_err_t tlv320adc5120_stop(void) {
    if (s_reader) { vTaskDelete(s_reader); s_reader = NULL; }
    if (s_rx_chan) ESP_RETURN_ON_ERROR(i2s_channel_disable(s_rx_chan), TAG, "i2s_channel_disable");
    LOG_INFO(TAG, "driver stopped");
    return ESP_OK;
}

esp_err_t tlv320adc5120_deinit(void) {
    if (s_rx_chan) { i2s_del_channel(s_rx_chan); s_rx_chan = NULL; }
    i2c_driver_delete(s_cfg.i2c_port);
    LOG_INFO(TAG, "driver deinit OK");
    return ESP_OK;
}

// Pop one 512B buffer if available
bool tlv320adc5120_pop(uint8_t out[TLV_DMA_BUF_SZ]) {
    if (s_ring.rd_idx == s_ring.wr_idx) {
        // static uint32_t empty_cnt = 0;
        // if ((++empty_cnt % 200) == 0) {
        //     LOG_INFO(TAG, "pop: empty (rd=%d wr=%d)", (int)s_ring.rd_idx, (int)s_ring.wr_idx);
        // }
        return false;
    }
    memcpy(out, s_ring.dma_buf[s_ring.rd_idx], TLV_DMA_BUF_SZ);
    s_ring.rd_idx = (s_ring.rd_idx + 1) % TLV_DMA_BUF_COUNT;

    static uint32_t got_cnt = 0;
    if ((++got_cnt % 100) == 0) {
        LOG_INFO(TAG, "pop: got (rd=%d wr=%d)", (int)s_ring.rd_idx, (int)s_ring.wr_idx);
    }
    return true;
}


esp_err_t tlv320adc5120_read_id(uint8_t *out_id) {
    // TODO: read an ID/version register defined by TI
    uint8_t id = 0;
    // ESP_RETURN_ON_ERROR(i2c_read_reg(TLV_REG_DEVICE_ID, &id, 1), TAG, "read id");
    if (out_id) *out_id = id;
    return ESP_OK;
}

