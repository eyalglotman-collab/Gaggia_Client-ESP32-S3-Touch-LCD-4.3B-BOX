/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware initialization for ESP32-S3-Touch-LCD-4.3B
 * 800x480 IPS RGB LCD with GT911 Capacitive Touchscreen
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include <string.h>
#include "hardware_init.h"
#include "lvgl_port.h"

static const char *TAG = "hw_init";
static bool s_backlight_enabled = true;

/* Shared I2C bus used by GT911 + CH422G (from Waveshare demos). */
#define BOARD_I2C_SDA           (8)
#define BOARD_I2C_SCL           (9)
#define BOARD_I2C_PORT          (I2C_NUM_0)
#define BOARD_I2C_FREQ_HZ       (400000)

/* CH422G pseudo-addressed function registers (Waveshare mapping). */
#define CH422G_ADDR_MODE        (0x24)
#define CH422G_ADDR_IO_OUT      (0x38)
#define CH422G_MODE_IO_OE       (0x01)

/* RGB LCD pin mapping from Waveshare 4.3B examples. */
#define LCD_PIN_HSYNC           (46)
#define LCD_PIN_VSYNC           (3)
#define LCD_PIN_DE              (5)
#define LCD_PIN_PCLK            (7)

#define LCD_PIN_D0              (14)
#define LCD_PIN_D1              (38)
#define LCD_PIN_D2              (18)
#define LCD_PIN_D3              (17)
#define LCD_PIN_D4              (10)
#define LCD_PIN_D5              (39)
#define LCD_PIN_D6              (0)
#define LCD_PIN_D7              (45)
#define LCD_PIN_D8              (48)
#define LCD_PIN_D9              (47)
#define LCD_PIN_D10             (21)
#define LCD_PIN_D11             (1)
#define LCD_PIN_D12             (2)
#define LCD_PIN_D13             (42)
#define LCD_PIN_D14             (41)
#define LCD_PIN_D15             (40)

/* Touch reset helper pin used in Waveshare demo sequence. */
#define TOUCH_RESET_GPIO        (4)

/* Display timing from Waveshare RGB example (800x480). */
#define LCD_H_RES               (800)
#define LCD_V_RES               (480)
#define LCD_HSYNC_LEN           (4)
#define LCD_HBP                 (8)
#define LCD_HFP                 (8)
#define LCD_VSYNC_LEN           (4)
#define LCD_VBP                 (8)
#define LCD_VFP                 (8)
#define LCD_PCLK_HZ             (16000000)

static i2c_master_bus_handle_t s_i2c_bus = NULL;

/**
 * @brief Add temporary I2C device handle and execute callback transaction.
 *
 * @details ESP-IDF bus API requires a device handle per address. This helper
 * creates the handle for a transaction and removes it afterward.
 */
static esp_err_t i2c_with_device(uint8_t addr,
                                 esp_err_t (*op)(i2c_master_dev_handle_t dev, void *ctx),
                                 void *ctx)
{
    ESP_RETURN_ON_FALSE(s_i2c_bus != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev = NULL;
    esp_err_t ret = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &dev);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = op(dev, ctx);
    i2c_master_bus_rm_device(dev);
    return ret;
}

typedef struct {
    const uint8_t *tx;
    size_t tx_len;
} i2c_tx_ctx_t;

/**
 * @brief Execute raw I2C write transaction.
 */
static esp_err_t i2c_tx_op(i2c_master_dev_handle_t dev, void *ctx)
{
    const i2c_tx_ctx_t *tx_ctx = (const i2c_tx_ctx_t *)ctx;
    return i2c_master_transmit(dev, tx_ctx->tx, tx_ctx->tx_len, -1);
}

typedef struct {
    uint8_t *rx;
    size_t rx_len;
} i2c_rx_ctx_t;

/**
 * @brief Execute raw I2C read transaction.
 */
static esp_err_t i2c_rx_op(i2c_master_dev_handle_t dev, void *ctx)
{
    const i2c_rx_ctx_t *rx_ctx = (const i2c_rx_ctx_t *)ctx;
    return i2c_master_receive(dev, rx_ctx->rx, rx_ctx->rx_len, -1);
}

typedef struct {
    const uint8_t *tx;
    size_t tx_len;
    uint8_t *rx;
    size_t rx_len;
} i2c_txrx_ctx_t;

/**
 * @brief Execute combined I2C write-then-read transaction.
 */
static esp_err_t i2c_txrx_op(i2c_master_dev_handle_t dev, void *ctx)
{
    const i2c_txrx_ctx_t *txrx_ctx = (const i2c_txrx_ctx_t *)ctx;
    return i2c_master_transmit_receive(dev,
                                       txrx_ctx->tx,
                                       txrx_ctx->tx_len,
                                       txrx_ctx->rx,
                                       txrx_ctx->rx_len,
                                       -1);
}

/**
 * @brief Initialize board I2C bus used by touch and expander devices.
 */
