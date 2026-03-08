/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "peripherals_manager.h"

#include <stdio.h>
#include <time.h>
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
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/ip4_addr.h"

#include "hardware_init.h"
#include "system_constants.h"

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
 * @return
 *      - ESP_OK: RTC configured successfully
 *      - ESP_ERR_*: RTC communication or timestamp parsing failed
 */
esp_err_t peripherals_manager_init_rtc_now(void)
{
    esp_err_t ret;
    uint8_t ctrl1_data[2] = {
        RTC_REG_CTRL1,
        RTC_CTRL1_CAP_SEL,
    };
    ret = hardware_i2c_write_raw(RTC_ADDR, ctrl1_data, sizeof(ctrl1_data));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RTC control init failed");
        s_rtc_ready = false;
        return ret;
    }

    uint8_t raw[7] = {0};
    bool time_valid = false;
    ret = rtc_read_raw_time(raw, &time_valid);
    if (ret == ESP_OK && time_valid) {
        s_rtc_ready = true;
        ESP_LOGI(TAG, "RTC retained time is valid; keeping current clock");
        return rtc_log_now();
    }

    struct tm build_tm = {0};
    ret = parse_build_time(&build_tm);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RTC build-time parse failed");
        s_rtc_ready = false;
        return ret;
    }

    ret = peripherals_manager_set_rtc_time(&build_tm);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RTC datetime set failed");
        s_rtc_ready = false;
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
    return rtc_log_now();
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
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "NVS init failed");

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_netif init failed");
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Default event loop init failed");
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief Initialize Wi-Fi in station mode and run a scan test.
 *
 * @details Creates the default station network interface if needed, starts the
 * Wi-Fi driver, performs a blocking access-point scan, and logs the number of
 * visible networks as a functional bring-up test.
 *
 * @return
 *      - ESP_OK: Wi-Fi initialized and scan completed successfully
 *      - ESP_ERR_*: Wi-Fi init/start/scan failed
 */
esp_err_t peripherals_manager_init_wifi(void)
{
    if (s_wifi_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(wifi_stack_init_once(), TAG, "Wi-Fi support stack init failed");

    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        if (esp_netif_create_default_wifi_sta() == NULL) {
            ESP_LOGW(TAG, "Failed to create default Wi-Fi STA netif");
            return ESP_FAIL;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Wi-Fi driver init failed");
        return ret;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi mode set failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi storage set failed");

    ret = esp_wifi_start();
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

    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_cfg, true), TAG, "Wi-Fi scan failed");

    uint16_t ap_count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&ap_count), TAG, "Wi-Fi AP count read failed");
    s_last_wifi_ap_count = ap_count;
    ESP_LOGI(TAG, "Wi-Fi scan complete: %u APs found", ap_count);

    wifi_ap_record_t ap_records[5] = {0};
    uint16_t ap_records_count = 5;
    if (ap_count > 0) {
        ap_records_count = ap_count < ap_records_count ? ap_count : ap_records_count;
        if (esp_wifi_scan_get_ap_records(&ap_records_count, ap_records) == ESP_OK) {
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
 * @brief Request controller initialization over RS485 and decode the reply.
 *
 * @details Sends a compact `INIT?` probe to the external controller and waits
 * briefly for a short textual response. Missing responses are treated as an
 * offline controller state so the application can continue in a known mode.
 *
 * @param[out] out_status Destination status value.
 *
 * @return
 *      - ESP_OK: Request completed and status was determined
 *      - ESP_ERR_INVALID_ARG: `out_status` is NULL
 *      - ESP_ERR_*: RS485 transport path could not be initialized or used
 */
esp_err_t peripherals_manager_request_controller_init(peripherals_controller_status_t *out_status)
{
    ESP_RETURN_ON_FALSE(out_status != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid controller status buffer");

    if (!s_rs485_ready) {
        rs485_init();
    }
    ESP_RETURN_ON_FALSE(s_rs485_ready, ESP_FAIL, TAG, "RS485 not ready");

    static const char *request = "INIT?\r\n";
    uint8_t rx_buf[64] = {0};
    *out_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;
    s_last_controller_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;

    uart_flush_input(RS485_UART_PORT);
    ESP_RETURN_ON_FALSE(uart_write_bytes(RS485_UART_PORT, request, strlen(request)) >= 0,
                        ESP_FAIL,
                        TAG,
                        "Controller init request send failed");

    int rx_len = uart_read_bytes(RS485_UART_PORT,
                                 rx_buf,
                                 sizeof(rx_buf) - 1,
                                 pdMS_TO_TICKS(RS485_CONTROLLER_TIMEOUT_MS));
    if (rx_len <= 0) {
        *out_status = PERIPHERALS_CONTROLLER_STATUS_OFFLINE;
        s_last_controller_status = *out_status;
        ESP_LOGW(TAG, "Controller init request timed out");
        return ESP_OK;
    }

    rx_buf[rx_len] = '\0';
    if (strstr((const char *)rx_buf, "READY") != NULL) {
        *out_status = PERIPHERALS_CONTROLLER_STATUS_READY;
    } else if (strstr((const char *)rx_buf, "BUSY") != NULL) {
        *out_status = PERIPHERALS_CONTROLLER_STATUS_BUSY;
    } else if (strstr((const char *)rx_buf, "ERR") != NULL) {
        *out_status = PERIPHERALS_CONTROLLER_STATUS_ERROR;
    } else {
        *out_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;
    }

    s_last_controller_status = *out_status;
    ESP_LOGI(TAG, "Controller init response: %s", (const char *)rx_buf);
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
 * @return
 *      - ESP_OK: TF card mounted and read/write test passed
 *      - ESP_ERR_*: Mount or verification failed
 */
esp_err_t peripherals_manager_init_tf_card(void)
{
    if (s_sd_ready && s_sd_card != NULL) {
        return ESP_OK;
    }

    sd_card_init();
    if (!s_sd_ready || s_sd_card == NULL) {
        return ESP_FAIL;
    }

    return sd_card_verify_file_io();
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
 * periodic service task that exercises integrated peripheral paths.
 */
esp_err_t peripherals_manager_start(void)
{
    i2c_scan();
    io_self_test();
    peripherals_manager_init_rtc_now();
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
