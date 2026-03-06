/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "peripherals_manager.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include "driver/twai.h"
#pragma GCC diagnostic pop
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"

#include "hardware_init.h"

static const char *TAG = "peripherals";

/* CH422G pseudo-addressed registers from Waveshare demo sources. */
#define CH422G_ADDR_MODE            (0x24)
#define CH422G_ADDR_IO_OUT          (0x38)
#define CH422G_ADDR_OD_OUT          (0x23)
#define CH422G_ADDR_IO_IN           (0x26)
#define CH422G_MODE_IO_OE           (0x01)

/* CH422G bit mapping used on the board. */
#define CH422G_IO_TOUCH_RST         (0x02)
#define CH422G_IO_BACKLIGHT         (0x04)
#define CH422G_IO_LCD_RST           (0x08)
#define CH422G_IO_SD_CS             (0x10)
#define CH422G_IO_DI1               (0x20)
#define CH422G_IO_DI0               (0x01)

/* RS485 wiring from demo defaults. */
#define RS485_UART_PORT             (UART_NUM_2)
#define RS485_UART_BAUD             (115200)
#define RS485_TXD                   (44)
#define RS485_RXD                   (43)
#define RS485_BUF_SIZE              (1024)

/* SD card SPI wiring from demo defaults. */
#define SD_MOSI                     (11)
#define SD_MISO                     (13)
#define SD_SCLK                     (12)
#define SD_CS_GPIO                  (-1)
#define SD_MOUNT_POINT              "/sdcard"

/* RTC (PCF85063A) constants from demo. */
#define RTC_ADDR                    (0x51)
#define RTC_REG_CTRL1               (0x00)
#define RTC_REG_SECONDS             (0x04)
#define RTC_CTRL1_CAP_SEL           (0x01)

/* TWAI wiring from demo defaults. */
#define TWAI_TX_GPIO                (15)
#define TWAI_RX_GPIO                (16)
#define TWAI_TX_PERIOD_MS           (1000)

static bool s_rs485_ready = false;
static bool s_twai_ready = false;
static bool s_sd_ready = false;
static sdmmc_card_t *s_sd_card = NULL;
static TaskHandle_t s_peripherals_task = NULL;

/**
 * @brief Convert decimal integer to BCD byte.
 */
static uint8_t dec_to_bcd(int val)
{
    return (uint8_t)((val / 10 * 16) + (val % 10));
}

/**
 * @brief Convert BCD byte to decimal integer.
 */
static int bcd_to_dec(uint8_t val)
{
    return (int)((val / 16 * 10) + (val % 16));
}

/**
 * @brief Configure CH422G for IO output mode.
 */
static esp_err_t ch422g_enable_io_output(void)
{
    uint8_t mode = CH422G_MODE_IO_OE;
    return hardware_i2c_write_raw(CH422G_ADDR_MODE, &mode, 1);
}

/**
 * @brief Write CH422G IO output bitmask.
 */
static esp_err_t ch422g_write_io(uint8_t mask)
{
    ESP_RETURN_ON_ERROR(ch422g_enable_io_output(), TAG, "Failed to set CH422G mode");
    return hardware_i2c_write_raw(CH422G_ADDR_IO_OUT, &mask, 1);
}

/**
 * @brief Read CH422G digital input state byte.
 */
static esp_err_t ch422g_read_io(uint8_t *value)
{
    return hardware_i2c_read_raw(CH422G_ADDR_IO_IN, value, 1);
}

/**
 * @brief Write CH422G OD output byte.
 */
static esp_err_t ch422g_write_od(uint8_t mask)
{
    return hardware_i2c_write_raw(CH422G_ADDR_OD_OUT, &mask, 1);
}

/**
 * @brief Run CH422G DI/DO loopback test from Waveshare IO example.
 *
 * @details Toggles OD outputs and checks DI0/DI1 states to verify the digital
 * path is active. This test is advisory and does not fail startup.
 */