static esp_err_t board_i2c_init(void)
{
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BOARD_I2C_PORT,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_io_num = BOARD_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create board I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Board I2C initialized (SDA:%d, SCL:%d)", BOARD_I2C_SDA, BOARD_I2C_SCL);
    return ESP_OK;
}

/**
 * @brief Set CH422G IO output byte.
 *
 * @details Configures CH422G IO pins as outputs and writes desired IO mask.
 */
static esp_err_t ch422g_set_io(uint8_t io_mask)
{
    uint8_t mode = CH422G_MODE_IO_OE;
    esp_err_t ret = hardware_i2c_write_raw(CH422G_ADDR_MODE, &mode, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    return hardware_i2c_write_raw(CH422G_ADDR_IO_OUT, &io_mask, 1);
}

/**
 * @brief Configure and run Waveshare touch reset sequence.
 *
 * @details Uses CH422G and GPIO4 toggling sequence from vendor demo to place
 * GT911 into a known state before touch driver initialization.
 */
static esp_err_t touch_reset_sequence(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TOUCH_RESET_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Failed to config touch reset GPIO");

    /* Keep LCD reset, touch reset, and backlight lines high by default. */
    ESP_RETURN_ON_ERROR(ch422g_set_io(0x1E), TAG, "Failed to prime CH422G outputs");
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Vendor-defined reset pattern: 0x2C -> GPIO4 low -> 0x2E. */
    ESP_RETURN_ON_ERROR(ch422g_set_io(0x2C), TAG, "Failed touch reset stage 1");
    esp_rom_delay_us(100 * 1000);
    gpio_set_level(TOUCH_RESET_GPIO, 0);
    esp_rom_delay_us(100 * 1000);
    ESP_RETURN_ON_ERROR(ch422g_set_io(0x2E), TAG, "Failed touch reset stage 2");
    esp_rom_delay_us(200 * 1000);

    return ESP_OK;
}

/**
 * @brief Initialize RGB LCD panel using Waveshare 4.3B mapping/timings.
 */
static esp_err_t lcd_init(esp_lcd_panel_handle_t *lcd_handle)
{
    /* Force a deterministic LCD reset pulse through CH422G before panel init. */
    ESP_RETURN_ON_ERROR(ch422g_set_io(0x12), TAG, "Failed LCD reset low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(ch422g_set_io(0x1A), TAG, "Failed LCD reset high");
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = LVGL_PORT_LCD_RGB_BUFFER_NUMS,
        /* A small bounce buffer removes idle redraw flicker on the 4.3B panel. */
        .bounce_buffer_size_px = LCD_H_RES * HARDWARE_LCD_RGB_BOUNCE_BUFFER_HEIGHT,
        .sram_trans_align = 4,
        /* 64-byte PSRAM alignment matches the vendor example and the stable path. */
        .psram_trans_align = 64,
        .dma_burst_size = 64,
        .hsync_gpio_num = LCD_PIN_HSYNC,
        .vsync_gpio_num = LCD_PIN_VSYNC,
        .de_gpio_num = LCD_PIN_DE,
        .pclk_gpio_num = LCD_PIN_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
            LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
            LCD_PIN_D8, LCD_PIN_D9, LCD_PIN_D10, LCD_PIN_D11,
            LCD_PIN_D12, LCD_PIN_D13, LCD_PIN_D14, LCD_PIN_D15,
        },
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = LCD_HSYNC_LEN,
            .hsync_back_porch = LCD_HBP,
            .hsync_front_porch = LCD_HFP,
            .vsync_pulse_width = LCD_VSYNC_LEN,
            .vsync_back_porch = LCD_VBP,
            .vsync_front_porch = LCD_VFP,
            .flags = {
                .pclk_active_neg = true,
            },
        },
        .flags = {
            .fb_in_psram = true,
        },
    };

    esp_err_t ret = esp_lcd_new_rgb_panel(&panel_config, lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RGB panel: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_init(*lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RGB panel: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Ensure backlight line is enabled after panel init. */
    ret = ch422g_set_io(0x1E);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Backlight enable via CH422G failed: %s", esp_err_to_name(ret));
    } else {
        s_backlight_enabled = true;
    }

    /* Pre-fill RGB framebuffer with white to validate hardware path before LVGL. */
    void *fb = NULL;
    ret = esp_lcd_rgb_panel_get_frame_buffer(*lcd_handle, 1, &fb);
    if (ret == ESP_OK && fb) {
        memset(fb, 0xFF, LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
        esp_lcd_panel_draw_bitmap(*lcd_handle, 0, 0, LCD_H_RES, LCD_V_RES, fb);
        ESP_LOGI(TAG, "LCD framebuffer prefill applied (white test frame)");
    } else {
        ESP_LOGW(TAG, "Unable to prefill LCD framebuffer: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "RGB LCD panel initialized (800x480)");
    return ESP_OK;
}

/**
 * @brief Initialize GT911 touch controller on shared board I2C bus.
 */
static esp_err_t touch_gt911_init(esp_lcd_touch_handle_t *tp_handle)
{
    esp_err_t ret = ESP_FAIL;
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };

    /* Try the primary GT911 address, then fallback address if needed. */
    const uint8_t gt911_addrs[] = {0x5D, 0x14};
    for (size_t i = 0; i < sizeof(gt911_addrs); i++) {
        esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        io_config.dev_addr = gt911_addrs[i];
        io_config.scl_speed_hz = BOARD_I2C_FREQ_HZ;

        esp_lcd_panel_io_handle_t io_handle = NULL;
        ret = esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_config, &io_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "GT911 IO create failed at 0x%02X: %s", gt911_addrs[i], esp_err_to_name(ret));
            continue;
        }

        ret = esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, tp_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "GT911 touch initialized at I2C address 0x%02X", gt911_addrs[i]);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "GT911 init failed at 0x%02X: %s", gt911_addrs[i], esp_err_to_name(ret));
        esp_lcd_panel_io_del(io_handle);
    }

    ESP_LOGE(TAG, "Failed to init GT911 touch on supported addresses");
    return ret;
}

