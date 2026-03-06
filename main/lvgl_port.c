/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "lvgl_port.h"

static const char *TAG = "lv_port";
static SemaphoreHandle_t lvgl_mux = NULL;
static TaskHandle_t lvgl_task_handle = NULL;

/**
 * @brief Flush LVGL render area to RGB panel.
 *
 * @details Sends one rendered area from LVGL to the ESP LCD panel driver and
 * notifies LVGL flush completion immediately after draw submission.
 *
 * @param[in] disp LVGL display handle.
 * @param[in] area Area to flush.
 * @param[in] px_map Rendered pixel buffer.
 */
static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    static bool s_logged_first_flush = false;
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    assert(panel_handle);

    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_handle,
                                              area->x1,
                                              area->y1,
                                              area->x2 + 1,
                                              area->y2 + 1,
                                              px_map);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Flush failed: %s", esp_err_to_name(ret));
    } else if (!s_logged_first_flush) {
        ESP_LOGI(TAG, "First LVGL flush submitted");
        s_logged_first_flush = true;
    }

    lv_display_flush_ready(disp);
}

/**
 * @brief Create LVGL display and attach render buffer.
 *
 * @details Allocates a partial render buffer in PSRAM and registers the panel
 * flush callback for LVGL9 display pipeline.
 *
 * @param[in] panel_handle Initialized ESP LCD panel handle.
 *
 * @return Created LVGL display object.
 */
static lv_display_t *display_init(esp_lcd_panel_handle_t panel_handle)
{
    assert(panel_handle);

    const size_t pixel_size = sizeof(lv_color_t);
    const size_t frame_pixels = LVGL_PORT_H_RES * LVGL_PORT_V_RES;
    const size_t buffer_size = frame_pixels * pixel_size;

    /* Full-frame double buffering is more robust on ESP RGB panels than partial buffers. */
    void *buf1 = heap_caps_malloc(buffer_size, LVGL_PORT_BUFFER_MALLOC_CAPS | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(buffer_size, LVGL_PORT_BUFFER_MALLOC_CAPS | MALLOC_CAP_8BIT);
    assert(buf1);
    assert(buf2);

    ESP_LOGI(TAG, "LVGL framebuffer size: %uKB x2", (unsigned)(buffer_size / 1024));

    lv_display_t *disp = lv_display_create(LVGL_PORT_H_RES, LVGL_PORT_V_RES);
    assert(disp);
    lv_display_set_user_data(disp, panel_handle);

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf1, buf2, buffer_size, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_callback);

    return disp;
}

/**
 * @brief Read touch state from GT911 into LVGL.
 *
 * @details Polls ESP LCD touch driver and maps one coordinate sample to LVGL
 * pointer input state.
 *
 * @param[in] indev LVGL input device handle.
 * @param[out] data Output LVGL input sample.
 */
static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    assert(tp);

    uint16_t touchpad_x = 0;
    uint16_t touchpad_y = 0;
    uint8_t touchpad_cnt = 0;

    esp_lcd_touch_read_data(tp);
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(tp, &touchpad_x, &touchpad_y, NULL, &touchpad_cnt, 1);

    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x;
        data->point.y = touchpad_y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/**
 * @brief Register LVGL pointer device for touch controller.
 *
 * @details Creates one LVGL indev object and stores ESP touch handle as user
 * data used by the periodic read callback.
 *
 * @param[in] tp ESP LCD touch handle.
 *
 * @return Created LVGL input device.
 */
static lv_indev_t *indev_init(esp_lcd_touch_handle_t tp)
{
    assert(tp);

    lv_indev_t *indev = lv_indev_create();
    assert(indev);
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
    lv_indev_set_user_data(indev, tp);
    return indev;
}

/**
 * @brief LVGL tick timer callback.
 *
 * @details Advances LVGL millisecond tick with fixed period.
 *
 * @param[in] arg Unused callback argument.
 */
static void tick_increment(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

/**
 * @brief Create periodic LVGL tick timer.
 *
 * @details Starts an ESP timer to feed LVGL runtime tick.
 *
 * @return ESP_OK on success.
 */
static esp_err_t tick_init(void)
{
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &tick_increment,
        .name = "LVGL tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    return esp_timer_start_periodic(lvgl_tick_timer, LVGL_PORT_TICK_PERIOD_MS * 1000);
}

/**
 * @brief LVGL worker task loop.
 *
 * @details Runs `lv_timer_handler()` under a recursive mutex and sleeps based
 * on LVGL requested delay clamped to configured bounds.
 *
 * @param[in] arg Unused task argument.
 */
static void lvgl_port_task(void *arg)
{
    (void)arg;

    uint32_t task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_port_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }

        if (task_delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

/**
 * @brief Initialize LVGL display/input/task port.
 *
 * @details Initializes LVGL runtime, tick timer, display driver, optional
 * touch input, synchronization mutex, and LVGL worker task.
 *
 * @param[in] lcd_handle Initialized ESP LCD panel handle.
 * @param[in] tp_handle Optional touch handle, may be NULL.
 *
 * @return ESP_OK on success, otherwise ESP_FAIL.
 */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle)
{
    lv_init();
    ESP_ERROR_CHECK(tick_init());

    lv_display_t *disp = display_init(lcd_handle);
    assert(disp);

    if (tp_handle) {
        lv_indev_t *indev = indev_init(tp_handle);
        assert(indev);
    }

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux);

    const BaseType_t core_id = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE;
    BaseType_t ret = xTaskCreatePinnedToCore(lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, NULL,
                                             LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core_id);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Lock LVGL recursive mutex.
 *
 * @details Blocks indefinitely for timeout values below zero.
 *
 * @param[in] timeout_ms Lock timeout in milliseconds.
 *
 * @return True when lock acquired.
 */
bool lvgl_port_lock(int timeout_ms)
{
    assert(lvgl_mux && "lvgl_port_init must be called first");
    const TickType_t timeout_ticks = (timeout_ms <= 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

/**
 * @brief Unlock LVGL recursive mutex.
 *
 * @details Releases mutex taken with `lvgl_port_lock()`.
 */
void lvgl_port_unlock(void)
{
    assert(lvgl_mux && "lvgl_port_init must be called first");
    xSemaphoreGiveRecursive(lvgl_mux);
}

/**
 * @brief Notify LVGL about RGB VSYNC event.
 *
 * @details Kept for compatibility with demo-style callback registration. In
 * this simplified LVGL9 port no explicit VSYNC synchronization is required.
 *
 * @return Always false.
 */
bool lvgl_port_notify_rgb_vsync(void)
{
    return false;
}
