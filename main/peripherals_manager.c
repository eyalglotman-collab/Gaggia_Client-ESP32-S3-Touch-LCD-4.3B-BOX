/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "peripherals_manager.h"

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>
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
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/ip4_addr.h"

#include "hardware_init.h"
#include "system_constants.h"

static const char *TAG = "peripherals";

/*
 * Temporary probe switch for validating the physical RS485 path without
 * changing the normal request/parse flow. Set to 0 to revert to standard
 * logging only.
 */
#ifndef ESPRESSO_RS485_DIAGNOSTICS
#define ESPRESSO_RS485_DIAGNOSTICS 1
#endif

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
#define RS485_CONTROLLER_TIMEOUT_MS (750)

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
#define RTC_SECONDS_OS              (0x80)

/* TWAI wiring from demo defaults. */
#define TWAI_TX_GPIO                (15)
#define TWAI_RX_GPIO                (16)
#define TWAI_TX_PERIOD_MS           (1000)

static bool s_rs485_ready = false;
static bool s_twai_ready = false;
static bool s_sd_ready = false;
static bool s_rtc_ready = false;
static bool s_wifi_ready = false;
static uint16_t s_last_wifi_ap_count = 0;
static peripherals_controller_status_t s_last_controller_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;
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
 * @brief Parse the firmware build timestamp into calendar fields.
 *
 * @details Converts the compiler `__DATE__` and `__TIME__` macros into a
 * normalized `struct tm` so the RTC can be initialized to the build time.
 *
 * @param[out] out_tm Parsed timestamp structure.
 *
 * @return
 *      - ESP_OK: Build timestamp parsed successfully
 *      - ESP_ERR_INVALID_STATE: Build timestamp could not be parsed
 */