static void io_self_test(void)
{
    uint8_t io_state = 0;
    bool pass1 = false;
    bool pass2 = false;

    if (ch422g_write_od(0x01) == ESP_OK &&
        ch422g_read_io(&io_state) == ESP_OK) {
        pass1 = ((io_state & CH422G_IO_DI0) == CH422G_IO_DI0) &&
                ((io_state & CH422G_IO_DI1) == 0);
    }

    vTaskDelay(pdMS_TO_TICKS(1));

    if (ch422g_write_od(0x02) == ESP_OK &&
        ch422g_read_io(&io_state) == ESP_OK) {
        pass2 = ((io_state & CH422G_IO_DI0) == 0) &&
                ((io_state & CH422G_IO_DI1) == CH422G_IO_DI1);
    }

    if (pass1 && pass2) {
        ESP_LOGI(TAG, "CH422G DI/DO self-test: PASS");
    } else {
        ESP_LOGW(TAG, "CH422G DI/DO self-test: WARN (wiring or loopback not present)");
    }
}

/**
 * @brief Scan I2C addresses and print detected devices.
 */
static void i2c_scan(void)
{
    ESP_LOGI(TAG, "I2C scan started");
    for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
        if (hardware_i2c_probe(addr) == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found at 0x%02X", addr);
        }
    }
}

/**
 * @brief Initialize and configure RTC (PCF85063A) baseline time.
 *
 * @details Mirrors Waveshare RTC example flow by writing CTRL1 CAP_SEL and
 * setting a known starting date/time.
 */
static void rtc_init_and_set(void)
{
    uint8_t ctrl1_data[2] = {
        RTC_REG_CTRL1,
        RTC_CTRL1_CAP_SEL,
    };

    if (hardware_i2c_write_raw(RTC_ADDR, ctrl1_data, sizeof(ctrl1_data)) != ESP_OK) {
        ESP_LOGW(TAG, "RTC control init failed");
        return;
    }

    /* Set a baseline date/time (2026-03-03 12:00:00, weekday=2 Tuesday). */
    uint8_t dt_data[8] = {
        RTC_REG_SECONDS,
        dec_to_bcd(0),
        dec_to_bcd(0),
        dec_to_bcd(12),
        dec_to_bcd(3),
        dec_to_bcd(2),
        dec_to_bcd(3),
        dec_to_bcd(2026 - 1970),
    };

    if (hardware_i2c_write_raw(RTC_ADDR, dt_data, sizeof(dt_data)) != ESP_OK) {
        ESP_LOGW(TAG, "RTC datetime set failed");
    }
}

/**
 * @brief Read and log RTC date/time values.
 */
static void rtc_log_now(void)
{
    uint8_t reg = RTC_REG_SECONDS;
    uint8_t raw[7] = {0};

    if (hardware_i2c_write_read(RTC_ADDR, &reg, 1, raw, sizeof(raw)) != ESP_OK) {
        ESP_LOGW(TAG, "RTC read failed");
        return;
    }

    int sec = bcd_to_dec(raw[0] & 0x7F);
    int min = bcd_to_dec(raw[1] & 0x7F);
    int hour = bcd_to_dec(raw[2] & 0x3F);
    int day = bcd_to_dec(raw[3] & 0x3F);
    int month = bcd_to_dec(raw[5] & 0x1F);
    int year = bcd_to_dec(raw[6]) + 1970;

    ESP_LOGI(TAG, "RTC now: %04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, min, sec);
}

/**
 * @brief Initialize RS485 UART link and TX/RX pins.
 */
static void rs485_init(void)
{
    uart_config_t cfg = {
        .baud_rate = RS485_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_driver_install(RS485_UART_PORT, RS485_BUF_SIZE * 2, 0, 0, NULL, 0) != ESP_OK ||
        uart_param_config(RS485_UART_PORT, &cfg) != ESP_OK ||
        uart_set_pin(RS485_UART_PORT, RS485_TXD, RS485_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGW(TAG, "RS485 init failed");
        s_rs485_ready = false;
        return;
    }

    s_rs485_ready = true;
    ESP_LOGI(TAG, "RS485 initialized (UART2 TX=%d RX=%d)", RS485_TXD, RS485_RXD);
}

/**
 * @brief Send periodic RS485 heartbeat payload.
 */
static void rs485_send_heartbeat(void)
{
    static const char *payload = "Eyal espresso RS485 heartbeat\r\n";
    if (!s_rs485_ready) {
        return;
    }
    uart_write_bytes(RS485_UART_PORT, payload, strlen(payload));
}

/**
 * @brief Initialize TWAI driver for periodic transmit.
 */
static void twai_init(void)
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_TX_GPIO, TWAI_RX_GPIO, TWAI_MODE_NO_ACK);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_50KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK ||
        twai_start() != ESP_OK) {
        ESP_LOGW(TAG, "TWAI init failed");
        s_twai_ready = false;
        return;
    }

    s_twai_ready = true;
    ESP_LOGI(TAG, "TWAI initialized (TX=%d RX=%d)", TWAI_TX_GPIO, TWAI_RX_GPIO);
}

