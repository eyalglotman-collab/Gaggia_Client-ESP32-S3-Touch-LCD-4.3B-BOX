/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <assert.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "lvgl_port.h"

static const char *TAG = "lv_port";
static SemaphoreHandle_t lvgl_mux = NULL;
static TaskHandle_t lvgl_task_handle = NULL;

/**
 * @brief Flush callback function for LVGL (LVGL 9.x)
 */
static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
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
    }

    lv_display_flush_ready(disp);
}

/**
 * @brief Initialize display for LVGL (LVGL 9.x)
 */
static lv_display_t *display_init(esp_lcd_panel_handle_t panel_handle)
{
    assert(panel_handle);

    size_t pixel_size = sizeof(lv_color_t);
    size_t buffer_size = LVGL_PORT_H_RES * LVGL_PORT_BUFFER_HEIGHT * pixel_size;

    // Allocate display buffer
    void *buf1 = heap_caps_malloc(buffer_size,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(buf1);

    ESP_LOGI(TAG, "LVGL buffer size: %uKB", (unsigned)(buffer_size / 1024));

    // Create display object
    lv_display_t *disp = lv_display_create(LVGL_PORT_H_RES, LVGL_PORT_V_RES);
    assert(disp);
    lv_display_set_user_data(disp, panel_handle);

    // Set buffer
    lv_display_set_buffers(disp, buf1, NULL,
                          buffer_size,
                          LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Set flush callback
    lv_display_set_flush_cb(disp, flush_callback);

    return disp;
}

/**
 * @brief Read touchpad callback for LVGL (LVGL 9.x)
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
        ESP_LOGD(TAG, "Touch: x=%d, y=%d", touchpad_x, touchpad_y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/**
 * @brief Initialize touchpad for LVGL (LVGL 9.x)
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
 * @brief Increment LVGL tick counter
 */
static void tick_increment(void *arg)
{
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

/**
 * @brief Initialize LVGL tick
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
 * @brief LVGL task
 */
static void lvgl_port_task(void *arg)
{
    ESP_LOGD(TAG, "Starting LVGL task");

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
 * @brief Initialize LVGL port
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

    ESP_LOGI(TAG, "Create LVGL task");
    BaseType_t core_id = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE;
    BaseType_t ret = xTaskCreatePinnedToCore(lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, NULL,
                                             LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core_id);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Lock LVGL mutex
 */
bool lvgl_port_lock(int timeout_ms)
{
    assert(lvgl_mux);
    const TickType_t timeout_ticks = (timeout_ms <= 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

/**
 * @brief Unlock LVGL mutex
 */
void lvgl_port_unlock(void)
{
    assert(lvgl_mux);
    xSemaphoreGiveRecursive(lvgl_mux);
}