static esp_err_t parse_build_time(struct tm *out_tm)
{
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month_str[4] = {0};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (sscanf(__DATE__, "%3s %d %d", month_str, &day, &year) != 3) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *month_pos = strstr(months, month_str);
    if (month_pos == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(out_tm, 0, sizeof(*out_tm));
    out_tm->tm_year = year - 1900;
    out_tm->tm_mon = (int)((month_pos - months) / 3);
    out_tm->tm_mday = day;
    out_tm->tm_hour = hour;
    out_tm->tm_min = minute;
    out_tm->tm_sec = second;
    out_tm->tm_isdst = -1;

    if (mktime(out_tm) == (time_t)-1) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
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
 * @brief Read and log the current RTC date/time values.
 *
 * @details Fetches the PCF85063A calendar registers over I2C, converts the
 * returned BCD fields into decimal values, and prints the resulting timestamp.
 *
 * @return
 *      - ESP_OK: RTC values read and logged successfully
 *      - ESP_FAIL: RTC read transaction failed
 */
static esp_err_t rtc_log_now(void)
{
    struct tm rtc_tm = {0};
    esp_err_t ret = peripherals_manager_get_rtc_time(&rtc_tm);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RTC read failed");
        return ret;
    }

    ESP_LOGI(TAG,
             "RTC now: %04d-%02d-%02d %02d:%02d:%02d",
             rtc_tm.tm_year + 1900,
             rtc_tm.tm_mon + 1,
             rtc_tm.tm_mday,
             rtc_tm.tm_hour,
             rtc_tm.tm_min,
             rtc_tm.tm_sec);
    return ESP_OK;
}

/**
 * @brief Read raw RTC calendar registers and report clock validity.
 *
 * @details Reads the seven active calendar bytes starting at the seconds
 * register and exposes whether the oscillator stop flag indicates invalid
 * retained time.
 *
 * @param[out] raw Destination byte buffer for the 7 RTC calendar registers.
 * @param[out] time_valid Set to `true` when the oscillator stop flag is clear.
 *
 * @return
 *      - ESP_OK: Raw RTC registers read successfully
 *      - ESP_ERR_INVALID_ARG: Output pointers are NULL
 *      - ESP_ERR_*: Underlying I2C transaction failed
 */
static esp_err_t rtc_read_raw_time(uint8_t raw[7], bool *time_valid)
{
    ESP_RETURN_ON_FALSE(raw != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid RTC raw buffer");
    ESP_RETURN_ON_FALSE(time_valid != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid RTC validity buffer");

    uint8_t reg = RTC_REG_SECONDS;
    ESP_RETURN_ON_ERROR(hardware_i2c_write_read(RTC_ADDR, &reg, 1, raw, 7),
                        TAG,
                        "RTC read failed");

    *time_valid = ((raw[0] & RTC_SECONDS_OS) == 0);
    return ESP_OK;
}

/**
 * @brief Validate and write a calendar time into the RTC registers.
 *
 * @details Checks the provided calendar fields for a reasonable range, encodes
 * them into the PCF85063A register layout, and writes the seven active date
 * and time registers in one transaction.
 *
 * @param[in] new_tm New calendar time to write.
 *
 * @return
 *      - ESP_OK: RTC registers updated successfully
 *      - ESP_ERR_INVALID_ARG: `new_tm` is NULL or contains invalid fields
 *      - ESP_ERR_*: Underlying RTC write failed
 */
esp_err_t peripherals_manager_set_rtc_time(const struct tm *new_tm)
{
    ESP_RETURN_ON_FALSE(new_tm != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid RTC time input");
    ESP_RETURN_ON_FALSE(new_tm->tm_sec >= 0 && new_tm->tm_sec <= 59, ESP_ERR_INVALID_ARG, TAG, "Invalid seconds");
    ESP_RETURN_ON_FALSE(new_tm->tm_min >= 0 && new_tm->tm_min <= 59, ESP_ERR_INVALID_ARG, TAG, "Invalid minutes");
    ESP_RETURN_ON_FALSE(new_tm->tm_hour >= 0 && new_tm->tm_hour <= 23, ESP_ERR_INVALID_ARG, TAG, "Invalid hours");
    ESP_RETURN_ON_FALSE(new_tm->tm_mday >= 1 && new_tm->tm_mday <= 31, ESP_ERR_INVALID_ARG, TAG, "Invalid day");
    ESP_RETURN_ON_FALSE(new_tm->tm_mon >= 0 && new_tm->tm_mon <= 11, ESP_ERR_INVALID_ARG, TAG, "Invalid month");
    ESP_RETURN_ON_FALSE((new_tm->tm_year + 1900) >= 1970 && (new_tm->tm_year + 1900) <= 2069,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "Invalid year");

    struct tm normalized_tm = *new_tm;
    normalized_tm.tm_isdst = -1;
    time_t stamp = mktime(&normalized_tm);
    ESP_RETURN_ON_FALSE(stamp != (time_t)-1, ESP_ERR_INVALID_ARG, TAG, "Invalid RTC calendar");

    uint8_t dt_data[8] = {
        RTC_REG_SECONDS,
        dec_to_bcd(normalized_tm.tm_sec),
        dec_to_bcd(normalized_tm.tm_min),
        dec_to_bcd(normalized_tm.tm_hour),
        dec_to_bcd(normalized_tm.tm_mday),
        dec_to_bcd(normalized_tm.tm_wday),
        dec_to_bcd(normalized_tm.tm_mon + 1),
        dec_to_bcd((normalized_tm.tm_year + 1900) - 1970),
    };

    ESP_RETURN_ON_ERROR(hardware_i2c_write_raw(RTC_ADDR, dt_data, sizeof(dt_data)),
                        TAG,
                        "RTC datetime set failed");

    s_rtc_ready = true;
    ESP_LOGI(TAG,
             "RTC time updated to %04d-%02d-%02d %02d:%02d:%02d",
             normalized_tm.tm_year + 1900,
             normalized_tm.tm_mon + 1,
             normalized_tm.tm_mday,
             normalized_tm.tm_hour,
             normalized_tm.tm_min,
             normalized_tm.tm_sec);
    return ESP_OK;
}

/**
 * @brief Read the current RTC calendar registers.
 *
 * @details Retrieves the active PCF85063A date/time register set and converts
 * it into a standard `struct tm` for application-facing consumers.
 *
 * @param[out] out_tm Destination time structure.
 *
 * @return
 *      - ESP_OK: RTC time decoded successfully
 *      - ESP_ERR_INVALID_STATE: RTC has not been initialized
 *      - ESP_ERR_INVALID_ARG: `out_tm` is NULL
 *      - ESP_ERR_*: I2C read failed
 */
esp_err_t peripherals_manager_get_rtc_time(struct tm *out_tm)
{
    ESP_RETURN_ON_FALSE(out_tm != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid RTC output buffer");
    ESP_RETURN_ON_FALSE(s_rtc_ready, ESP_ERR_INVALID_STATE, TAG, "RTC not initialized");

    uint8_t raw[7] = {0};
    bool time_valid = false;
    ESP_RETURN_ON_ERROR(rtc_read_raw_time(raw, &time_valid), TAG, "RTC read failed");
    ESP_RETURN_ON_FALSE(time_valid, ESP_ERR_INVALID_STATE, TAG, "RTC time invalid");

    memset(out_tm, 0, sizeof(*out_tm));
    out_tm->tm_sec = bcd_to_dec(raw[0] & 0x7F);
    out_tm->tm_min = bcd_to_dec(raw[1] & 0x7F);
    out_tm->tm_hour = bcd_to_dec(raw[2] & 0x3F);
    out_tm->tm_mday = bcd_to_dec(raw[3] & 0x3F);
    out_tm->tm_wday = bcd_to_dec(raw[4] & 0x07);
    out_tm->tm_mon = bcd_to_dec(raw[5] & 0x1F) - 1;
    out_tm->tm_year = (bcd_to_dec(raw[6]) + 1970) - 1900;
    out_tm->tm_isdst = -1;

    return ESP_OK;
}

/**
 * @brief Initialize the RTC and set it only when retained time is invalid.
 *
 * @details Configures the PCF85063A control register, checks whether retained
 * RTC time is valid, and only writes the parsed compiler build timestamp when
 * the oscillator stop flag indicates the current clock contents are invalid.
 * This preserves RTC-backed time across normal power cycles when backup power
 * is present.
 *
 * @param[in] offline Startup offline-mode flag.
 *
 * @return
 *      - ESP_OK: RTC configured successfully
 *      - ESP_ERR_*: RTC communication or timestamp parsing failed
 */
esp_err_t peripherals_manager_init_rtc_now(bool offline)
{
    ESP_LOGI(TAG, "RTC init begin (offline=%d)", offline);
    if (offline) {
        ESP_LOGI(TAG, "RTC init running in offline mode; caller downgrades failures to warnings");
    }

    esp_err_t ret;
    uint8_t ctrl1_data[2] = {
        RTC_REG_CTRL1,
        RTC_CTRL1_CAP_SEL,
    };
    ESP_LOGI(TAG, "RTC init step: writing control register");
    ret = hardware_i2c_write_raw(RTC_ADDR, ctrl1_data, sizeof(ctrl1_data));
    ESP_LOGI(TAG, "RTC init step result: control register write -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RTC control init failed");
        s_rtc_ready = false;
        ESP_LOGW(TAG, "RTC init end -> %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t raw[7] = {0};
    bool time_valid = false;
    ESP_LOGI(TAG, "RTC init step: reading retained RTC time");
    ret = rtc_read_raw_time(raw, &time_valid);
    ESP_LOGI(TAG,
             "RTC init step result: retained RTC read -> %s (time_valid=%d)",
             esp_err_to_name(ret),
             time_valid);
    if (ret == ESP_OK && time_valid) {
        s_rtc_ready = true;
        ESP_LOGI(TAG, "RTC retained time is valid; keeping current clock");
        ESP_LOGI(TAG, "RTC init step: logging current RTC time");
        ret = rtc_log_now();
        ESP_LOGI(TAG, "RTC init step result: rtc_log_now -> %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "RTC init end -> %s", esp_err_to_name(ret));
        return ret;
    }

    struct tm build_tm = {0};
    ESP_LOGI(TAG, "RTC init step: parsing build timestamp");
    ret = parse_build_time(&build_tm);
    ESP_LOGI(TAG, "RTC init step result: parse build timestamp -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RTC build-time parse failed");
        s_rtc_ready = false;
        ESP_LOGW(TAG, "RTC init end -> %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "RTC init step: writing parsed build timestamp into RTC");
    ret = peripherals_manager_set_rtc_time(&build_tm);
    ESP_LOGI(TAG, "RTC init step result: set RTC time -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RTC datetime set failed");
        s_rtc_ready = false;
        ESP_LOGW(TAG, "RTC init end -> %s", esp_err_to_name(ret));
        return ret;
    }

    s_rtc_ready = true;
    ESP_LOGI(TAG,
             "RTC initialized to build time %04d-%02d-%02d %02d:%02d:%02d",
             build_tm.tm_year + 1900,
             build_tm.tm_mon + 1,
             build_tm.tm_mday,
             build_tm.tm_hour,
             build_tm.tm_min,
             build_tm.tm_sec);
    ESP_LOGI(TAG, "RTC init step: logging current RTC time");
    ret = rtc_log_now();
    ESP_LOGI(TAG, "RTC init step result: rtc_log_now -> %s", esp_err_to_name(ret));
    ESP_LOGI(TAG, "RTC init end -> %s", esp_err_to_name(ret));
    return ret;
}

/**
 * @brief Initialize the Wi-Fi support stack once.
 *
 * @details Brings up NVS, esp-netif, and the default event loop in an
 * idempotent way so higher-level Wi-Fi tests can start the STA interface.
 *
 * @return
 *      - ESP_OK: Support stack ready
 *      - ESP_ERR_*: One of the required subsystems failed to initialize
 */
static esp_err_t wifi_stack_init_once(void)
{
    ESP_LOGI(TAG, "Wi-Fi stack init begin");
    esp_err_t ret = nvs_flash_init();
    ESP_LOGI(TAG, "Wi-Fi stack step result: nvs_flash_init -> %s", esp_err_to_name(ret));
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Wi-Fi stack step: NVS requires erase before re-init");
        ret = nvs_flash_erase();
        ESP_LOGI(TAG, "Wi-Fi stack step result: nvs_flash_erase -> %s", esp_err_to_name(ret));
        ESP_RETURN_ON_ERROR(ret, TAG, "NVS erase failed");
        ESP_LOGI(TAG, "Wi-Fi stack step: retrying nvs_flash_init after erase");
        ret = nvs_flash_init();
        ESP_LOGI(TAG, "Wi-Fi stack step result: nvs_flash_init retry -> %s", esp_err_to_name(ret));
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "NVS init failed");

    ESP_LOGI(TAG, "Wi-Fi stack step: esp_netif_init");
    ret = esp_netif_init();
    ESP_LOGI(TAG, "Wi-Fi stack step result: esp_netif_init -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_netif init failed");
        return ret;
    }

    ESP_LOGI(TAG, "Wi-Fi stack step: esp_event_loop_create_default");
    ret = esp_event_loop_create_default();
    ESP_LOGI(TAG, "Wi-Fi stack step result: esp_event_loop_create_default -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Default event loop init failed");
        return ret;
    }

    ESP_LOGI(TAG, "Wi-Fi stack init end -> %s", esp_err_to_name(ESP_OK));
    return ESP_OK;
}

/**
 * @brief Initialize Wi-Fi in station mode and run a scan test.
 *
 * @details Creates the default station network interface if needed, starts the
 * Wi-Fi driver, performs a blocking access-point scan, and logs the number of
 * visible networks as a functional bring-up test.
 *
 * @param[in] offline Startup offline-mode flag.
 *
 * @return
 *      - ESP_OK: Wi-Fi initialized and scan completed successfully
 *      - ESP_ERR_*: Wi-Fi init/start/scan failed
 */
esp_err_t peripherals_manager_init_wifi(bool offline)
{
    ESP_LOGI(TAG, "Wi-Fi init begin (offline=%d)", offline);
    if (offline) {
        ESP_LOGI(TAG, "Wi-Fi init running in offline mode; caller downgrades failures to warnings");
    }

    if (s_wifi_ready) {
        ESP_LOGI(TAG, "Wi-Fi init end -> already ready");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Wi-Fi init step: wifi_stack_init_once");
    esp_err_t ret = wifi_stack_init_once();
    ESP_LOGI(TAG, "Wi-Fi init step result: wifi_stack_init_once -> %s", esp_err_to_name(ret));
    ESP_RETURN_ON_ERROR(ret, TAG, "Wi-Fi support stack init failed");

    ESP_LOGI(TAG, "Wi-Fi init step: checking default STA netif");
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        ESP_LOGI(TAG, "Wi-Fi init step: creating default STA netif");
        if (esp_netif_create_default_wifi_sta() == NULL) {
            ESP_LOGW(TAG, "Failed to create default Wi-Fi STA netif");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Wi-Fi init step result: create default STA netif -> OK");
    } else {
        ESP_LOGI(TAG, "Wi-Fi init step result: default STA netif already exists");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_LOGI(TAG, "Wi-Fi init step: esp_wifi_init");
    ret = esp_wifi_init(&cfg);
    ESP_LOGI(TAG, "Wi-Fi init step result: esp_wifi_init -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Wi-Fi driver init failed");
        return ret;
    }

    ESP_LOGI(TAG, "Wi-Fi init step: esp_wifi_set_mode(STA)");
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "Wi-Fi init step result: esp_wifi_set_mode -> %s", esp_err_to_name(ret));
    ESP_RETURN_ON_ERROR(ret, TAG, "Wi-Fi mode set failed");

    ESP_LOGI(TAG, "Wi-Fi init step: esp_wifi_set_storage(RAM)");
    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    ESP_LOGI(TAG, "Wi-Fi init step result: esp_wifi_set_storage -> %s", esp_err_to_name(ret));
    ESP_RETURN_ON_ERROR(ret, TAG, "Wi-Fi storage set failed");

    ESP_LOGI(TAG, "Wi-Fi init step: esp_wifi_start");
    ret = esp_wifi_start();
    ESP_LOGI(TAG, "Wi-Fi init step result: esp_wifi_start -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "Wi-Fi start failed");
        return ret;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    ESP_LOGI(TAG, "Wi-Fi init step: blocking AP scan start");
    ret = esp_wifi_scan_start(&scan_cfg, true);
    ESP_LOGI(TAG, "Wi-Fi init step result: esp_wifi_scan_start -> %s", esp_err_to_name(ret));
    ESP_RETURN_ON_ERROR(ret, TAG, "Wi-Fi scan failed");

    uint16_t ap_count = 0;
    ESP_LOGI(TAG, "Wi-Fi init step: reading AP count");
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Wi-Fi init step result: esp_wifi_scan_get_ap_num -> %s (ap_count=%u)",
             esp_err_to_name(ret),
             ap_count);
    ESP_RETURN_ON_ERROR(ret, TAG, "Wi-Fi AP count read failed");
    s_last_wifi_ap_count = ap_count;
    ESP_LOGI(TAG, "Wi-Fi scan complete: %u APs found", ap_count);

    wifi_ap_record_t ap_records[5] = {0};
    uint16_t ap_records_count = 5;
    if (ap_count > 0) {
        ap_records_count = ap_count < ap_records_count ? ap_count : ap_records_count;
        ESP_LOGI(TAG, "Wi-Fi init step: reading up to %u AP records", ap_records_count);
        ret = esp_wifi_scan_get_ap_records(&ap_records_count, ap_records);
        ESP_LOGI(TAG,
                 "Wi-Fi init step result: esp_wifi_scan_get_ap_records -> %s (records=%u)",
                 esp_err_to_name(ret),
                 ap_records_count);
        if (ret == ESP_OK) {
            for (uint16_t i = 0; i < ap_records_count; ++i) {
                ESP_LOGI(TAG,
                         "Wi-Fi AP[%u]: SSID='%s' RSSI=%d channel=%u",
                         i,
                         (const char *)ap_records[i].ssid,
                         ap_records[i].rssi,
                         ap_records[i].primary);
            }
        }
    }

    s_wifi_ready = true;
    ESP_LOGI(TAG, "Wi-Fi init end -> %s", esp_err_to_name(ESP_OK));
    return ESP_OK;
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
 * @brief Log RS485 diagnostic bytes when the temporary probe is enabled.
 *
 * @details Emits a hex dump for the supplied payload only when the compile-time
 * diagnostics switch is active so normal builds keep their previous log volume.
 *
 * @param[in] label Short text describing the payload direction/purpose.
 * @param[in] data Byte buffer to print.
 * @param[in] len Number of valid bytes in `data`.
 */
static void rs485_log_hex_dump(const char *label, const uint8_t *data, size_t len)
{
#if ESPRESSO_RS485_DIAGNOSTICS
    if (label == NULL || data == NULL || len == 0U) {
        ESP_LOGI(TAG, "RS485 diag: %s hex dump skipped", label != NULL ? label : "unnamed");
        return;
    }

    ESP_LOGI(TAG, "RS485 diag: %s len=%u", label, (unsigned int)len);
    ESP_LOG_BUFFER_HEX(TAG, data, len);
#else
    (void)label;
    (void)data;
    (void)len;
#endif
}

/**
 * @brief Log current UART diagnostic status for the RS485 port.
 *
 * @details Captures buffered byte counts and low-level UART status bits around
 * controller requests so transport-layer failures can be classified without
 * changing the live request flow.
 *
 * @param[in] label Short text describing the capture point.
 */
static void rs485_log_uart_state(const char *label)
{
#if ESPRESSO_RS485_DIAGNOSTICS
    size_t buffered_len = 0U;
    esp_err_t buffered_ret = uart_get_buffered_data_len(RS485_UART_PORT, &buffered_len);
    uint32_t baud_rate = 0U;
    esp_err_t baud_ret = uart_get_baudrate(RS485_UART_PORT, &baud_rate);
    ESP_LOGI(TAG,
             "RS485 diag: %s buffered=%s(%u) baud=%s(%" PRIu32 ")",
             label != NULL ? label : "state",
             esp_err_to_name(buffered_ret),
             (unsigned int)buffered_len,
             esp_err_to_name(baud_ret),
             baud_rate);
#else
    (void)label;
#endif
}

/**
 * @brief Normalize an RS485 reply into a compact printable string.
 *
 * @details Removes non-printable bytes, collapses whitespace runs, and keeps
 * only ASCII-visible content so controller parsing does not depend on raw UART
 * framing noise or echoed delimiters.
 *
 * @param[in] input Raw reply buffer.
 * @param[in] input_len Raw reply length in bytes.
 * @param[out] output Destination printable string buffer.
 * @param[in] output_len Destination buffer size in bytes.
 */
static void rs485_normalize_reply_text(const uint8_t *input,
                                       size_t input_len,
                                       char *output,
                                       size_t output_len)
{
    if (output == NULL || output_len == 0U) {
        return;
    }

    size_t out_index = 0U;
    bool last_was_space = true;
    output[0] = '\0';

    for (size_t index = 0; index < input_len && out_index + 1U < output_len; index++) {
        unsigned char ch = input[index];
        bool is_space = (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t');
        bool is_printable = (ch >= 32U && ch <= 126U);

        if (is_space) {
            if (!last_was_space && out_index + 1U < output_len) {
                output[out_index++] = ' ';
            }
            last_was_space = true;
            continue;
        }

        if (!is_printable) {
            continue;
        }

        output[out_index++] = (char)ch;
        last_was_space = false;
    }

    while (out_index > 0U && output[out_index - 1U] == ' ') {
        out_index--;
    }

    output[out_index] = '\0';
}

/**
 * @brief Read and normalize one RS485 controller reply.
 *
 * @details Collects UART bytes until the configured timeout expires or a short
 * quiet window follows the first received bytes, then returns a compact
 * printable reply string for status/version parsing.
 *
 * @param[out] out_reply Destination normalized reply string.
 * @param[in] out_len Destination buffer size in bytes.
 *
 * @return
 *      - ESP_OK: Reply read and normalized successfully
 *      - ESP_ERR_INVALID_ARG: Output buffer invalid
 *      - ESP_ERR_TIMEOUT: No reply bytes received before timeout
 */
static esp_err_t rs485_read_normalized_reply(char *out_reply, size_t out_len)
{
    ESP_RETURN_ON_FALSE(out_reply != NULL && out_len > 1U,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "Invalid RS485 normalized reply buffer");

    uint8_t raw_reply[128] = {0};
    size_t total_len = 0U;
    bool saw_data = false;
    int64_t start_us = esp_timer_get_time();
    int64_t last_data_us = start_us;

    while (((esp_timer_get_time() - start_us) / 1000LL) < RS485_CONTROLLER_TIMEOUT_MS &&
           total_len < sizeof(raw_reply) - 1U) {
        int rx_len = uart_read_bytes(RS485_UART_PORT,
                                     &raw_reply[total_len],
                                     sizeof(raw_reply) - 1U - total_len,
                                     pdMS_TO_TICKS(60));
        if (rx_len > 0) {
#if ESPRESSO_RS485_DIAGNOSTICS
            ESP_LOGI(TAG,
                     "RS485 diag: uart_read_bytes chunk=%d elapsed_ms=%lld",
                     rx_len,
                     (esp_timer_get_time() - start_us) / 1000LL);
#endif
            total_len += (size_t)rx_len;
            saw_data = true;
            last_data_us = esp_timer_get_time();
            continue;
        }

        if (saw_data && ((esp_timer_get_time() - last_data_us) / 1000LL) >= 80LL) {
            break;
        }
    }

    if (!saw_data) {
        out_reply[0] = '\0';
        rs485_log_uart_state("read timeout");
        return ESP_ERR_TIMEOUT;
    }

    rs485_log_hex_dump("RX raw", raw_reply, total_len);
    rs485_normalize_reply_text(raw_reply, total_len, out_reply, out_len);
#if ESPRESSO_RS485_DIAGNOSTICS
    ESP_LOGI(TAG, "RS485 diag: normalized reply='%s'", out_reply);
#endif
    return ESP_OK;
}

/**
 * @brief Extract a version-like token from a normalized controller reply.
 *
 * @details Scans the normalized reply for the first token that looks like a
 * dotted software version so echoed request text or status prefixes do not
 * become the stored version string.
 *
 * @param[in] normalized_reply Printable normalized reply string.
 * @param[out] out_version Destination version string buffer.
 * @param[in] out_len Destination buffer size in bytes.
 *
 * @return `true` when a version-like token was extracted.
 */
static bool rs485_extract_version_token(const char *normalized_reply,
                                        char *out_version,
                                        size_t out_len)
{
    if (normalized_reply == NULL || out_version == NULL || out_len <= 1U) {
        return false;
    }

    const char *cursor = normalized_reply;
    while (*cursor != '\0') {
        while (*cursor == ' ') {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        const char *token_start = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            cursor++;
        }

        size_t token_len = (size_t)(cursor - token_start);
        bool has_digit = false;
        bool has_dot = false;
        bool valid_chars = true;
        for (size_t i = 0; i < token_len; i++) {
            char ch = token_start[i];
            if (ch >= '0' && ch <= '9') {
                has_digit = true;
            } else if (ch == '.') {
                has_dot = true;
            } else {
                valid_chars = false;
                break;
            }
        }

        if (valid_chars && has_digit && has_dot) {
            size_t copy_len = (token_len < (out_len - 1U)) ? token_len : (out_len - 1U);
            memcpy(out_version, token_start, copy_len);
            out_version[copy_len] = '\0';
            return true;
        }
    }

    return false;
}

/**
 * @brief Convert controller status enum into printable text.
 *
 * @details Centralizes user-visible wording for controller initialization
 * results so logs and UI share the same labels.
 *
 * @param[in] status Controller status enum.
 *
 * @return Constant text description for the provided status.
 */
const char *peripherals_manager_controller_status_to_string(peripherals_controller_status_t status)
{
    switch (status) {
    case PERIPHERALS_CONTROLLER_STATUS_READY:
        return "READY";
    case PERIPHERALS_CONTROLLER_STATUS_BUSY:
        return "BUSY";
    case PERIPHERALS_CONTROLLER_STATUS_ERROR:
        return "ERROR";
    case PERIPHERALS_CONTROLLER_STATUS_OFFLINE:
        return "OFFLINE";
    case PERIPHERALS_CONTROLLER_STATUS_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

/**
 * @brief Override cached connection state for offline simulation.
 *
 * @details Updates the module-level status fields used by the UI so offline
 * initialization can present simulated connectivity without running real Wi-Fi
 * or controller transactions.
 *
 * @param[in] wifi_ready Simulated Wi-Fi readiness flag.
 * @param[in] status Simulated controller status value.
 */
void peripherals_manager_set_connection_simulation(bool wifi_ready,
                                                   peripherals_controller_status_t status)
{
    s_wifi_ready = wifi_ready;
    s_last_wifi_ap_count = 0;
    s_last_controller_status = status;
}

/**
 * @brief Request controller initialization over RS485 and decode the reply.
 *
 * @details Sends a compact `INIT?` probe to the external controller and waits
 * briefly for a short textual response. Missing responses are treated as an
 * offline controller state so the application can continue in a known mode.
 *
 * @param[in] offline Startup offline-mode flag.
 * @param[out] out_status Destination status value.
 *
 * @return
 *      - ESP_OK: Request completed and status was determined
 *      - ESP_ERR_INVALID_ARG: `out_status` is NULL
 *      - ESP_ERR_*: RS485 transport path could not be initialized or used
 */
esp_err_t peripherals_manager_request_controller_init(bool offline,
                                                      peripherals_controller_status_t *out_status)
{
    ESP_LOGI(TAG, "Controller init request begin (offline=%d)", offline);
    if (offline) {
        ESP_LOGI(TAG, "Controller init probe running in offline mode; caller downgrades failures to warnings");
    }

    ESP_RETURN_ON_FALSE(out_status != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid controller status buffer");

    if (!s_rs485_ready) {
        ESP_LOGI(TAG, "Controller init step: rs485_init");
        rs485_init();
        ESP_LOGI(TAG, "Controller init step result: rs485_init -> ready=%d", s_rs485_ready);
    }
    ESP_RETURN_ON_FALSE(s_rs485_ready, ESP_FAIL, TAG, "RS485 not ready");

    static const char *request = "INIT?\r\n";
    char normalized_reply[96] = {0};
    *out_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;
    s_last_controller_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;

    rs485_log_uart_state("before controller init flush");
    ESP_LOGI(TAG, "Controller init step: uart_flush_input");
    uart_flush_input(RS485_UART_PORT);
    ESP_LOGI(TAG, "Controller init step result: uart_flush_input -> done");
    ESP_LOGI(TAG, "Controller init step: sending INIT? request");
    rs485_log_hex_dump("TX INIT?", (const uint8_t *)request, strlen(request));
    int written = uart_write_bytes(RS485_UART_PORT, request, strlen(request));
    ESP_LOGI(TAG, "Controller init step result: uart_write_bytes -> %d", written);
    ESP_RETURN_ON_FALSE(written >= 0,
                        ESP_FAIL,
                        TAG,
                        "Controller init request send failed");
    ESP_LOGI(TAG, "Controller init step result: INIT? request send -> OK");
    ESP_LOGI(TAG, "Controller init step: waiting for TX completion");
    esp_err_t tx_ret = uart_wait_tx_done(RS485_UART_PORT, pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Controller init step result: uart_wait_tx_done -> %s", esp_err_to_name(tx_ret));
    rs485_log_uart_state("after controller init tx");

    ESP_LOGI(TAG, "Controller init step: reading INIT? response");
    esp_err_t read_ret = rs485_read_normalized_reply(normalized_reply, sizeof(normalized_reply));
    ESP_LOGI(TAG,
             "Controller init step result: rs485_read_normalized_reply -> %s reply='%s'",
             esp_err_to_name(read_ret),
             normalized_reply);
    if (read_ret == ESP_ERR_TIMEOUT || normalized_reply[0] == '\0') {
        *out_status = PERIPHERALS_CONTROLLER_STATUS_OFFLINE;
        s_last_controller_status = *out_status;
        ESP_LOGW(TAG, "Controller init request timed out");
        ESP_LOGI(TAG, "Controller init end -> status=%s ret=%s",
                 peripherals_manager_controller_status_to_string(*out_status),
                 esp_err_to_name(ESP_OK));
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(read_ret, TAG, "Controller init reply read failed");

    if (strstr(normalized_reply, "READY") != NULL) {
        *out_status = PERIPHERALS_CONTROLLER_STATUS_READY;
    } else if (strstr(normalized_reply, "BUSY") != NULL) {
        *out_status = PERIPHERALS_CONTROLLER_STATUS_BUSY;
    } else if (strstr(normalized_reply, "ERR") != NULL ||
               strstr(normalized_reply, "ERROR") != NULL) {
        *out_status = PERIPHERALS_CONTROLLER_STATUS_ERROR;
    } else {
        *out_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;
    }

    s_last_controller_status = *out_status;
    ESP_LOGI(TAG, "Controller init response: %s", normalized_reply);
    ESP_LOGI(TAG, "Controller init end -> status=%s ret=%s",
             peripherals_manager_controller_status_to_string(*out_status),
             esp_err_to_name(ESP_OK));
    return ESP_OK;
}

/**
 * @brief Request the controller software version over RS485.
 *
 * @details Sends a compact `VER?` probe and returns the received reply string
 * for startup compatibility checks. Missing or empty replies are treated as a
 * transport failure.
 *
 * @param[in] offline Startup offline-mode flag.
 * @param[out] out_version Destination string buffer.
 * @param[in] out_len Destination buffer length.
 *
 * @return
 *      - ESP_OK: Query succeeded and a reply string was captured
 *      - ESP_ERR_INVALID_ARG: Output buffer is invalid
 *      - ESP_ERR_*: RS485 path could not be initialized or the query failed
 */
esp_err_t peripherals_manager_request_controller_version(bool offline,
                                                         char *out_version,
                                                         size_t out_len)
{
    ESP_LOGI(TAG, "Controller version request begin (offline=%d)", offline);
    if (offline) {
        ESP_LOGI(TAG, "Controller version probe running in offline mode; caller downgrades failures to warnings");
    }

    ESP_RETURN_ON_FALSE(out_version != NULL && out_len > 1U, ESP_ERR_INVALID_ARG, TAG, "Invalid version buffer");

    if (!s_rs485_ready) {
        ESP_LOGI(TAG, "Controller version step: rs485_init");
        rs485_init();
        ESP_LOGI(TAG, "Controller version step result: rs485_init -> ready=%d", s_rs485_ready);
    }
    ESP_RETURN_ON_FALSE(s_rs485_ready, ESP_FAIL, TAG, "RS485 not ready");

    static const char *request = "VER?\r\n";
    char normalized_reply[96] = {0};
    out_version[0] = '\0';

    rs485_log_uart_state("before controller version flush");
    ESP_LOGI(TAG, "Controller version step: uart_flush_input");
    uart_flush_input(RS485_UART_PORT);
    ESP_LOGI(TAG, "Controller version step result: uart_flush_input -> done");
    ESP_LOGI(TAG, "Controller version step: sending VER? request");
    rs485_log_hex_dump("TX VER?", (const uint8_t *)request, strlen(request));
    int written = uart_write_bytes(RS485_UART_PORT, request, strlen(request));
    ESP_LOGI(TAG, "Controller version step result: uart_write_bytes -> %d", written);
    ESP_RETURN_ON_FALSE(written >= 0,
                        ESP_FAIL,
                        TAG,
                        "Controller version request send failed");
    ESP_LOGI(TAG, "Controller version step result: VER? request send -> OK");
    ESP_LOGI(TAG, "Controller version step: waiting for TX completion");
    esp_err_t tx_ret = uart_wait_tx_done(RS485_UART_PORT, pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Controller version step result: uart_wait_tx_done -> %s", esp_err_to_name(tx_ret));
    rs485_log_uart_state("after controller version tx");

    ESP_LOGI(TAG, "Controller version step: reading VER? response");
    esp_err_t read_ret = rs485_read_normalized_reply(normalized_reply, sizeof(normalized_reply));
    ESP_LOGI(TAG,
             "Controller version step result: rs485_read_normalized_reply -> %s reply='%s'",
             esp_err_to_name(read_ret),
             normalized_reply);
    ESP_RETURN_ON_ERROR(read_ret, TAG, "Controller version request timed out");

    if (!rs485_extract_version_token(normalized_reply, out_version, out_len)) {
        snprintf(out_version, out_len, "%s", normalized_reply);
    }
    ESP_LOGI(TAG, "Controller version response: %s", out_version);
    ESP_LOGI(TAG, "Controller version request end -> %s", esp_err_to_name(ESP_OK));
    return ESP_OK;
}

/**
 * @brief Collect a UI-friendly snapshot of connection information.
 *
 * @details Reads the current Wi-Fi IP assignment if available and combines it
 * with the latest cached transport and telemetry state tracked by the
 * peripheral manager.
 *
 * @param[out] out_info Destination snapshot structure.
 *
 * @return
 *      - ESP_OK: Snapshot filled successfully
 *      - ESP_ERR_INVALID_ARG: `out_info` is NULL
 */
esp_err_t peripherals_manager_get_connection_info(peripherals_connection_info_t *out_info)
{
    ESP_RETURN_ON_FALSE(out_info != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid connection info buffer");

    memset(out_info, 0, sizeof(*out_info));
    const system_constants_data_t *constants = system_constants_get();
    snprintf(out_info->ip_address, sizeof(out_info->ip_address), "Not assigned");
    snprintf(out_info->port_text,
             sizeof(out_info->port_text),
             "%s @ %d",
             constants->connection_port,
             constants->connection_baud_rate);

    out_info->wifi_ready = s_wifi_ready;
    out_info->rtc_ready = s_rtc_ready;
    out_info->tf_ready = s_sd_ready;
    out_info->wifi_ap_count = s_last_wifi_ap_count;
    out_info->controller_status = s_last_controller_status;

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        esp_netif_ip_info_t ip_info = {0};
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            snprintf(out_info->ip_address,
                     sizeof(out_info->ip_address),
                     IPSTR,
                     IP2STR(&ip_info.ip));
        }
    }

    return ESP_OK;
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
 * @brief Verify the currently mounted TF card by writing and reading a file.
 *
 * @details Reuses the quick SD card file smoke test and converts it to an
 * `esp_err_t` status for application startup checks.
 *
 * @return
 *      - ESP_OK: File write/read verification passed
 *      - ESP_FAIL: Verification failed
 */
static esp_err_t sd_card_verify_file_io(void)
{
    const char *path = SD_MOUNT_POINT "/CHECK.TXT";
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "SD file write open failed");
        return ESP_FAIL;
    }

    fprintf(f, "Eyal espresso SD check\n");
    fclose(f);

    char line[64] = {0};
    f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "SD file read open failed");
        return ESP_FAIL;
    }

    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        ESP_LOGW(TAG, "SD file read failed");
        return ESP_FAIL;
    }

    fclose(f);
    ESP_LOGI(TAG, "SD test read: %s", line);
    return ESP_OK;
}

/**
 * @brief Initialize SD over SDSPI using Waveshare demo pinout.
 *
 * @details Uses CH422G to drive board-level SD CS line behavior similarly to
 * vendor example where GPIO CS is not routed directly to ESP pin. If the card
 * is present but does not yet contain a FAT filesystem, the mount helper is
 * allowed to create one so first-boot TF validation can succeed.
 */
static void sd_card_init(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    const uint8_t display_lines_high = CH422G_IO_TOUCH_RST | CH422G_IO_BACKLIGHT | CH422G_IO_LCD_RST;

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

    /* Keep display-related lines high while pulling SD CS low for mount. */
    if (ch422g_write_io(display_lines_high) != ESP_OK) {
        ESP_LOGW(TAG, "CH422G SD CS prep failed");
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_GPIO;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_cfg, &s_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        ch422g_write_io(display_lines_high | CH422G_IO_SD_CS);
        spi_bus_free(host.slot);
        s_sd_ready = false;
        return;
    }

    /* Keep SD CS asserted because this board routes CS through CH422G, not a
     * native ESP GPIO the SDSPI driver can toggle for later file operations. */
    ch422g_write_io(display_lines_high);

    s_sd_ready = true;
    ESP_LOGI(TAG, "SD mounted at %s", SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_sd_card);
}

/**
 * @brief Initialize and verify TF card support without starting other peripherals.
 *
 * @details Mounts the card if needed and performs a simple file IO smoke test so
 * callers can fail or warn early during normal application startup.
 *
 * @param[in] offline Startup offline-mode flag.
 *
 * @return
 *      - ESP_OK: TF card mounted and read/write test passed
 *      - ESP_ERR_*: Mount or verification failed
 */
esp_err_t peripherals_manager_init_tf_card(bool offline)
{
    ESP_LOGI(TAG, "TF card init begin (offline=%d)", offline);
    if (offline) {
        ESP_LOGI(TAG, "TF card init running in offline mode; caller downgrades failures to warnings");
    }

    if (s_sd_ready && s_sd_card != NULL) {
        ESP_LOGI(TAG, "TF card init end -> already ready");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "TF card init step: sd_card_init");
    sd_card_init();
    ESP_LOGI(TAG, "TF card init step result: sd_card_init -> ready=%d card=%p",
             s_sd_ready,
             (void *)s_sd_card);
    if (!s_sd_ready || s_sd_card == NULL) {
        ESP_LOGW(TAG, "TF card init end -> %s", esp_err_to_name(ESP_FAIL));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TF card init step: sd_card_verify_file_io");
    esp_err_t ret = sd_card_verify_file_io();
    ESP_LOGI(TAG, "TF card init step result: sd_card_verify_file_io -> %s", esp_err_to_name(ret));
    ESP_LOGI(TAG, "TF card init end -> %s", esp_err_to_name(ret));
    return ret;
}

/**
 * @brief Report the current TF card ready state.
 *
 * @details Exposes the internal SD mount state for higher-level modules that
 * want to gate filesystem operations on successful initialization.
 *
 * @return
 *      - true: TF card mounted
 *      - false: TF card not mounted
 */
bool peripherals_manager_is_tf_card_ready(void)
{
    return s_sd_ready;
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
            if (s_rtc_ready) {
                rtc_log_now();
            }
            last_rtc_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Initialize and start all non-display board peripherals.
 *
 * @details Runs one-time setup derived from Waveshare demos and launches a
 * periodic service task that exercises integrated peripheral paths. The
 * `offline` flag keeps the same initialization sequence but documents that
 * higher-level startup code may downgrade failures to warnings.
 *
 * @param[in] offline Startup offline-mode flag.
 *
 * @return
 *      - ESP_OK: Manager startup task created
 *      - ESP_ERR_*: Failed to create manager task
 */
esp_err_t peripherals_manager_start(bool offline)
{
    if (offline) {
        ESP_LOGI(TAG, "Peripheral manager start running in offline mode; caller downgrades failures to warnings");
    }

    i2c_scan();
    io_self_test();
    peripherals_manager_init_rtc_now(offline);
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
