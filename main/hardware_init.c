/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Hardware initialization for ESP32-S3-Touch-LCD-4.3b-Box
 * 480x480 IPS RGB LCD with GT911 Capacitive Touchscreen
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "sdkconfig.h"  // for CONFIG_EXAMPLE_I2C_MASTER_* macros

static const char *TAG = "hw_init";

/* LCD Pin Definitions for ESP32-S3-Touch-LCD-4.3b-Box */
#define LCD_PIN_D0          (8)
#define LCD_PIN_D1          (3)
#define LCD_PIN_D2          (46)
#define LCD_PIN_D3          (9)
#define LCD_PIN_D4          (1)
#define LCD_PIN_D5          (5)
#define LCD_PIN_D6          (6)
#define LCD_PIN_D7          (7)
#define LCD_PIN_D8          (15)
#define LCD_PIN_D9          (16)
#define LCD_PIN_D10         (4)
#define LCD_PIN_D11         (45)
#define LCD_PIN_D12         (48)
#define LCD_PIN_D13         (47)
#define LCD_PIN_D14         (21)
#define LCD_PIN_D15         (14)

#define LCD_PIN_HSYNC       (39)
#define LCD_PIN_VSYNC       (40)
#define LCD_PIN_DE          (41)
#define LCD_PIN_PCLK        (42)

#define LCD_PIN_RST         (2)
#define LCD_PIN_BACKLIGHT   (10)

/* Touch Panel Pin Definitions (I2C) */
/* allow board to be configured via sdkconfig like example 01_I2C_Test */
#ifndef TOUCH_I2C_SDA
#ifdef CONFIG_EXAMPLE_I2C_MASTER_SDA
#define TOUCH_I2C_SDA       CONFIG_EXAMPLE_I2C_MASTER_SDA
#else
#define TOUCH_I2C_SDA       (19) /* default from original board */
#endif
#endif
#ifndef TOUCH_I2C_SCL
#ifdef CONFIG_EXAMPLE_I2C_MASTER_SCL
#define TOUCH_I2C_SCL       CONFIG_EXAMPLE_I2C_MASTER_SCL
#else
#define TOUCH_I2C_SCL       (20) /* default from original board */
#endif
#endif
#define TOUCH_I2C_PORT      (I2C_NUM_0)
#define TOUCH_I2C_FREQ      (400000)
#define TOUCH_GT911_ADDR    (0x5D)
#define TOUCH_PIN_RST       (38)
#define TOUCH_PIN_INT       (37)

/* Display Configuration */
#define LCD_H_RES           (480)
#define LCD_V_RES           (480)
#define LCD_HSYNC_LEN       (10)
#define LCD_HBP             (8)
#define LCD_HFP             (8)
#define LCD_VSYNC_LEN       (10)
#define LCD_VBP             (8)
#define LCD_VFP             (8)
#define LCD_PCLK_HZ         (21000000)  // 21 MHz pixel clock

/**
 * @brief Initialize GPIO for backlight control
 */
static esp_err_t backlight_init(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LCD_PIN_BACKLIGHT),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(LCD_PIN_BACKLIGHT, 1);  // Turn on backlight
    ESP_LOGI(TAG, "Backlight initialized");
    return ESP_OK;
}

/**
 * @brief Initialize I2C for touch controller
 */
static esp_err_t i2c_touch_init(i2c_master_bus_handle_t *bus_handle)
{
    i2c_master_bus_config_t i2c_bus_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = TOUCH_I2C_PORT,
        .scl_io_num = TOUCH_I2C_SCL,
        .sda_io_num = TOUCH_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&i2c_bus_conf, bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized (SDA:%d, SCL:%d)", TOUCH_I2C_SDA, TOUCH_I2C_SCL);
    return ESP_OK;
}

/**
 * @brief Initialize RGB LCD Panel
 */
static esp_err_t lcd_init(esp_lcd_panel_handle_t *lcd_handle)
{
    ESP_LOGI(TAG, "Initializing RGB LCD Panel (480x480)");

    /* LCD IO configuration */
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,  // 16-bit RGB565
        .psram_trans_align = 64,
        .hsync_gpio_num = LCD_PIN_HSYNC,
        .vsync_gpio_num = LCD_PIN_VSYNC,
        .de_gpio_num = LCD_PIN_DE,
        .pclk_gpio_num = LCD_PIN_PCLK,
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
                .hsync_idle_low = false,
                .vsync_idle_low = false,
                .de_idle_high = true,
                .pclk_active_neg = false,
            },
        },
        .flags = {
            .fb_in_psram = true,
            .double_fb = true,
            .no_fb = false,
        },
    };

    esp_err_t ret = esp_lcd_new_rgb_panel(&panel_config, lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RGB panel: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Reset panel before initialization sequence */
    ret = esp_lcd_panel_reset(*lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset RGB panel: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_init(*lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RGB panel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "RGB LCD Panel initialized (480x480, 16-bit)");
    return ESP_OK;
}

/**
 * @brief Initialize GT911 Touch Controller
 */
static esp_err_t touch_gt911_init(i2c_master_bus_handle_t i2c_bus, 
                                   esp_lcd_touch_handle_t *tp_handle)
{
    ESP_LOGI(TAG, "Initializing GT911 Touch Controller");

    /* Configure touch reset and interrupt pins */
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TOUCH_PIN_RST),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* Reset touch controller */
    gpio_set_level(TOUCH_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TOUCH_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Create I2C device for touch (GT911 uses I2C) */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = TOUCH_GT911_ADDR,
        .control_phase_bytes = 1,
        /* user_ctx and on_color_trans_done not needed for touch */
    };
    
    esp_err_t ret = esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C panel IO: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure GT911 touch device */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = TOUCH_PIN_RST,
        .int_gpio_num = TOUCH_PIN_INT,
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

    ret = esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, tp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GT911 touch controller: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "GT911 Touch Controller initialized");
    return ESP_OK;
}

/**
 * @brief Initialize LCD and Touch hardware for ESP32-S3-Touch-LCD-4.3b-Box
 */
esp_err_t hardware_init(esp_lcd_panel_handle_t *lcd_handle, 
                        esp_lcd_touch_handle_t *tp_handle)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "=== Hardware Initialization ===");
    ESP_LOGI(TAG, "Board: ESP32-S3-Touch-LCD-4.3b-Box");
    ESP_LOGI(TAG, "Display: 480x480 IPS RGB LCD");
    ESP_LOGI(TAG, "Touch: GT911 Capacitive");

    /* Initialize backlight */
    ret = backlight_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Backlight initialization failed");
        return ret;
    }

    /* Initialize I2C for touch */
    i2c_master_bus_handle_t i2c_bus = NULL;
    ret = i2c_touch_init(&i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed");
        return ret;
    }

    /* Initialize LCD */
    ret = lcd_init(lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD initialization failed");
        return ret;
    }

    /* Initialize touch controller */
    ret = touch_gt911_init(i2c_bus, tp_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch controller initialization failed");
        return ret;
    }

    ESP_LOGI(TAG, "=== Hardware Initialization Complete ===");
    return ESP_OK;
}