/**
 * @brief Send periodic TWAI message frame.
 */
static void twai_send_heartbeat(void)
{
    twai_message_t message = {
        .identifier = 0x0F6,
        .data_length_code = 8,
    };

    if (!s_twai_ready) {
        return;
    }

    for (int i = 0; i < 8; i++) {
        message.data[i] = (uint8_t)i;
    }

    if (twai_transmit(&message, pdMS_TO_TICKS(100)) != ESP_OK) {
        ESP_LOGW(TAG, "TWAI heartbeat transmit failed");
    }
}

/**
 * @brief Write and read a quick SD card file for verification.
 */
static void sd_card_file_test(void)
{
    const char *path = SD_MOUNT_POINT "/peripheral_check.txt";
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "SD file write open failed");
        return;
    }

    fprintf(f, "Eyal espresso SD check\n");
    fclose(f);

    char line[64] = {0};
    f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "SD file read open failed");
        return;
    }

    fgets(line, sizeof(line), f);
    fclose(f);
    ESP_LOGI(TAG, "SD test read: %s", line);
}

/**
 * @brief Initialize SD over SDSPI using Waveshare demo pinout.
 *
 * @details Uses CH422G to drive board-level SD CS line behavior similarly to
 * vendor example where GPIO CS is not routed directly to ESP pin.
 */
static void sd_card_init(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI,
        .miso_io_num = SD_MISO,
        .sclk_io_num = SD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    if (spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA) != ESP_OK) {
        ESP_LOGW(TAG, "SD SPI bus init failed");
        s_sd_ready = false;
        return;
    }

    /* Keep touch/LCD control lines high, pull SD CS low via IO4 for mount phase. */
    if (ch422g_write_io(CH422G_IO_TOUCH_RST | CH422G_IO_LCD_RST) != ESP_OK) {
        ESP_LOGW(TAG, "CH422G SD CS prep failed");
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_GPIO;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_cfg, &s_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        spi_bus_free(host.slot);
        s_sd_ready = false;
        return;
    }

    /* Return IO4 high after mount attempt. */
    ch422g_write_io(CH422G_IO_TOUCH_RST | CH422G_IO_BACKLIGHT | CH422G_IO_LCD_RST | CH422G_IO_SD_CS);

    s_sd_ready = true;
    ESP_LOGI(TAG, "SD mounted at %s", SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_sd_card);
    sd_card_file_test();
}

/**
 * @brief Background service loop for periodic peripheral activity.
 *
 * @details Sends RS485/TWAI heartbeats and logs RTC time at fixed intervals so
 * all integrated peripherals actively operate after startup.
 */
static void peripherals_task(void *arg)
{
    (void)arg;

    int64_t last_rs485_ms = 0;
    int64_t last_twai_ms = 0;
    int64_t last_rtc_ms = 0;

    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;

        if ((now_ms - last_rs485_ms) >= 2000) {
            rs485_send_heartbeat();
            last_rs485_ms = now_ms;
        }

        if ((now_ms - last_twai_ms) >= TWAI_TX_PERIOD_MS) {
            twai_send_heartbeat();
            last_twai_ms = now_ms;
        }

        if ((now_ms - last_rtc_ms) >= 5000) {
            rtc_log_now();
            last_rtc_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Initialize and start all non-display board peripherals.
 *
 * @details Runs one-time setup derived from Waveshare demos and launches a
 * periodic service task that exercises integrated peripheral paths.
 */
esp_err_t peripherals_manager_start(void)
{
    i2c_scan();
    io_self_test();
    rtc_init_and_set();
    rs485_init();
    sd_card_init();
    twai_init();

    if (xTaskCreate(peripherals_task,
                    "peripherals_task",
                    6144,
                    NULL,
                    4,
                    &s_peripherals_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create peripherals task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "Peripherals manager started (RS485=%d TWAI=%d SD=%d)",
             s_rs485_ready,
             s_twai_ready,
             s_sd_ready);
    return ESP_OK;
}