/**
 * @brief Initialize full display and touch hardware stack for target board.
 *
 * @details Orchestrates shared I2C bring-up, CH422G/touch reset sequence,
 * RGB panel setup, and GT911 initialization. If GT911 setup fails, the
 * function continues in display-only mode and returns ESP_OK with `tp_handle`
 * set to NULL.
 */
esp_err_t hardware_init(esp_lcd_panel_handle_t *lcd_handle,
                        esp_lcd_touch_handle_t *tp_handle)
{
    ESP_LOGI(TAG, "=== Hardware Initialization ===");
    ESP_LOGI(TAG, "Board: ESP32-S3-Touch-LCD-4.3B");
    ESP_LOGI(TAG, "Display: 800x480 IPS RGB LCD");
    ESP_LOGI(TAG, "Touch: GT911 Capacitive");

    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "Board I2C init failed");
    ESP_RETURN_ON_ERROR(touch_reset_sequence(), TAG, "Touch reset sequence failed");
    ESP_RETURN_ON_ERROR(lcd_init(lcd_handle), TAG, "LCD init failed");
    if (tp_handle) {
        esp_err_t tp_ret = touch_gt911_init(tp_handle);
        if (tp_ret != ESP_OK) {
            *tp_handle = NULL;
            ESP_LOGW(TAG, "Touch init failed, continuing display-only mode");
        }
    }

    ESP_LOGI(TAG, "=== Hardware Initialization Complete ===");
    return ESP_OK;
}

esp_err_t hardware_i2c_probe(uint8_t addr)
{
    ESP_RETURN_ON_FALSE(s_i2c_bus != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");
    return i2c_master_probe(s_i2c_bus, addr, -1);
}

esp_err_t hardware_i2c_write_raw(uint8_t addr, const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(data != NULL && len > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid write args");
    i2c_tx_ctx_t ctx = {
        .tx = data,
        .tx_len = len,
    };
    return i2c_with_device(addr, i2c_tx_op, &ctx);
}

esp_err_t hardware_i2c_read_raw(uint8_t addr, uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(data != NULL && len > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid read args");
    i2c_rx_ctx_t ctx = {
        .rx = data,
        .rx_len = len,
    };
    return i2c_with_device(addr, i2c_rx_op, &ctx);
}

esp_err_t hardware_i2c_write_read(uint8_t addr,
                                  const uint8_t *wdata,
                                  size_t wlen,
                                  uint8_t *rdata,
                                  size_t rlen)
{
    ESP_RETURN_ON_FALSE(wdata != NULL && wlen > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid write/read write args");
    ESP_RETURN_ON_FALSE(rdata != NULL && rlen > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid write/read read args");

    i2c_txrx_ctx_t ctx = {
        .tx = wdata,
        .tx_len = wlen,
        .rx = rdata,
        .rx_len = rlen,
    };
    return i2c_with_device(addr, i2c_txrx_op, &ctx);
}

/**
 * @brief Set the LCD backlight state through CH422G.
 *
 * @details Uses the known-good board masks: `0x1E` keeps the backlight on and
 * `0x1A` disables only the backlight while preserving LCD reset, touch reset,
 * and SD chip-select high.
 *
 * @param[in] enabled `true` to enable the backlight.
 *
 * @return
 *      - ESP_OK: CH422G accepted the command
 *      - ESP_ERR_*: CH422G write failed
 */
esp_err_t hardware_set_backlight_enabled(bool enabled)
{
    const uint8_t mask = enabled ? 0x1E : 0x1A;
    esp_err_t ret = ch422g_set_io(mask);
    if (ret == ESP_OK) {
        s_backlight_enabled = enabled;
    }
    return ret;
}

/**
 * @brief Return the software-tracked backlight state.
 *
 * @details Exposes the most recent requested backlight state so UI and touch
 * logic can avoid redundant writes and wake the screen on touch.
 *
 * @return `true` when the backlight is currently requested on.
 */
bool hardware_get_backlight_enabled(void)
{
    return s_backlight_enabled;
}
