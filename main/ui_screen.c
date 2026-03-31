/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "CommunicationFunctions.h"
#include "ProtocolLCD_Controller.h"
#include "data_payload.h"
#include "ui_screen.h"
#include "hardware_init.h"
#include "peripherals_manager.h"
#include "system_constants.h"
#include "assets/coffee_ready_img.h"

static const char *TAG = "ui_screen";

#define UI_PLOT_POINT_COUNT      (1200U)
#define UI_PLOT_WINDOW_POINT_COUNT (201U)
#define UI_PLOT_X_LABEL_COUNT    (21U)
#define UI_PLOT_Y_LABEL_COUNT    (6U)

typedef enum {
    UI_PAGE_BREW = 0,
    UI_PAGE_LIVE_SHOT,
    UI_PAGE_SETTINGS,
    UI_PAGE_COUNT
} ui_page_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *init_title_label;
    lv_obj_t *init_status_label;
    lv_obj_t *init_mode_prompt_label;
    lv_obj_t *init_mode_countdown_label;
    lv_obj_t *init_fail_label;
    lv_obj_t *init_mode_yes_btn;
    lv_obj_t *init_mode_no_btn;
    lv_obj_t *init_confirm_btn;
    lv_obj_t *error_screen;
    lv_obj_t *tabview;
    lv_obj_t *tab_pages[UI_PAGE_COUNT];
    lv_obj_t *connection_fault_label;
    lv_obj_t *page_status;
    lv_obj_t *page_runtime;
    lv_obj_t *clock_label;
    lv_obj_t *clock_set_overlay;
    lv_obj_t *connection_info_overlay;
    lv_obj_t *system_constants_overlay;
    lv_obj_t *sim_data_overlay;
    lv_obj_t *clock_set_day_roller;
    lv_obj_t *clock_set_month_roller;
    lv_obj_t *clock_set_year_roller;
    lv_obj_t *clock_set_hour_roller;
    lv_obj_t *clock_set_minute_roller;
    lv_obj_t *connection_info_details_label;
    lv_obj_t *connection_info_auto_reconnect_btn;
    lv_obj_t *sim_data_toggle_btn;
    lv_obj_t *sim_data_status_label;
    lv_obj_t *sim_data_chart;
    lv_chart_series_t *sim_data_series;
    int32_t sim_data_chart_y_values[DATA_SIZE_FLOATS];
    lv_obj_t *sim_data_x_axis_labels[6];
    lv_obj_t *sim_data_y_axis_labels[5];
    char sim_data_status_text[256];
    char sim_data_x_axis_label_text[6][24];
    char sim_data_y_axis_label_text[5][24];
    bool connection_info_show_scan_results;
    bool sim_data_toggle_syncing;
    lv_obj_t *plot_chart;
    lv_chart_series_t *plot_pressure_series;
    lv_chart_series_t *plot_weight_series;
    lv_chart_series_t *plot_flow_series;
    lv_chart_series_t *plot_temperature_series;
    int32_t plot_pressure_chart_y_values[UI_PLOT_POINT_COUNT];
    int32_t plot_weight_chart_y_values[UI_PLOT_POINT_COUNT];
    int32_t plot_flow_chart_y_values[UI_PLOT_POINT_COUNT];
    int32_t plot_temperature_chart_y_values[UI_PLOT_POINT_COUNT];
    lv_obj_t *plot_x_axis_labels[UI_PLOT_X_LABEL_COUNT];
    lv_obj_t *plot_pressure_y_axis_labels[UI_PLOT_Y_LABEL_COUNT];
    lv_obj_t *plot_weight_y_axis_labels[UI_PLOT_Y_LABEL_COUNT];
    lv_obj_t *plot_flow_y_axis_labels[UI_PLOT_Y_LABEL_COUNT];
    lv_obj_t *plot_temperature_y_axis_labels[UI_PLOT_Y_LABEL_COUNT];
    char plot_x_axis_label_text[UI_PLOT_X_LABEL_COUNT][24];
    char plot_pressure_y_axis_label_text[UI_PLOT_Y_LABEL_COUNT][24];
    char plot_weight_y_axis_label_text[UI_PLOT_Y_LABEL_COUNT][24];
    char plot_flow_y_axis_label_text[UI_PLOT_Y_LABEL_COUNT][24];
    char plot_temperature_y_axis_label_text[UI_PLOT_Y_LABEL_COUNT][24];
    bool plot_stream_started;
    lv_obj_t *plot_show_pressure_checkbox;
    lv_obj_t *plot_show_weight_checkbox;
    lv_obj_t *plot_show_flow_checkbox;
    lv_obj_t *plot_show_temperature_checkbox;
    lv_obj_t *plot_autoscale_pressure_checkbox;
    lv_obj_t *plot_autoscale_weight_checkbox;
    lv_obj_t *plot_autoscale_flow_checkbox;
    lv_obj_t *plot_autoscale_temperature_checkbox;
    bool plot_show_pressure;
    bool plot_show_weight;
    bool plot_show_flow;
    bool plot_show_temperature;
    bool plot_autoscale_pressure;
    bool plot_autoscale_weight;
    bool plot_autoscale_flow;
    bool plot_autoscale_temperature;
    lv_obj_t *plot_range_overlay;
    lv_obj_t *plot_range_pressure_value_label;
    lv_obj_t *plot_range_weight_value_label;
    lv_obj_t *plot_range_flow_value_label;
    lv_obj_t *plot_range_temperature_value_label;
    lv_obj_t *plot_range_pressure_roller;
    lv_obj_t *plot_range_weight_roller;
    lv_obj_t *plot_range_flow_roller;
    lv_obj_t *plot_range_temperature_roller;
    float plot_manual_pressure_max_bar;
    float plot_manual_weight_max_g;
    float plot_manual_flow_max_ml_s;
    float plot_manual_temperature_max_c;
    float plot_history_time_sec[UI_PLOT_POINT_COUNT];
    float plot_history_pressure_bar[UI_PLOT_POINT_COUNT];
    float plot_history_weight_g[UI_PLOT_POINT_COUNT];
    float plot_history_flow_ml_s[UI_PLOT_POINT_COUNT];
    float plot_history_temperature_c[UI_PLOT_POINT_COUNT];
    uint32_t plot_history_count;
    bool plot_history_frozen;
    lv_obj_t *content;
    lv_obj_t *brew_toggle_btn;
    lv_obj_t *steam_toggle_btn;
    lv_obj_t *brew_profile_dropdown;
    lv_obj_t *home_active_profile;
    lv_obj_t *home_target_label;
    lv_obj_t *brew_temperature_value_label;
    lv_obj_t *brew_water_level_label;
    lv_obj_t *brew_water_level_bar;
    lv_obj_t *brew_weight_value_label;
    lv_obj_t *brew_weight_scale_bar;
    lv_obj_t *brew_warmup_led;
    lv_obj_t *brew_warmup_label;
    lv_obj_t *brew_steam_led;
    lv_obj_t *brew_steam_label;
    lv_obj_t *brew_uptime_label;
    lv_obj_t *coffee_preparation_success_msgbox;
    lv_obj_t *coffee_preparation_success_ok_btn;
    lv_obj_t *settings_target_slider;
    lv_obj_t *settings_target_value;
    lv_obj_t *settings_preinf_slider;
    lv_obj_t *settings_preinf_value;
    lv_obj_t *settings_backlight_toggle;
    lv_timer_t *heartbeat_timer;
    lv_timer_t *sim_data_poll_timer;
    lv_timer_t *init_mode_countdown_timer;
    ui_page_t active_page;
    ui_init_mode_t init_mode_selection;
    ui_init_mode_t startup_mode;
    uint32_t init_mode_countdown_seconds;
    bool brewing;
    bool steaming;
    bool brew_profile_dropdown_syncing;
    int active_profile;
    int target_temp_c;
    int preinf_s;
    int shot_s;
    float brew_live_pressure_bar;
    float brew_live_temperature_c;
    float brew_live_water_level_pct;
    float brew_live_weight_g;
    float brew_shot_target_preview_g;
    bool brew_warmup_on;
    bool brew_steam_indicator_on;
    bool brew_server_shot_valid;
    bool brew_server_pressure_valid;
    bool brew_server_water_valid;
    bool brew_server_weight_valid;
    bool brew_server_warmup_valid;
    bool brew_server_temperature_valid;
    float brew_uptime_minutes;
    bool sim_data_enabled;
    uint32_t sim_data_packets_received;
    uint32_t sim_data_last_seq;
    bool sim_data_last_seq_valid;
    uint32_t sim_data_last_peer_event_count;
    data_downlink_packet_t sim_data_work_packet;
    int64_t sim_data_last_packet_rx_us;
    uint32_t sim_data_packet_interval_us;
    uint32_t sim_data_sample_period_us;
    uint16_t sim_data_rx_fifo_packet_backlog_npackets;
    bool sim_data_timing_valid;
    int32_t sim_data_last_y_axis_limit;
    uint32_t sim_data_last_axis_packet_interval_us;
    int64_t sim_data_last_axis_refresh_us;
    int64_t sim_data_last_seq_gap_log_us;
    int64_t sim_data_last_chart_refresh_us;
    int64_t sim_data_last_status_refresh_us;
    bool reinit_requested;
    bool init_failure_confirm_requested;
} ui_state_t;

static ui_state_t s_ui = {
    .root = NULL,
    .init_title_label = NULL,
    .init_status_label = NULL,
    .init_mode_prompt_label = NULL,
    .init_mode_countdown_label = NULL,
    .init_fail_label = NULL,
    .init_mode_yes_btn = NULL,
    .init_mode_no_btn = NULL,
    .init_confirm_btn = NULL,
    .error_screen = NULL,
    .tabview = NULL,
    .tab_pages = {NULL},
    .connection_fault_label = NULL,
    .page_status = NULL,
    .page_runtime = NULL,
    .clock_label = NULL,
    .clock_set_overlay = NULL,
    .connection_info_overlay = NULL,
    .system_constants_overlay = NULL,
    .sim_data_overlay = NULL,
    .clock_set_day_roller = NULL,
    .clock_set_month_roller = NULL,
    .clock_set_year_roller = NULL,
    .clock_set_hour_roller = NULL,
    .clock_set_minute_roller = NULL,
    .connection_info_details_label = NULL,
    .connection_info_auto_reconnect_btn = NULL,
    .sim_data_toggle_btn = NULL,
    .sim_data_status_label = NULL,
    .sim_data_chart = NULL,
    .sim_data_series = NULL,
    .sim_data_chart_y_values = {0},
    .sim_data_x_axis_labels = {NULL},
    .sim_data_y_axis_labels = {NULL},
    .sim_data_status_text = {0},
    .sim_data_x_axis_label_text = {{0}},
    .sim_data_y_axis_label_text = {{0}},
    .connection_info_show_scan_results = false,
    .sim_data_toggle_syncing = false,
    .plot_chart = NULL,
    .plot_pressure_series = NULL,
    .plot_weight_series = NULL,
    .plot_flow_series = NULL,
    .plot_temperature_series = NULL,
    .plot_pressure_chart_y_values = {0},
    .plot_weight_chart_y_values = {0},
    .plot_flow_chart_y_values = {0},
    .plot_temperature_chart_y_values = {0},
    .plot_x_axis_labels = {NULL},
    .plot_pressure_y_axis_labels = {NULL},
    .plot_weight_y_axis_labels = {NULL},
    .plot_flow_y_axis_labels = {NULL},
    .plot_temperature_y_axis_labels = {NULL},
    .plot_x_axis_label_text = {{0}},
    .plot_pressure_y_axis_label_text = {{0}},
    .plot_weight_y_axis_label_text = {{0}},
    .plot_flow_y_axis_label_text = {{0}},
    .plot_temperature_y_axis_label_text = {{0}},
    .plot_stream_started = false,
    .plot_show_pressure_checkbox = NULL,
    .plot_show_weight_checkbox = NULL,
    .plot_show_flow_checkbox = NULL,
    .plot_show_temperature_checkbox = NULL,
    .plot_autoscale_pressure_checkbox = NULL,
    .plot_autoscale_weight_checkbox = NULL,
    .plot_autoscale_flow_checkbox = NULL,
    .plot_autoscale_temperature_checkbox = NULL,
    .plot_show_pressure = true,
    .plot_show_weight = true,
    .plot_show_flow = true,
    .plot_show_temperature = true,
    .plot_autoscale_pressure = true,
    .plot_autoscale_weight = true,
    .plot_autoscale_flow = true,
    .plot_autoscale_temperature = true,
    .plot_range_overlay = NULL,
    .plot_range_pressure_value_label = NULL,
    .plot_range_weight_value_label = NULL,
    .plot_range_flow_value_label = NULL,
    .plot_range_temperature_value_label = NULL,
    .plot_range_pressure_roller = NULL,
    .plot_range_weight_roller = NULL,
    .plot_range_flow_roller = NULL,
    .plot_range_temperature_roller = NULL,
    .plot_manual_pressure_max_bar = 12.0f,
    .plot_manual_weight_max_g = 60.0f,
    .plot_manual_flow_max_ml_s = 5.0f,
    .plot_manual_temperature_max_c = 105.0f,
    .plot_history_time_sec = {0},
    .plot_history_pressure_bar = {0},
    .plot_history_weight_g = {0},
    .plot_history_flow_ml_s = {0},
    .plot_history_temperature_c = {0},
    .plot_history_count = 0U,
    .plot_history_frozen = false,
    .content = NULL,
    .brew_toggle_btn = NULL,
    .steam_toggle_btn = NULL,
    .brew_profile_dropdown = NULL,
    .home_active_profile = NULL,
    .home_target_label = NULL,
    .brew_temperature_value_label = NULL,
    .brew_water_level_label = NULL,
    .brew_water_level_bar = NULL,
    .brew_weight_value_label = NULL,
    .brew_weight_scale_bar = NULL,
    .brew_warmup_led = NULL,
    .brew_warmup_label = NULL,
    .brew_steam_led = NULL,
    .brew_steam_label = NULL,
    .brew_uptime_label = NULL,
    .coffee_preparation_success_msgbox = NULL,
    .coffee_preparation_success_ok_btn = NULL,
    .settings_target_slider = NULL,
    .settings_target_value = NULL,
    .settings_preinf_slider = NULL,
    .settings_preinf_value = NULL,
    .settings_backlight_toggle = NULL,
    .heartbeat_timer = NULL,
    .sim_data_poll_timer = NULL,
    .init_mode_countdown_timer = NULL,
    .active_page = UI_PAGE_BREW,
    .init_mode_selection = UI_INIT_MODE_NONE,
    .startup_mode = UI_INIT_MODE_NONE,
    .init_mode_countdown_seconds = 0,
    .brewing = false,
    .steaming = false,
    .brew_profile_dropdown_syncing = false,
    .active_profile = 1,
    .target_temp_c = 93,
    .preinf_s = 4,
    .shot_s = 0,
    .brew_live_pressure_bar = 0.2f,
    .brew_live_temperature_c = 93.0f,
    .brew_live_water_level_pct = 92.0f,
    .brew_live_weight_g = 0.0f,
    .brew_shot_target_preview_g = 36.0f,
    .brew_warmup_on = true,
    .brew_steam_indicator_on = false,
    .brew_server_shot_valid = false,
    .brew_server_pressure_valid = false,
    .brew_server_water_valid = false,
    .brew_server_weight_valid = false,
    .brew_server_warmup_valid = false,
    .brew_server_temperature_valid = false,
    .brew_uptime_minutes = 0.0f,
    .sim_data_enabled = false,
    .sim_data_packets_received = 0,
    .sim_data_last_seq = 0,
    .sim_data_last_seq_valid = false,
    .sim_data_last_peer_event_count = 0,
    .sim_data_work_packet = {0},
    .sim_data_last_packet_rx_us = 0,
    .sim_data_packet_interval_us = 0,
    .sim_data_sample_period_us = 0,
    .sim_data_rx_fifo_packet_backlog_npackets = 0U,
    .sim_data_timing_valid = false,
    .sim_data_last_y_axis_limit = 0,
    .sim_data_last_axis_packet_interval_us = 0U,
    .sim_data_last_axis_refresh_us = 0,
    .sim_data_last_seq_gap_log_us = 0,
    .sim_data_last_chart_refresh_us = 0,
    .sim_data_last_status_refresh_us = 0,
    .reinit_requested = false,
    .init_failure_confirm_requested = false,
};

#define UI_COLOR_BG            0x070B11
#define UI_COLOR_PANEL         0x111827
#define UI_COLOR_PANEL_ALT     0x0F172A
#define UI_COLOR_CARD          0x162033
#define UI_COLOR_CARD_ALT      0x1B263B
#define UI_COLOR_BORDER        0x243349
#define UI_COLOR_TEXT          0xE5EEF9
#define UI_COLOR_TEXT_MUTED    0x9FB3C8
#define UI_COLOR_ACCENT        0x38BDF8
#define UI_COLOR_ACCENT_ALT    0x0EA5E9
#define UI_COLOR_SUCCESS       0x22C55E
#define UI_CLOCK_SET_YEAR_START (2020)
#define UI_CLOCK_SET_YEAR_END   (2045)
#define UI_TABVIEW_HEIGHT      (432)
#define UI_CLOCK_BAR_HEIGHT    (56)
#define UI_SYSTEM_CONSTANTS_TEXT_MAX (4096)
#define UI_SIM_DATA_POLL_PERIOD_MS (20)
#define UI_SIM_DATA_CHART_REFRESH_PERIOD_MS (100U)
#define UI_SIM_DATA_STATUS_REFRESH_PERIOD_MS (250U)
#define UI_SIM_DATA_MAX_DRAIN_PER_TICK (8U)
#define UI_SIM_DATA_CHART_SCALE_FACTOR (100.0f)
#define UI_SIM_DATA_DEFAULT_PACKET_INTERVAL_US (1000000U)
#define UI_SIM_DATA_STREAM_IDLE_TIMEOUT_US (1500000U)
#define UI_SIM_DATA_X_LABEL_COUNT (6U)
#define UI_SIM_DATA_Y_LABEL_COUNT (5U)
#define UI_SIM_DATA_AXIS_REFRESH_PERIOD_US (500000LL)
#define UI_SIM_DATA_GAP_LOG_PERIOD_US (1000000LL)
#define UI_PLOT_WINDOW_SECONDS (20U)
#define UI_PLOT_X_LABEL_STEP_SECONDS (1.0f)
#define UI_PLOT_NORMALIZED_MAX (10000)
#define UI_PLOT_FLOAT_SCALE_FACTOR (100.0f)
#define UI_PLOT_SAMPLES_PER_CHANNEL (10U)
#define UI_PLOT_CHANNEL_OFFSET_PRESSURE (0U)
#define UI_PLOT_CHANNEL_OFFSET_FLOW (UI_PLOT_SAMPLES_PER_CHANNEL)
#define UI_PLOT_CHANNEL_OFFSET_TEMPERATURE (2U * UI_PLOT_SAMPLES_PER_CHANNEL)
#define UI_PLOT_CHANNEL_OFFSET_WEIGHT (3U * UI_PLOT_SAMPLES_PER_CHANNEL)
#define UI_BREW_VALID_SHOT_TIMER_MASK (1U << 0)
#define UI_BREW_VALID_LIVE_PRESSURE_MASK (1U << 1)
#define UI_BREW_VALID_WATER_LEVEL_MASK (1U << 2)
#define UI_BREW_VALID_WEIGHT_MASK (1U << 3)
#define UI_BREW_VALID_WARMUP_MASK (1U << 4)

typedef enum {
    UI_PLOT_CHECKBOX_AUTOSCALE_PRESSURE = 0,
    UI_PLOT_CHECKBOX_AUTOSCALE_WEIGHT,
    UI_PLOT_CHECKBOX_AUTOSCALE_FLOW,
    UI_PLOT_CHECKBOX_AUTOSCALE_TEMPERATURE
} ui_plot_checkbox_id_t;

typedef enum {
    UI_PLOT_RANGE_ROLLER_PRESSURE = 0,
    UI_PLOT_RANGE_ROLLER_WEIGHT,
    UI_PLOT_RANGE_ROLLER_FLOW,
    UI_PLOT_RANGE_ROLLER_TEMPERATURE
} ui_plot_range_roller_id_t;

static const system_constants_data_t *ui_get_constants(void);
static const system_constants_profile_t *ui_get_profile_constants(int profile_index);
static void ui_apply_profile_defaults(int profile_index);
static const char *ui_get_system_constants_pretty_text(void);
static void ui_update_connection_info_overlay_contents(void);
static void ui_update_sim_data_status_label(void);
static esp_err_t ui_request_sim_data_toggle(bool enabled, bool sync_toggle_button);
static bool ui_try_parse_sim_data_event_from_text(const char *payload_text, bool *out_enabled);
static void ui_sync_sim_data_toggle_from_peer_event(void);
static void ui_sync_sim_data_toggle_from_stream_activity(void);
static void ui_clear_sim_data_fifo(void);
static void ui_clear_sim_data_plot(void);
static void ui_reset_sim_data_stream_state(void);
static void ui_update_sim_data_axis_labels(uint32_t packet_interval_us, int32_t y_axis_limit);
static bool ui_sim_data_refresh_due(int64_t now_us, int64_t last_refresh_us, uint32_t period_ms);
static void ui_process_sim_data_fifo(void);
static void ui_plot_sim_data_packet(const data_downlink_packet_t *packet, uint32_t packet_interval_us);
static void ui_clear_plot_data(void);
static void ui_update_plot_axis_labels(float pressure_max,
                                       float weight_max,
                                       float flow_max,
                                       float temperature_max,
                                       float x_axis_span_s,
                                       bool rolling_window);
static void ui_render_live_shot_plot(void);
static void ui_plot_realtime_packet(const data_downlink_packet_t *packet, uint32_t packet_interval_us);
static void ui_close_plot_range_overlay(void);
static void ui_open_plot_range_overlay(void);
static void ui_plot_range_update_labels(void);
static void ui_plot_range_set_roller_options_tenths(lv_obj_t *roller,
                                                     int min_tenths,
                                                     int max_tenths,
                                                     int selected_tenths);
static void ui_plot_checkbox_event_cb(lv_event_t *e);
static void ui_plot_set_range_button_event_cb(lv_event_t *e);
static void ui_plot_range_roller_event_cb(lv_event_t *e);
static void ui_plot_range_done_event_cb(lv_event_t *e);
static void ui_ingest_home_metrics_from_packet(const data_downlink_packet_t *packet, uint32_t packet_interval_us);
static float ui_estimate_shot_target_preview_g(int profile_index, float brew_time_s);
static void ui_profile_dropdown_event_cb(lv_event_t *e);
static void ui_sync_brew_profile_dropdown(void);
static void ui_update_brew_widgets(void);
static void ui_send_profile_selection_command(bool offline_startup);
static esp_err_t ui_lcd_protocol_send_command_cb(const char *payload_text, void *user_ctx);
static void ui_lcd_protocol_profile_catalog_hook(const lcd_controller_profile_catalog_t *catalog,
                                                 void *user_ctx);
static void ui_lcd_protocol_brew_state_hook(const lcd_controller_brew_home_state_t *state,
                                            void *user_ctx);
static void ui_refresh_profile_dropdown_from_protocol(const lcd_controller_profile_catalog_t *catalog);
static void ui_close_sim_data_overlay(void);
static void ui_update_init_mode_prompt_text(void);
static lv_obj_t *ui_create_toggle_button(lv_obj_t *parent,
                                         const char *text,
                                         bool checked,
                                         lv_coord_t x,
                                         lv_coord_t y,
                                         lv_event_cb_t cb);

static char s_system_constants_pretty_text[UI_SYSTEM_CONSTANTS_TEXT_MAX];
static const uint32_t UI_INIT_MODE_DEFAULT_COUNTDOWN_SEC = 5U;

/**
 * @brief Handle the startup-mode `Yes` / `No` prompt selection.
 *
 * @details Stores the operator's requested initialization mode so the main task
 * can begin either offline or online startup sequencing.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_init_mode_select_event_cb(lv_event_t *e)
{
    intptr_t mode_value = (intptr_t)lv_event_get_user_data(e);
    if (mode_value == (intptr_t)UI_INIT_MODE_OFFLINE) {
        s_ui.init_mode_selection = UI_INIT_MODE_OFFLINE;
    } else {
        s_ui.init_mode_selection = UI_INIT_MODE_ONLINE;
    }

    if (s_ui.init_mode_countdown_timer != NULL) {
        lv_timer_del(s_ui.init_mode_countdown_timer);
        s_ui.init_mode_countdown_timer = NULL;
    }
}

/**
 * @brief Refresh the startup-mode prompt and countdown text.
 *
 * @details Keeps the operator-facing explanation aligned with the current
 * 5-second auto-select countdown while documenting that both choices still
 * keep server communication enabled for now.
 */
static void ui_update_init_mode_prompt_text(void)
{
    if (s_ui.init_mode_prompt_label != NULL) {
        lv_label_set_text_fmt(
            s_ui.init_mode_prompt_label,
            "Offline completes the full startup sequence and logs init failures as warnings.\n"
            "Online keeps the same full startup sequence but treats init failures as blocking errors.\n"
            "Both modes keep server communication enabled for now.\n"
            "Defaulting to Offline in %u second%s.",
            (unsigned)s_ui.init_mode_countdown_seconds,
            (s_ui.init_mode_countdown_seconds == 1U) ? "" : "s");
    }

    if (s_ui.init_mode_countdown_label != NULL) {
        lv_label_set_text_fmt(s_ui.init_mode_countdown_label,
                              "Automatic selection: Offline in %u",
                              (unsigned)s_ui.init_mode_countdown_seconds);
    }
}

/**
 * @brief Advance the startup-mode countdown timer.
 *
 * @details Updates the visible once-per-second countdown and auto-selects the
 * default Offline mode when the timeout expires without operator input.
 *
 * @param[in] timer LVGL timer payload.
 */
static void ui_init_mode_countdown_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_ui.init_mode_selection != UI_INIT_MODE_NONE) {
        return;
    }

    if (s_ui.init_mode_countdown_seconds > 0U) {
        s_ui.init_mode_countdown_seconds--;
    }

    if (s_ui.init_mode_countdown_seconds == 0U) {
        s_ui.init_mode_selection = UI_INIT_MODE_OFFLINE;
        if (s_ui.init_mode_countdown_timer != NULL) {
            lv_timer_del(s_ui.init_mode_countdown_timer);
            s_ui.init_mode_countdown_timer = NULL;
        }
        return;
    }

    ui_update_init_mode_prompt_text();
}

/**
 * @brief Handle initialization failure confirmation presses.
 *
 * @details Captures the operator acknowledgement so the application can switch
 * from the failed initialization view to the dedicated error screen.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_init_failure_confirm_event_cb(lv_event_t *e)
{
    (void)e;
    s_ui.init_failure_confirm_requested = true;
}

/**
 * @brief Apply shared dark card styling.
 *
 * @details Normalizes panel widgets to the selected dark UI palette so each
 * tab feels visually consistent.
 *
 * @param[in] obj LVGL object to style.
 * @param[in] bg_hex Background color in hex RGB form.
 */
static void ui_style_card(lv_obj_t *obj, uint32_t bg_hex)
{
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

/**
 * @brief Apply shared styling to action and toggle buttons.
 *
 * @details Gives both momentary buttons and checkable toggle buttons a
 * consistent button-like surface with pressed-state motion and checked-state
 * color changes.
 *
 * @param[in] btn LVGL button object.
 */
static void ui_style_action_button(lv_obj_t *btn)
{
    lv_obj_set_style_radius(btn, 18, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(UI_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_CARD_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_ACCENT_ALT), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x020617), LV_PART_MAIN);
    lv_obj_set_style_translate_y(btn, 2, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN);
}

/**
 * @brief Apply shared styling to button labels.
 *
 * @details Keeps labels readable across normal, pressed, and checked button
 * states.
 *
 * @param[in] label LVGL label object hosted inside a button.
 */
static void ui_style_button_label(lv_obj_t *label)
{
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
}

/**
 * @brief Apply shared dark slider styling.
 *
 * @details Uses the accent color for the active range while keeping the track
 * subdued against dark panels.
 *
 * @param[in] slider LVGL slider object.
 */
static void ui_style_slider(lv_obj_t *slider)
{
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_PANEL_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xF8FAFC), LV_PART_KNOB);
}

/**
 * @brief Update high-level status line in the header.
 *
 * @details Builds a single concise status text showing active mode and profile
 * to mimic dashboard-style espresso workflows.
 */
static void ui_update_header_status(void)
{
    if (!s_ui.page_status) {
        return;
    }

    char line[96];
    const char *mode = s_ui.brewing ? "BREWING" : (s_ui.steaming ? "STEAM" : "IDLE");
    snprintf(line, sizeof(line), "%s  |  Profile %d  |  Target %d C",
             mode, s_ui.active_profile, s_ui.target_temp_c);
    lv_label_set_text(s_ui.page_status, line);
}

/**
 * @brief Update live runtime text.
 *
 * @details Renders shot timer and a simple synthetic pressure/flow readout to
 * keep the home screen feeling active.
 */
static void ui_update_header_runtime(void)
{
    if (!s_ui.page_runtime) {
        return;
    }

    const system_constants_profile_t *profile_constants = ui_get_profile_constants(s_ui.active_profile);
    char shot_text[24];
    char pressure_text[32];
    char temp_text[64];
    char line[160];
    if (s_ui.brew_server_shot_valid) {
        snprintf(shot_text, sizeof(shot_text), "%02ds", s_ui.shot_s);
    } else {
        snprintf(shot_text, sizeof(shot_text), "N/A");
    }
    if (s_ui.brew_server_pressure_valid) {
        snprintf(pressure_text, sizeof(pressure_text), "%.1f Bar", s_ui.brew_live_pressure_bar);
    } else {
        snprintf(pressure_text, sizeof(pressure_text), "N/A");
    }
    if (s_ui.brew_server_temperature_valid) {
        snprintf(temp_text,
                 sizeof(temp_text),
                 "%.1f C (Target %.1f C)",
                 s_ui.brew_live_temperature_c,
                 (double)s_ui.target_temp_c);
    } else {
        snprintf(temp_text, sizeof(temp_text), "N/A (Target %.1f C)", (double)s_ui.target_temp_c);
    }

    if (s_ui.active_page == UI_PAGE_BREW) {
        snprintf(
            line,
            sizeof(line),
            "Shot %s  |  Pressure %s  |  Temperature %s",
            shot_text,
            pressure_text,
            temp_text);
    } else if (s_ui.active_page == UI_PAGE_LIVE_SHOT) {
        snprintf(
            line,
            sizeof(line),
            "Profile: %s  |  Temperature %s",
            (profile_constants != NULL) ? profile_constants->name : "N/A",
            temp_text);
    } else {
        snprintf(
            line,
            sizeof(line),
            "Shot %s  |  Pressure %s",
            shot_text,
            pressure_text);
    }
    lv_label_set_text(s_ui.page_runtime, line);
}

/**
 * @brief Update the persistent bottom clock bar from the RTC.
 *
 * @details Reads the latest RTC time if available and formats a compact clock
 * string for the always-visible lower status bar.
 */
static void ui_update_clock_bar(void)
{
    if (!s_ui.clock_label) {
        return;
    }

    struct tm rtc_tm = {0};
    if (peripherals_manager_get_rtc_time(&rtc_tm) == ESP_OK) {
        static const char *wday_names[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        static const char *month_names[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        char txt[64];
        const char *wday = (rtc_tm.tm_wday >= 0 && rtc_tm.tm_wday < 7) ? wday_names[rtc_tm.tm_wday] : "---";
        const char *month = (rtc_tm.tm_mon >= 0 && rtc_tm.tm_mon < 12) ? month_names[rtc_tm.tm_mon] : "---";
        snprintf(txt,
                 sizeof(txt),
                 "%s, %s-%d, %04d %02d:%02d:%02d",
                 wday,
                 month,
                 rtc_tm.tm_mday,
                 rtc_tm.tm_year + 1900,
                 rtc_tm.tm_hour,
                 rtc_tm.tm_min,
                 rtc_tm.tm_sec);
        lv_label_set_text(s_ui.clock_label, txt);
    } else {
        lv_label_set_text(s_ui.clock_label, "--------, --- --, ---- --:--:--");
    }
}

/**
 * @brief Refresh home page detail labels if they exist.
 *
 * @details Updates active profile and target labels without forcing a full page
 * rebuild.
 */
static void ui_update_home_labels(void)
{
    if (s_ui.home_active_profile) {
        lv_label_set_text(s_ui.home_active_profile, "Profile:");
        if (s_ui.brew_profile_dropdown) {
            lv_obj_align_to(
                s_ui.brew_profile_dropdown,
                s_ui.home_active_profile,
                LV_ALIGN_OUT_RIGHT_MID,
                16,
                0);
        }
    }

    ui_sync_brew_profile_dropdown();
    ui_update_brew_widgets();
}

/**
 * @brief Estimate profile shot-target mass for preview text.
 *
 * @details Uses profile flow target and expected brew duration to present a
 * practical grams preview near the profile selection controls.
 */
static float ui_estimate_shot_target_preview_g(int profile_index, float brew_time_s)
{
    const system_constants_profile_t *profile = ui_get_profile_constants(profile_index);
    float flow_ml_sec = (float)profile->target_flow_tenths / 10.0f;
    float duration_s = (brew_time_s > 0.0f) ? brew_time_s : 30.0f;
    float estimate_g = flow_ml_sec * duration_s;

    if (estimate_g < 0.0f) {
        estimate_g = 0.0f;
    }
    if (estimate_g > 200.0f) {
        estimate_g = 200.0f;
    }

    return estimate_g;
}

/**
 * @brief Synchronize profile dropdown selection with active profile state.
 */
static void ui_sync_brew_profile_dropdown(void)
{
    lcd_controller_profile_catalog_t protocol_catalog = {0};
    if (s_ui.brew_profile_dropdown == NULL) {
        return;
    }

    uint16_t selected = lv_dropdown_get_selected(s_ui.brew_profile_dropdown);
    uint16_t target = 0U;
    bool target_found = false;

    if (lcd_controller_protocol_get_profile_catalog(&protocol_catalog) == ESP_OK &&
        protocol_catalog.count > 0U) {
        for (uint8_t index = 0U; index < protocol_catalog.count; index++) {
            if ((int)protocol_catalog.entries[index].profile_id == s_ui.active_profile) {
                target = (uint16_t)index;
                target_found = true;
                break;
            }
        }
        if (!target_found) {
            target = 0U;
        }
    } else {
        target = (s_ui.active_profile > 0) ? (uint16_t)(s_ui.active_profile - 1) : 0U;
    }

    if (selected == target) {
        return;
    }

    s_ui.brew_profile_dropdown_syncing = true;
    lv_dropdown_set_selected(s_ui.brew_profile_dropdown, target);
    s_ui.brew_profile_dropdown_syncing = false;
}

/**
 * @brief Forward one protocol command through the communication transport.
 *
 * @param[in] payload_text Protocol command text.
 * @param[in] user_ctx Unused hook context.
 *
 * @return Command queue result from `communication_functions_queue_data_text_command`.
 */
static esp_err_t ui_lcd_protocol_send_command_cb(const char *payload_text, void *user_ctx)
{
    (void)user_ctx;
    return communication_functions_queue_data_text_command(payload_text);
}

/**
 * @brief Refresh Brew dropdown options from protocol-provided profile catalog.
 *
 * @param[in] catalog Parsed profile catalog snapshot.
 */
static void ui_refresh_profile_dropdown_from_protocol(const lcd_controller_profile_catalog_t *catalog)
{
    char dropdown_options[320] = {0};
    size_t used = 0U;
    uint16_t selected_index = 0U;
    bool selected_found = false;

    if (catalog == NULL || s_ui.brew_profile_dropdown == NULL || catalog->count == 0U) {
        return;
    }

    for (uint8_t index = 0U; index < catalog->count && index < LCD_CONTROLLER_MAX_PROFILES; index++) {
        const char *name = catalog->entries[index].profile_name;
        int written = 0;

        if (name[0] == '\0') {
            continue;
        }

        written = snprintf(dropdown_options + used,
                           sizeof(dropdown_options) - used,
                           "%s%s",
                           name,
                           (index + 1U < catalog->count) ? "\n" : "");
        if (written < 0) {
            break;
        }

        used += (size_t)written;
        if (used >= sizeof(dropdown_options)) {
            used = sizeof(dropdown_options) - 1U;
            break;
        }

        if ((int)catalog->entries[index].profile_id == s_ui.active_profile) {
            selected_index = (uint16_t)index;
            selected_found = true;
        }
    }

    if (used == 0U) {
        return;
    }

    s_ui.brew_profile_dropdown_syncing = true;
    lv_dropdown_set_options(s_ui.brew_profile_dropdown, dropdown_options);

    if (!selected_found && catalog->entries[0].profile_id > 0U) {
        selected_index = 0U;
        s_ui.active_profile = (int)catalog->entries[0].profile_id;
        if (s_ui.active_profile >= 1 && s_ui.active_profile <= ui_get_constants()->profile_count) {
            ui_apply_profile_defaults(s_ui.active_profile);
        }
    }

    lv_dropdown_set_selected(s_ui.brew_profile_dropdown, selected_index);
    s_ui.brew_profile_dropdown_syncing = false;

    ui_update_home_labels();
}

/**
 * @brief Apply simulator-provided profile catalog updates to Brew controls.
 *
 * @param[in] catalog Parsed profile catalog snapshot.
 * @param[in] user_ctx Unused hook context.
 */
static void ui_lcd_protocol_profile_catalog_hook(const lcd_controller_profile_catalog_t *catalog,
                                                 void *user_ctx)
{
    (void)user_ctx;
    ui_refresh_profile_dropdown_from_protocol(catalog);
}

/**
 * @brief Consume Brew-state publish notifications from protocol layer.
 *
 * @details Reserved for future UI actions that react to protocol-side state
 * publish callbacks.
 *
 * @param[in] state Brew/Home state snapshot.
 * @param[in] user_ctx Unused hook context.
 */
static void ui_lcd_protocol_brew_state_hook(const lcd_controller_brew_home_state_t *state,
                                            void *user_ctx)
{
    (void)state;
    (void)user_ctx;
}

static void ui_hide_coffee_preparation_success_msgbox(void)
{
    if (s_ui.coffee_preparation_success_msgbox != NULL) {
        lv_obj_del(s_ui.coffee_preparation_success_msgbox);
        s_ui.coffee_preparation_success_msgbox = NULL;
        s_ui.coffee_preparation_success_ok_btn = NULL;
    }
}

static void ui_coffee_preparation_success_ok_event_cb(lv_event_t *e)
{
    (void)e;
    ui_hide_coffee_preparation_success_msgbox();
}

/* Coffee_preparation_success_msgbox */
static void ui_show_coffee_preparation_success_msgbox(void)
{
    lv_obj_t *title = NULL;
    lv_obj_t *img = NULL;
    lv_obj_t *ok_label = NULL;

    if (s_ui.root == NULL || s_ui.active_page != UI_PAGE_BREW) {
        return;
    }

    if (s_ui.coffee_preparation_success_msgbox != NULL) {
        lv_obj_move_foreground(s_ui.coffee_preparation_success_msgbox);
        lv_obj_clear_flag(s_ui.coffee_preparation_success_msgbox, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    s_ui.coffee_preparation_success_msgbox = lv_obj_create(s_ui.root);
    lv_obj_set_size(s_ui.coffee_preparation_success_msgbox, 700, 350);
    lv_obj_align(s_ui.coffee_preparation_success_msgbox, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_style_radius(s_ui.coffee_preparation_success_msgbox, 20, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.coffee_preparation_success_msgbox, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.coffee_preparation_success_msgbox, lv_color_hex(0x93C5FD), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.coffee_preparation_success_msgbox, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.coffee_preparation_success_msgbox, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_ui.coffee_preparation_success_msgbox, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_ui.coffee_preparation_success_msgbox, lv_color_hex(0x020617), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.coffee_preparation_success_msgbox, 14, LV_PART_MAIN);
    lv_obj_clear_flag(s_ui.coffee_preparation_success_msgbox, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(s_ui.coffee_preparation_success_msgbox);
    lv_label_set_text(title, "Coffee Ready!");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    img = lv_image_create(s_ui.coffee_preparation_success_msgbox);
    lv_image_set_src(img, &coffee_ready_espresso_img);
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 64);

    s_ui.coffee_preparation_success_ok_btn = lv_btn_create(s_ui.coffee_preparation_success_msgbox);
    lv_obj_set_size(s_ui.coffee_preparation_success_ok_btn, 130, 52);
    lv_obj_align(s_ui.coffee_preparation_success_ok_btn, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_radius(s_ui.coffee_preparation_success_ok_btn, 14, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.coffee_preparation_success_ok_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.coffee_preparation_success_ok_btn, lv_color_hex(UI_COLOR_ACCENT_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.coffee_preparation_success_ok_btn, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);

    ok_label = lv_label_create(s_ui.coffee_preparation_success_ok_btn);
    lv_label_set_text(ok_label, "OK");
    lv_obj_set_style_text_font(ok_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ok_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(ok_label);

    lv_obj_add_event_cb(
        s_ui.coffee_preparation_success_ok_btn,
        ui_coffee_preparation_success_ok_event_cb,
        LV_EVENT_CLICKED,
        NULL);

    lv_obj_move_foreground(s_ui.coffee_preparation_success_msgbox);
}

/**
 * @brief Refresh Brew page widgets when they are currently rendered.
 */
static void ui_update_brew_widgets(void)
{
    bool shot_progress_valid = false;
    float shot_progress_pct = 0.0f;
    int shot_target_x10 = 0;
    int shot_weight_x10 = 0;

    if (s_ui.brew_shot_target_preview_g > 0.0f) {
        shot_target_x10 = (int)lroundf(s_ui.brew_shot_target_preview_g * 10.0f);
        if (shot_target_x10 < 1) {
            shot_target_x10 = 1;
        }
        if (shot_target_x10 > 30000) {
            shot_target_x10 = 30000;
        }
    }

    if (s_ui.brew_server_weight_valid && shot_target_x10 > 0) {
        shot_weight_x10 = (int)lroundf(s_ui.brew_live_weight_g * 10.0f);
        if (shot_weight_x10 < 0) {
            shot_weight_x10 = 0;
        }
        if (shot_weight_x10 > shot_target_x10) {
            shot_weight_x10 = shot_target_x10;
        }
        shot_progress_valid = true;
        shot_progress_pct = ((float)shot_weight_x10 / (float)shot_target_x10) * 100.0f;
        if (shot_progress_pct < 0.0f) {
            shot_progress_pct = 0.0f;
        }
        if (shot_progress_pct > 100.0f) {
            shot_progress_pct = 100.0f;
        }
    }

    if (s_ui.brew_temperature_value_label) {
        char txt[64];
        if (s_ui.brew_server_temperature_valid) {
            snprintf(
                txt,
                sizeof(txt),
                "%.1f C (Target %d C)",
                s_ui.brew_live_temperature_c,
                s_ui.target_temp_c);
        } else {
            snprintf(txt, sizeof(txt), "N/A (Target %d C)", s_ui.target_temp_c);
        }
        lv_label_set_text(s_ui.brew_temperature_value_label, txt);
    }

    if (s_ui.brew_water_level_label) {
        char txt[64];
        if (s_ui.brew_server_water_valid) {
            snprintf(txt, sizeof(txt), "Water Level: %.1f%% (0-100%%)", s_ui.brew_live_water_level_pct);
        } else {
            snprintf(txt, sizeof(txt), "Water Level: N/A (0-100%%)");
        }
        lv_label_set_text(s_ui.brew_water_level_label, txt);
    }

    if (s_ui.brew_water_level_bar) {
        int water_x10 = 0;
        if (s_ui.brew_server_water_valid) {
            water_x10 = (int)lroundf(s_ui.brew_live_water_level_pct * 10.0f);
            if (water_x10 < 0) {
                water_x10 = 0;
            }
            if (water_x10 > 1000) {
                water_x10 = 1000;
            }
        }
        lv_bar_set_range(s_ui.brew_water_level_bar, 0, 1000);
        lv_bar_set_value(s_ui.brew_water_level_bar, water_x10, LV_ANIM_OFF);
    }

    if (s_ui.brew_weight_value_label) {
        char txt[96];
        if (shot_progress_valid) {
            snprintf(
                txt,
                sizeof(txt),
                "Shot Progress: %.1f g (%.1f%%)  |  Shot Target %.1f g",
                s_ui.brew_live_weight_g,
                shot_progress_pct,
                s_ui.brew_shot_target_preview_g);
        } else if (shot_target_x10 > 0) {
            snprintf(
                txt,
                sizeof(txt),
                "Shot Progress: N/A  |  Shot Target %.1f g",
                s_ui.brew_shot_target_preview_g);
        } else {
            snprintf(txt, sizeof(txt), "Shot Progress: N/A  |  Shot Target N/A");
        }
        lv_label_set_text(s_ui.brew_weight_value_label, txt);
    }

    if (s_ui.brew_weight_scale_bar) {
        if (shot_target_x10 > 0) {
            lv_bar_set_range(s_ui.brew_weight_scale_bar, 0, shot_target_x10);
            lv_bar_set_value(
                s_ui.brew_weight_scale_bar,
                shot_progress_valid ? shot_weight_x10 : 0,
                LV_ANIM_OFF);
        } else {
            lv_bar_set_range(s_ui.brew_weight_scale_bar, 0, 1000);
            lv_bar_set_value(s_ui.brew_weight_scale_bar, 0, LV_ANIM_OFF);
        }
    }

    if (s_ui.brew_warmup_led) {
        if (s_ui.brew_server_warmup_valid && s_ui.brew_warmup_on) {
            lv_led_on(s_ui.brew_warmup_led);
        } else {
            lv_led_off(s_ui.brew_warmup_led);
        }
    }

    if (s_ui.brew_warmup_label) {
        if (!s_ui.brew_server_warmup_valid) {
            lv_label_set_text(s_ui.brew_warmup_label, "Warmup N/A");
        } else {
            lv_label_set_text(s_ui.brew_warmup_label, s_ui.brew_warmup_on ? "Warmup ON" : "Warmup OFF");
        }
    }

    if (s_ui.brew_steam_led) {
        bool steam_on = s_ui.brew_steam_indicator_on || s_ui.steaming;
        if (steam_on) {
            lv_led_on(s_ui.brew_steam_led);
        } else {
            lv_led_off(s_ui.brew_steam_led);
        }
    }

    if (s_ui.brew_steam_label) {
        bool steam_on = s_ui.brew_steam_indicator_on || s_ui.steaming;
        lv_label_set_text(s_ui.brew_steam_label, steam_on ? "Steam ON" : "Steam OFF");
    }

    if (s_ui.brew_uptime_label) {
        char txt[64];
        snprintf(txt, sizeof(txt), "LCD Uptime: %.1f min", s_ui.brew_uptime_minutes);
        lv_label_set_text(s_ui.brew_uptime_label, txt);
    }
}

/**
 * @brief Update top tab visual selection state.
 *
 * @details Clears checked state from all nav buttons and marks the active page
 * button as selected.
 */
static void ui_update_tab_style(void)
{
    if (!s_ui.tabview) {
        return;
    }

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        lv_obj_t *btn = lv_tabview_get_tab_button(s_ui.tabview, i);
        if (!btn) {
            continue;
        }
        lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x020617), LV_PART_MAIN);
        lv_obj_set_style_translate_y(btn, 2, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_ACCENT_ALT), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(btn, lv_color_hex(UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    }
}

/**
 * @brief Return the currently loaded system constants snapshot.
 *
 * @details Provides a short local wrapper so UI code can consume the loaded
 * constants database without repeating the accessor call everywhere.
 *
 * @return Pointer to the active constants snapshot.
 */
static const system_constants_data_t *ui_get_constants(void)
{
    return system_constants_get();
}

/**
 * @brief Resolve a one-based profile selection to loaded constants data.
 *
 * @details Maps the UI's profile numbering onto the loaded constants array and
 * clamps invalid indices to the nearest valid profile entry.
 *
 * @param[in] profile_index One-based profile index.
 *
 * @return Pointer to the resolved profile entry.
 */
static const system_constants_profile_t *ui_get_profile_constants(int profile_index)
{
    const system_constants_data_t *constants = ui_get_constants();

    if (constants->profile_count <= 0) {
        return &constants->profiles[0];
    }

    if (profile_index < 1) {
        profile_index = 1;
    }
    if (profile_index > constants->profile_count) {
        profile_index = constants->profile_count;
    }

    return &constants->profiles[profile_index - 1];
}

/**
 * @brief Apply a loaded profile's default targets to the active UI state.
 *
 * @details Copies the selected profile's configured target temperature and
 * preinfusion time from the constants database into the editable UI fields.
 *
 * @param[in] profile_index One-based profile index to apply.
 */
static void ui_apply_profile_defaults(int profile_index)
{
    const system_constants_profile_t *profile = ui_get_profile_constants(profile_index);
    float brew_time_s = 30.0f;
    if (s_ui.sim_data_work_packet.i[3] > 0) {
        brew_time_s = (float)s_ui.sim_data_work_packet.i[3] / 1000.0f;
    }
    s_ui.active_profile = profile_index;
    s_ui.target_temp_c = profile->target_temperature_c;
    s_ui.preinf_s = profile->preinfusion_seconds;
    s_ui.brew_shot_target_preview_g = ui_estimate_shot_target_preview_g(profile_index, brew_time_s);
}

/**
 * @brief Populate a roller with a numeric range.
 *
 * @details Generates newline-separated numeric labels so LVGL rollers can show
 * simple day/month/year/hour/minute selections.
 *
 * @param[in] roller Destination LVGL roller object.
 * @param[in] start_value Inclusive first value.
 * @param[in] end_value Inclusive last value.
 * @param[in] width_digits Minimum digits to print for each option.
 */
static void ui_set_roller_numeric_options(lv_obj_t *roller, int start_value, int end_value, int width_digits)
{
    char options[1024] = {0};
    size_t used = 0;

    for (int value = start_value; value <= end_value; value++) {
        int written = snprintf(options + used,
                               sizeof(options) - used,
                               (value == start_value) ? "%0*d" : "\n%0*d",
                               width_digits,
                               value);
        if (written <= 0 || (size_t)written >= (sizeof(options) - used)) {
            break;
        }
        used += (size_t)written;
    }

    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
}

/**
 * @brief Create a labeled time-selection roller.
 *
 * @details Builds a compact label plus roller pair used by the clock-setting
 * overlay.
 *
 * @param[in] parent Overlay panel parent object.
 * @param[in] title Section label text.
 * @param[in] x Left offset within the panel.
 * @param[in] y Top offset within the panel.
 *
 * @return Created LVGL roller object.
 */
static lv_obj_t *ui_create_clock_roller(lv_obj_t *parent, const char *title, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);

    lv_obj_t *roller = lv_roller_create(parent);
    lv_obj_set_size(roller, 120, 124);
    lv_obj_align(roller, LV_ALIGN_TOP_LEFT, x, y + 28);
    lv_obj_set_style_bg_color(roller, lv_color_hex(UI_COLOR_CARD_ALT), LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller, lv_color_hex(UI_COLOR_ACCENT_ALT), LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, lv_color_hex(0xFFFFFF), LV_PART_SELECTED);
    lv_obj_set_style_border_color(roller, lv_color_hex(UI_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(roller, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(roller, 14, LV_PART_MAIN);
    return roller;
}

/**
 * @brief Close the clock-setting overlay.
 *
 * @details Deletes the temporary full-screen time-picker UI and clears the
 * stored widget pointers.
 */
static void ui_close_clock_overlay(void)
{
    if (s_ui.clock_set_overlay) {
        lv_obj_del(s_ui.clock_set_overlay);
    }

    s_ui.clock_set_overlay = NULL;
    s_ui.clock_set_day_roller = NULL;
    s_ui.clock_set_month_roller = NULL;
    s_ui.clock_set_year_roller = NULL;
    s_ui.clock_set_hour_roller = NULL;
    s_ui.clock_set_minute_roller = NULL;
}

/**
 * @brief Close the connection-info overlay.
 *
 * @details Deletes the temporary full-screen connection-information UI and
 * clears the stored overlay pointer.
 */
static void ui_close_connection_info_overlay(void)
{
    if (s_ui.connection_info_overlay) {
        lv_obj_del(s_ui.connection_info_overlay);
    }

    s_ui.connection_info_overlay = NULL;
    s_ui.connection_info_details_label = NULL;
    s_ui.connection_info_auto_reconnect_btn = NULL;
    s_ui.connection_info_show_scan_results = false;
}

/**
 * @brief Refresh the dynamic contents of the connection-info overlay.
 *
 * @details Re-reads both peripheral and low-level communication snapshots so
 * the operator can observe the transport state machine progress after a reset.
 */
static void ui_update_connection_info_overlay_contents(void)
{
    if (!s_ui.connection_info_details_label) {
        return;
    }

    peripherals_connection_info_t info = {0};
    communication_snapshot_t comm_snapshot = {0};
    esp_err_t info_ret = peripherals_manager_get_connection_info(&info);
    esp_err_t comm_ret = communication_functions_get_snapshot(&comm_snapshot);
    if (info_ret != ESP_OK) {
        ESP_LOGW(TAG, "Connection info read failed: %s", esp_err_to_name(info_ret));
        snprintf(info.ip_address, sizeof(info.ip_address), "Unavailable");
        snprintf(info.port_text, sizeof(info.port_text), "Unavailable");
        info.wifi_ready = false;
        info.rtc_ready = false;
        info.tf_ready = false;
        info.wifi_ap_count = 0;
        info.controller_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;
    }

    if (comm_ret != ESP_OK) {
        ESP_LOGW(TAG, "Communication snapshot read failed: %s", esp_err_to_name(comm_ret));
        snprintf(comm_snapshot.last_error, sizeof(comm_snapshot.last_error), "Snapshot unavailable");
        comm_snapshot.state = COMMUNICATION_STATE_TOP_LAYER_RESET;
        comm_snapshot.scan_state = COMMUNICATION_SCAN_STATE_ERROR;
        snprintf(comm_snapshot.scan_results, sizeof(comm_snapshot.scan_results), "Scan snapshot unavailable");
    }

    if (s_ui.connection_info_show_scan_results) {
        lv_label_set_text_fmt(s_ui.connection_info_details_label,
                              "Device Scan\n"
                              "Scan State: %s\n"
                              "Detected Devices: %u\n"
                              "Duration: %u ms\n"
                              "\n"
                              "%s",
                              communication_functions_scan_state_to_string(comm_snapshot.scan_state),
                              (unsigned)comm_snapshot.scan_device_count,
                              (unsigned)comm_snapshot.scan_duration_ms,
                              comm_snapshot.scan_results);
        return;
    }

    char pkt_loss_buf[16] = {0};
    uint32_t pkt_total = comm_snapshot.keepalive_rx_count + comm_snapshot.timeout_event_count;
    if (pkt_total > 0U) {
        uint32_t loss_x10 = (comm_snapshot.timeout_event_count * 1000U) / pkt_total;
        snprintf(pkt_loss_buf, sizeof(pkt_loss_buf), "%u.%u%%", (unsigned)(loss_x10 / 10U), (unsigned)(loss_x10 % 10U));
    } else {
        snprintf(pkt_loss_buf, sizeof(pkt_loss_buf), "N/A");
    }

    lv_label_set_text_fmt(s_ui.connection_info_details_label,
                          "RF Link Quality\n"
                          "RSSI: %ld dBm\n"
                          "Noise Floor: %d dBm (2.4 GHz est)\n"
                          "SNR Estimate: %+d dB\n"
                          "Channel: %u\n"
                          "Auth Mode: %u\n"
                          "BSSID: %s\n"
                          "\n"
                          "Keepalive Timing\n"
                          "KA Response Last: %ld ms\n"
                          "KA Response Max: %ld ms\n"
                          "KA Response Min: %ld ms\n"
                          "Jitter (max-min): %ld ms\n"
                          "\n"
                          "Traffic Counters\n"
                          "KA Received: %" PRIu32 "\n"
                          "KA Sent: %" PRIu32 "\n"
                          "DATA Frames RX: %" PRIu32 "\n"
                          "Timeout Events: %" PRIu32 "\n"
                          "Packet Loss Rate: %s\n"
                          "CRC Frame Errors: %" PRIu32 "\n"
                          "Sequence Errors: %" PRIu32 "\n"
                          "Bottom Layer Retries: %" PRIu32 "\n"
                          "Top Layer Failures: %" PRIu32 "\n"
                          "Connect Streak: %" PRIu32 "\n"
                          "Reset-to-Debug: %" PRIu32 " ms\n"
                          "\n"
                          "Session\n"
                          "Session Uptime: %" PRIu32 " ms\n"
                          "Session Active: %s\n"
                          "\n"
                          "Configuration\n"
                          "KA Period: %" PRIu32 " ms\n"
                          "KA Wait Window: %u ms\n"
                          "KA Empty Window Limit: %u\n"
                          "Bottom Layer Retry Limit: %" PRIu32 "\n"
                          "Top Layer Failure Limit: %" PRIu32 "\n"
                          "Wi-Fi Connect Timeout: %" PRIu32 " ms\n"
                          "TCP Connect Timeout: %" PRIu32 " ms\n"
                          "\n"
                          "IP: %s\n"
                          "Port: %s\n"
                          "\n"
                          "Telemetry\n"
                          "Wi-Fi Ready: %s\n"
                          "Visible APs: %u\n"
                          "RTC Ready: %s\n"
                          "TF Card Ready: %s\n"
                          "Controller Status: %s\n"
                          "\n"
                          "Low-Level Communication\n"
                          "State: %s\n"
                          "Default SSID: %s\n"
                          "Server: %s:%u\n"
                          "Local IP: %s\n"
                          "TCP Connected: %s\n"
                          "Initialize Passed: %s\n"
                          "Connect Passed: %s\n"
                          "Send Data Enabled: %s\n"
                          "Auto Reconnect: %s\n"
                          "Connection Fault: %s\n"
                          "Keepalive Failures: %" PRIu32 "\n"
                          "RSSI: %ld dBm\n"
                          "ServerLiveInteger: %" PRIu32 "\n"
                          "ClientLiveInteger: %" PRIu32 "\n"
                          "Sequence: %u\n"
                          "Last Received Text: %s\n"
                          "Scan State: %s\n"
                          "Last Error: %s",
                          /* RF Link Quality */
                          (long)comm_snapshot.wifi_rssi,
                          (int)comm_snapshot.wifi_noise_floor_dbm,
                          (int)comm_snapshot.wifi_snr_estimate_db,
                          (unsigned)comm_snapshot.wifi_channel,
                          (unsigned)comm_snapshot.wifi_authmode,
                          comm_snapshot.wifi_bssid_str,
                          /* Keepalive Timing */
                          (long)comm_snapshot.ka_response_time_last_ms,
                          (long)comm_snapshot.ka_response_time_max_ms,
                          (long)comm_snapshot.ka_response_time_min_ms,
                          (long)comm_snapshot.ka_jitter_ms,
                          /* Traffic Counters */
                          comm_snapshot.keepalive_rx_count,
                          comm_snapshot.keepalive_tx_count,
                          comm_snapshot.data_frames_rx_count,
                          comm_snapshot.timeout_event_count,
                          pkt_loss_buf,
                          comm_snapshot.bottom_layer_checksum_error_count,
                          comm_snapshot.bottom_layer_sequence_error_count,
                          comm_snapshot.bottom_layer_retry_count,
                          comm_snapshot.top_layer_failure_count,
                          comm_snapshot.top_layer_connect_streak,
                          comm_snapshot.reset_to_debug_elapsed_ms,
                          /* Session */
                          comm_snapshot.session_uptime_ms,
                          comm_snapshot.session_uptime_ms > 0U ? "Yes" : "No",
                          /* Configuration */
                          comm_snapshot.config.keep_alive_period_ms,
                          (unsigned)comm_snapshot.config.ka_wait_window_ms,
                          (unsigned)comm_snapshot.config.ka_empty_window_limit,
                          comm_snapshot.config.bottom_layer_retry_limit,
                          comm_snapshot.config.top_layer_failure_limit,
                          comm_snapshot.config.wifi_connect_timeout_ms,
                          comm_snapshot.config.tcp_connect_timeout_ms,
                          /* Existing data — unchanged */
                          info.ip_address,
                          info.port_text,
                          info.wifi_ready ? "Yes" : "No",
                          info.wifi_ap_count,
                          info.rtc_ready ? "Yes" : "No",
                          info.tf_ready ? "Yes" : "No",
                          peripherals_manager_controller_status_to_string(info.controller_status),
                          communication_functions_state_to_string(comm_snapshot.state),
                          comm_snapshot.config.wifi_ssid,
                          comm_snapshot.config.server_ip,
                          (unsigned)comm_snapshot.config.server_port,
                          comm_snapshot.local_ip,
                          comm_snapshot.tcp_connected ? "Yes" : "No",
                          comm_snapshot.initialize_passed ? "Yes" : "No",
                          comm_snapshot.connect_passed ? "Yes" : "No",
                          comm_snapshot.send_data_enabled ? "Yes" : "No",
                          comm_snapshot.auto_reconnect_enabled ? "On" : "Off",
                          comm_snapshot.connection_fault ? "Yes" : "No",
                          comm_snapshot.consecutive_keepalive_failures,
                          (long)comm_snapshot.wifi_rssi,
                          comm_snapshot.server_live_integer,
                          comm_snapshot.client_live_integer,
                          (unsigned)comm_snapshot.sequence,
                          comm_snapshot.last_received_text,
                          communication_functions_scan_state_to_string(comm_snapshot.scan_state),
                          comm_snapshot.last_error);
}

/**
 * @brief Refresh the latched connection-fault banner in the active page header.
 *
 * @details Reads the communication snapshot and shows a retry banner to the
 * right of the current page title whenever the low-level transport has latched
 * a connection fault after repeated keepalive failures.
 */
static void ui_update_connection_fault_indicator(void)
{
    if (!s_ui.connection_fault_label) {
        return;
    }

    communication_snapshot_t comm_snapshot = {0};
    if (communication_functions_get_snapshot(&comm_snapshot) != ESP_OK) {
        lv_obj_add_flag(s_ui.connection_fault_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (comm_snapshot.connection_fault &&
        comm_snapshot.state != COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_SERVER_RECEIVE &&
        comm_snapshot.state != COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_CLIENT_SEND) {
        lv_label_set_text(s_ui.connection_fault_label, "Connection Fault, Please Reset");
        lv_obj_clear_flag(s_ui.connection_fault_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.connection_fault_label, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Close the system-constants overlay.
 *
 * @details Deletes the temporary full-screen XML viewer UI and clears the
 * stored overlay pointer.
 */
static void ui_close_system_constants_overlay(void)
{
    if (s_ui.system_constants_overlay) {
        lv_obj_del(s_ui.system_constants_overlay);
    }

    s_ui.system_constants_overlay = NULL;
}

/**
 * @brief Close the Simulate Data overlay and clear widget handles.
 *
 * @details Deletes the dedicated simulation screen opened from Settings and
 * clears all related LVGL object pointers.
 */
static void ui_close_sim_data_overlay(void)
{
    if (s_ui.sim_data_overlay) {
        lv_obj_del(s_ui.sim_data_overlay);
    }

    s_ui.sim_data_overlay = NULL;
    s_ui.sim_data_toggle_btn = NULL;
    s_ui.sim_data_status_label = NULL;
    s_ui.sim_data_chart = NULL;
    s_ui.sim_data_series = NULL;
    for (size_t index = 0; index < UI_SIM_DATA_X_LABEL_COUNT; index++) {
        s_ui.sim_data_x_axis_labels[index] = NULL;
    }
    for (size_t index = 0; index < UI_SIM_DATA_Y_LABEL_COUNT; index++) {
        s_ui.sim_data_y_axis_labels[index] = NULL;
    }
}

/**
 * @brief Remove all queued simulator downlink packets from the FIFO.
 *
 * @details Used when simulation is stopped so stale packets are not rendered
 * after the next start.
 */
static void ui_clear_sim_data_fifo(void)
{
    while (data_downlink_pop(&s_ui.sim_data_work_packet) == DATA_FIFO_OK) {
        /* drain FIFO */
    }
}

/**
 * @brief Clear all visible points from the Simulate Data chart.
 *
 * @details Uses LV_CHART_POINT_NONE to remove previously drawn waveform
 * segments so the graph looks empty while simulation is stopped.
 */
static void ui_clear_sim_data_plot(void)
{
    if (s_ui.sim_data_chart == NULL || s_ui.sim_data_series == NULL) {
        return;
    }

    for (uint32_t index = 0; index < DATA_SIZE_FLOATS; index++) {
        s_ui.sim_data_chart_y_values[index] = LV_CHART_POINT_NONE;
    }
    lv_chart_refresh(s_ui.sim_data_chart);
}

/**
 * @brief Clamp one plot axis value to a positive finite range.
 */
static float ui_plot_clamp_positive(float value, float min_value, float max_value, float fallback)
{
    if (!isfinite(value)) {
        return fallback;
    }
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

/**
 * @brief Append one sample into Live Shot history buffers.
 */
static void ui_plot_append_history_sample(float time_s,
                                          float pressure_bar,
                                          float weight_g,
                                          float flow_ml_s,
                                          float temperature_c)
{
    if (s_ui.plot_history_count < UI_PLOT_POINT_COUNT) {
        uint32_t idx = s_ui.plot_history_count;
        s_ui.plot_history_time_sec[idx] = time_s;
        s_ui.plot_history_pressure_bar[idx] = pressure_bar;
        s_ui.plot_history_weight_g[idx] = weight_g;
        s_ui.plot_history_flow_ml_s[idx] = flow_ml_s;
        s_ui.plot_history_temperature_c[idx] = temperature_c;
        s_ui.plot_history_count++;
        return;
    }

    memmove(&s_ui.plot_history_time_sec[0],
            &s_ui.plot_history_time_sec[1],
            (UI_PLOT_POINT_COUNT - 1U) * sizeof(s_ui.plot_history_time_sec[0]));
    memmove(&s_ui.plot_history_pressure_bar[0],
            &s_ui.plot_history_pressure_bar[1],
            (UI_PLOT_POINT_COUNT - 1U) * sizeof(s_ui.plot_history_pressure_bar[0]));
    memmove(&s_ui.plot_history_weight_g[0],
            &s_ui.plot_history_weight_g[1],
            (UI_PLOT_POINT_COUNT - 1U) * sizeof(s_ui.plot_history_weight_g[0]));
    memmove(&s_ui.plot_history_flow_ml_s[0],
            &s_ui.plot_history_flow_ml_s[1],
            (UI_PLOT_POINT_COUNT - 1U) * sizeof(s_ui.plot_history_flow_ml_s[0]));
    memmove(&s_ui.plot_history_temperature_c[0],
            &s_ui.plot_history_temperature_c[1],
            (UI_PLOT_POINT_COUNT - 1U) * sizeof(s_ui.plot_history_temperature_c[0]));

    s_ui.plot_history_time_sec[UI_PLOT_POINT_COUNT - 1U] = time_s;
    s_ui.plot_history_pressure_bar[UI_PLOT_POINT_COUNT - 1U] = pressure_bar;
    s_ui.plot_history_weight_g[UI_PLOT_POINT_COUNT - 1U] = weight_g;
    s_ui.plot_history_flow_ml_s[UI_PLOT_POINT_COUNT - 1U] = flow_ml_s;
    s_ui.plot_history_temperature_c[UI_PLOT_POINT_COUNT - 1U] = temperature_c;
}

/**
 * @brief Clear the Live Shot plot buffers and session history.
 *
 * @details Resets all four plot traces so a new StartBrew session starts from
 * an empty timeline.
 */
static void ui_clear_plot_data(void)
{
    for (uint32_t index = 0; index < UI_PLOT_POINT_COUNT; index++) {
        s_ui.plot_pressure_chart_y_values[index] = LV_CHART_POINT_NONE;
        s_ui.plot_weight_chart_y_values[index] = LV_CHART_POINT_NONE;
        s_ui.plot_flow_chart_y_values[index] = LV_CHART_POINT_NONE;
        s_ui.plot_temperature_chart_y_values[index] = LV_CHART_POINT_NONE;
        s_ui.plot_history_time_sec[index] = 0.0f;
        s_ui.plot_history_pressure_bar[index] = 0.0f;
        s_ui.plot_history_weight_g[index] = 0.0f;
        s_ui.plot_history_flow_ml_s[index] = 0.0f;
        s_ui.plot_history_temperature_c[index] = 0.0f;
    }
    s_ui.plot_history_count = 0U;
    s_ui.plot_history_frozen = false;
    s_ui.plot_stream_started = false;
    if (s_ui.plot_chart != NULL) {
        lv_chart_set_point_count(s_ui.plot_chart, UI_PLOT_WINDOW_POINT_COUNT);
        lv_chart_refresh(s_ui.plot_chart);
    }
    ui_update_plot_axis_labels(
        s_ui.plot_manual_pressure_max_bar,
        s_ui.plot_manual_weight_max_g,
        s_ui.plot_manual_flow_max_ml_s,
        s_ui.plot_manual_temperature_max_c,
        (float)UI_PLOT_WINDOW_SECONDS,
        true);
}

/**
 * @brief Update Live Shot custom X and right-side Y labels.
 *
 * @details Renders a fixed 20-second axis while brewing and a full-shot axis
 * when playback is frozen after brew completion.
 */
static void ui_update_plot_axis_labels(float pressure_max,
                                       float weight_max,
                                       float flow_max,
                                       float temperature_max,
                                       float x_axis_span_s,
                                       bool rolling_window)
{
    lv_coord_t chart_x = 0;
    lv_coord_t chart_y = 0;
    lv_coord_t chart_w = 0;
    lv_coord_t chart_h = 0;
    const lv_coord_t y_axis_pressure_x_offset = 18;
    const lv_coord_t y_axis_weight_x_offset = 44;
    const lv_coord_t y_axis_flow_x_offset = 70;
    const lv_coord_t y_axis_temperature_x_offset = 96;

    if (s_ui.plot_chart == NULL) {
        return;
    }

    chart_x = lv_obj_get_x(s_ui.plot_chart);
    chart_y = lv_obj_get_y(s_ui.plot_chart);
    chart_w = lv_obj_get_width(s_ui.plot_chart);
    chart_h = lv_obj_get_height(s_ui.plot_chart);

    for (uint32_t index = 0; index < UI_PLOT_X_LABEL_COUNT; index++) {
        lv_obj_t *label = s_ui.plot_x_axis_labels[index];
        float t_norm = (float)index / (float)(UI_PLOT_X_LABEL_COUNT - 1U);
        float seconds_value = x_axis_span_s * t_norm;
        lv_coord_t pos_x = chart_x + (lv_coord_t)(((chart_w - 1) * (int32_t)index) / (int32_t)(UI_PLOT_X_LABEL_COUNT - 1U));
        if (label == NULL) {
            continue;
        }
        (void)rolling_window;
        snprintf(s_ui.plot_x_axis_label_text[index],
                 sizeof(s_ui.plot_x_axis_label_text[index]),
                 "%.0fs",
                 (double)seconds_value);
        lv_label_set_text_static(label, s_ui.plot_x_axis_label_text[index]);
        lv_obj_update_layout(label);
        pos_x -= lv_obj_get_width(label) / 2;
        lv_obj_set_pos(label, pos_x, chart_y + chart_h - lv_obj_get_height(label) - 3);
    }

    for (uint32_t index = 0; index < UI_PLOT_Y_LABEL_COUNT; index++) {
        lv_coord_t pos_y = chart_y + (lv_coord_t)(((chart_h - 1) * (int32_t)index) / (int32_t)(UI_PLOT_Y_LABEL_COUNT - 1U));
        float normalized = 1.0f - ((float)index / (float)(UI_PLOT_Y_LABEL_COUNT - 1U));
        lv_obj_t *pressure_label = s_ui.plot_pressure_y_axis_labels[index];
        lv_obj_t *weight_label = s_ui.plot_weight_y_axis_labels[index];
        lv_obj_t *flow_label = s_ui.plot_flow_y_axis_labels[index];
        lv_obj_t *temperature_label = s_ui.plot_temperature_y_axis_labels[index];

        if (pressure_label != NULL) {
            snprintf(
                s_ui.plot_pressure_y_axis_label_text[index],
                sizeof(s_ui.plot_pressure_y_axis_label_text[index]),
                "%.1f",
                pressure_max * normalized);
            lv_label_set_text_static(pressure_label, s_ui.plot_pressure_y_axis_label_text[index]);
            lv_obj_update_layout(pressure_label);
            lv_obj_set_pos(
                pressure_label,
                chart_x + chart_w + y_axis_pressure_x_offset,
                pos_y - (lv_obj_get_height(pressure_label) / 2));
        }

        if (weight_label != NULL) {
            snprintf(
                s_ui.plot_weight_y_axis_label_text[index],
                sizeof(s_ui.plot_weight_y_axis_label_text[index]),
                "%.1f",
                weight_max * normalized);
            lv_label_set_text_static(weight_label, s_ui.plot_weight_y_axis_label_text[index]);
            lv_obj_update_layout(weight_label);
            lv_obj_set_pos(
                weight_label,
                chart_x + chart_w + y_axis_weight_x_offset,
                pos_y - (lv_obj_get_height(weight_label) / 2));
        }

        if (flow_label != NULL) {
            snprintf(
                s_ui.plot_flow_y_axis_label_text[index],
                sizeof(s_ui.plot_flow_y_axis_label_text[index]),
                "%.1f",
                flow_max * normalized);
            lv_label_set_text_static(flow_label, s_ui.plot_flow_y_axis_label_text[index]);
            lv_obj_update_layout(flow_label);
            lv_obj_set_pos(
                flow_label,
                chart_x + chart_w + y_axis_flow_x_offset,
                pos_y - (lv_obj_get_height(flow_label) / 2));
        }

        if (temperature_label != NULL) {
            snprintf(
                s_ui.plot_temperature_y_axis_label_text[index],
                sizeof(s_ui.plot_temperature_y_axis_label_text[index]),
                "%.1f",
                temperature_max * normalized);
            lv_label_set_text_static(temperature_label, s_ui.plot_temperature_y_axis_label_text[index]);
            lv_obj_update_layout(temperature_label);
            lv_obj_set_pos(
                temperature_label,
                chart_x + chart_w + y_axis_temperature_x_offset,
                pos_y - (lv_obj_get_height(temperature_label) / 2));
        }
    }
}

/**
 * @brief Render Live Shot chart from history using current view + axis settings.
 */
static void ui_render_live_shot_plot(void)
{
    uint32_t display_start = 0U;
    uint32_t display_count = 0U;
    uint32_t point_count = UI_PLOT_WINDOW_POINT_COUNT;
    bool rolling_window = false;
    float x_axis_span_s = (float)UI_PLOT_WINDOW_SECONDS;
    float x_axis_start_s = 0.0f;
    float x_axis_end_s = 0.0f;
    float pressure_max = 0.0f;
    float weight_max = 0.0f;
    float flow_max = 0.0f;
    float temperature_max = 0.0f;

    if (s_ui.plot_chart == NULL ||
        s_ui.plot_pressure_series == NULL ||
        s_ui.plot_weight_series == NULL ||
        s_ui.plot_flow_series == NULL ||
        s_ui.plot_temperature_series == NULL) {
        return;
    }

    for (uint32_t index = 0; index < UI_PLOT_POINT_COUNT; index++) {
        s_ui.plot_pressure_chart_y_values[index] = LV_CHART_POINT_NONE;
        s_ui.plot_weight_chart_y_values[index] = LV_CHART_POINT_NONE;
        s_ui.plot_flow_chart_y_values[index] = LV_CHART_POINT_NONE;
        s_ui.plot_temperature_chart_y_values[index] = LV_CHART_POINT_NONE;
    }

    if (s_ui.plot_history_count == 0U) {
        lv_chart_set_point_count(s_ui.plot_chart, UI_PLOT_WINDOW_POINT_COUNT);
        lv_chart_refresh(s_ui.plot_chart);
        ui_update_plot_axis_labels(
            s_ui.plot_manual_pressure_max_bar,
            s_ui.plot_manual_weight_max_g,
            s_ui.plot_manual_flow_max_ml_s,
            s_ui.plot_manual_temperature_max_c,
            (float)UI_PLOT_WINDOW_SECONDS,
            true);
        return;
    }

    rolling_window = (s_ui.brewing && !s_ui.plot_history_frozen);
    if (rolling_window) {
        float end_time = s_ui.plot_history_time_sec[s_ui.plot_history_count - 1U];
        float window_start = (end_time > (float)UI_PLOT_WINDOW_SECONDS)
                                 ? (end_time - (float)UI_PLOT_WINDOW_SECONDS)
                                 : 0.0f;
        while (display_start < s_ui.plot_history_count &&
               s_ui.plot_history_time_sec[display_start] < window_start) {
            display_start++;
        }
        x_axis_span_s = (float)UI_PLOT_WINDOW_SECONDS;
        x_axis_start_s = window_start;
        x_axis_end_s = window_start + x_axis_span_s;
        point_count = UI_PLOT_WINDOW_POINT_COUNT;
    } else {
        display_start = 0U;
        x_axis_start_s = s_ui.plot_history_time_sec[0];
        x_axis_end_s = s_ui.plot_history_time_sec[s_ui.plot_history_count - 1U];
        x_axis_span_s = x_axis_end_s - x_axis_start_s;
        if (x_axis_span_s < 1.0f) {
            x_axis_span_s = 1.0f;
            x_axis_end_s = x_axis_start_s + x_axis_span_s;
        }
        point_count = UI_PLOT_POINT_COUNT;
    }

    display_count = s_ui.plot_history_count - display_start;
    if (display_count == 0U) {
        display_count = 1U;
    }
    if (display_count > point_count) {
        display_count = point_count;
    }

    lv_chart_set_point_count(s_ui.plot_chart, point_count);

    for (uint32_t index = 0; index < (s_ui.plot_history_count - display_start); index++) {
        uint32_t src_idx = display_start + index;
        float sample_time_s = s_ui.plot_history_time_sec[src_idx];
        float pressure_value = s_ui.plot_history_pressure_bar[src_idx];
        float weight_value = s_ui.plot_history_weight_g[src_idx];
        float flow_value = s_ui.plot_history_flow_ml_s[src_idx];
        float temperature_value = s_ui.plot_history_temperature_c[src_idx];

        if (sample_time_s < x_axis_start_s || sample_time_s > x_axis_end_s + 0.0001f) {
            continue;
        }
        if (pressure_value > pressure_max) {
            pressure_max = pressure_value;
        }
        if (weight_value > weight_max) {
            weight_max = weight_value;
        }
        if (flow_value > flow_max) {
            flow_max = flow_value;
        }
        if (temperature_value > temperature_max) {
            temperature_max = temperature_value;
        }
    }

    pressure_max = s_ui.plot_autoscale_pressure
                       ? ui_plot_clamp_positive(pressure_max * 1.1f, 1.0f, 20.0f, 12.0f)
                       : s_ui.plot_manual_pressure_max_bar;
    weight_max = s_ui.plot_autoscale_weight
                     ? ui_plot_clamp_positive(weight_max * 1.1f, 10.0f, 100.0f, 60.0f)
                     : s_ui.plot_manual_weight_max_g;
    flow_max = s_ui.plot_autoscale_flow
                   ? ui_plot_clamp_positive(flow_max * 1.1f, 1.0f, 30.0f, 5.0f)
                   : s_ui.plot_manual_flow_max_ml_s;
    temperature_max = s_ui.plot_autoscale_temperature
                          ? ui_plot_clamp_positive(temperature_max * 1.05f, 30.0f, 105.0f, 105.0f)
                          : s_ui.plot_manual_temperature_max_c;

    for (uint32_t index = 0; index < (s_ui.plot_history_count - display_start); index++) {
        uint32_t src_idx = display_start + index;
        float sample_time_s = s_ui.plot_history_time_sec[src_idx];
        float relative_s = 0.0f;
        uint32_t bin_idx = 0U;
        float pressure_value = s_ui.plot_history_pressure_bar[src_idx];
        float weight_value = s_ui.plot_history_weight_g[src_idx];
        float flow_value = s_ui.plot_history_flow_ml_s[src_idx];
        float temperature_value = s_ui.plot_history_temperature_c[src_idx];
        if (sample_time_s < x_axis_start_s || sample_time_s > x_axis_end_s + 0.0001f) {
            continue;
        }
        relative_s = sample_time_s - x_axis_start_s;
        if (relative_s < 0.0f) {
            relative_s = 0.0f;
        }
        if (relative_s > x_axis_span_s) {
            relative_s = x_axis_span_s;
        }
        bin_idx = (uint32_t)lroundf((relative_s / x_axis_span_s) * (float)(point_count - 1U));
        if (bin_idx >= point_count) {
            bin_idx = point_count - 1U;
        }
        int32_t pressure_scaled = (int32_t)lroundf((pressure_value / pressure_max) * (float)UI_PLOT_NORMALIZED_MAX);
        int32_t weight_scaled = (int32_t)lroundf((weight_value / weight_max) * (float)UI_PLOT_NORMALIZED_MAX);
        int32_t flow_scaled = (int32_t)lroundf((flow_value / flow_max) * (float)UI_PLOT_NORMALIZED_MAX);
        int32_t temperature_scaled = (int32_t)lroundf((temperature_value / temperature_max) * (float)UI_PLOT_NORMALIZED_MAX);

        s_ui.plot_pressure_chart_y_values[bin_idx] = pressure_scaled;
        s_ui.plot_weight_chart_y_values[bin_idx] = weight_scaled;
        s_ui.plot_flow_chart_y_values[bin_idx] = flow_scaled;
        s_ui.plot_temperature_chart_y_values[bin_idx] = temperature_scaled;
    }

    /* Fill sparse time bins with last known value only inside the populated
     * sample range. This keeps continuity between received samples but avoids
     * drawing "future" flat lines all the way to the right edge. */
    uint32_t max_populated_idx = 0U;
    bool have_populated_idx = false;
    for (uint32_t index = 0; index < point_count; index++) {
        if (s_ui.plot_pressure_chart_y_values[index] != LV_CHART_POINT_NONE ||
            s_ui.plot_weight_chart_y_values[index] != LV_CHART_POINT_NONE ||
            s_ui.plot_flow_chart_y_values[index] != LV_CHART_POINT_NONE ||
            s_ui.plot_temperature_chart_y_values[index] != LV_CHART_POINT_NONE) {
            max_populated_idx = index;
            have_populated_idx = true;
        }
    }

    if (have_populated_idx) {
        int32_t last_pressure = LV_CHART_POINT_NONE;
        int32_t last_weight = LV_CHART_POINT_NONE;
        int32_t last_flow = LV_CHART_POINT_NONE;
        int32_t last_temperature = LV_CHART_POINT_NONE;
        for (uint32_t index = 0; index <= max_populated_idx; index++) {
            if (s_ui.plot_pressure_chart_y_values[index] == LV_CHART_POINT_NONE) {
                if (last_pressure != LV_CHART_POINT_NONE) {
                    s_ui.plot_pressure_chart_y_values[index] = last_pressure;
                }
            } else {
                last_pressure = s_ui.plot_pressure_chart_y_values[index];
            }

            if (s_ui.plot_weight_chart_y_values[index] == LV_CHART_POINT_NONE) {
                if (last_weight != LV_CHART_POINT_NONE) {
                    s_ui.plot_weight_chart_y_values[index] = last_weight;
                }
            } else {
                last_weight = s_ui.plot_weight_chart_y_values[index];
            }

            if (s_ui.plot_flow_chart_y_values[index] == LV_CHART_POINT_NONE) {
                if (last_flow != LV_CHART_POINT_NONE) {
                    s_ui.plot_flow_chart_y_values[index] = last_flow;
                }
            } else {
                last_flow = s_ui.plot_flow_chart_y_values[index];
            }

            if (s_ui.plot_temperature_chart_y_values[index] == LV_CHART_POINT_NONE) {
                if (last_temperature != LV_CHART_POINT_NONE) {
                    s_ui.plot_temperature_chart_y_values[index] = last_temperature;
                }
            } else {
                last_temperature = s_ui.plot_temperature_chart_y_values[index];
            }
        }
    }

    lv_chart_set_range(s_ui.plot_chart, LV_CHART_AXIS_PRIMARY_Y, 0, UI_PLOT_NORMALIZED_MAX);
    ui_update_plot_axis_labels(
        pressure_max,
        weight_max,
        flow_max,
        temperature_max,
        x_axis_span_s,
        rolling_window);
    lv_chart_refresh(s_ui.plot_chart);
}

/**
 * @brief Append one packet sample to Live Shot history and refresh the chart.
 *
 * @details Uses the latest sample in each channel to keep a deterministic
 * 10 Hz history timeline that can be shown as rolling live-window or static
 * full-shot playback.
 */
static void ui_plot_realtime_packet(const data_downlink_packet_t *packet, uint32_t packet_interval_us)
{
    const uint32_t sample_count = UI_PLOT_SAMPLES_PER_CHANNEL;
    float pressure_value = 0.0f;
    float flow_value = 0.0f;
    float temperature_value = 0.0f;
    float weight_value = 0.0f;
    float elapsed_s = 0.0f;
    float fallback_dt_s = 0.1f;

    if (packet == NULL ||
        s_ui.plot_chart == NULL ||
        s_ui.plot_pressure_series == NULL ||
        s_ui.plot_weight_series == NULL ||
        s_ui.plot_flow_series == NULL ||
        s_ui.plot_temperature_series == NULL ||
        sample_count == 0U) {
        return;
    }

    if (!s_ui.brewing || s_ui.plot_history_frozen) {
        return;
    }

    if ((UI_PLOT_CHANNEL_OFFSET_TEMPERATURE + sample_count) <= DATA_SIZE_FLOATS) {
        pressure_value = packet->f[UI_PLOT_CHANNEL_OFFSET_PRESSURE + sample_count - 1U];
        flow_value = packet->f[UI_PLOT_CHANNEL_OFFSET_FLOW + sample_count - 1U];
        temperature_value = packet->f[UI_PLOT_CHANNEL_OFFSET_TEMPERATURE + sample_count - 1U];
    }
    if ((UI_PLOT_CHANNEL_OFFSET_WEIGHT + sample_count) <= DATA_SIZE_FLOATS) {
        weight_value = packet->f[UI_PLOT_CHANNEL_OFFSET_WEIGHT + sample_count - 1U];
    } else if (packet->i[LCD_CONTROLLER_BREW_SLOT_WEIGHT_X100] >= 0) {
        weight_value = (float)packet->i[LCD_CONTROLLER_BREW_SLOT_WEIGHT_X100] / 100.0f;
    } else {
        weight_value = s_ui.brew_live_weight_g;
    }

    if (packet->i[LCD_CONTROLLER_BREW_SLOT_BREW_ELAPSED_MS] >= 0) {
        elapsed_s = (float)packet->i[LCD_CONTROLLER_BREW_SLOT_BREW_ELAPSED_MS] / 1000.0f;
    } else if (s_ui.plot_history_count > 0U) {
        elapsed_s = s_ui.plot_history_time_sec[s_ui.plot_history_count - 1U];
    }

    if (packet_interval_us > 0U) {
        fallback_dt_s = (float)packet_interval_us / 1000000.0f;
    }
    if (s_ui.plot_history_count > 0U) {
        float min_next_time = s_ui.plot_history_time_sec[s_ui.plot_history_count - 1U] + fallback_dt_s;
        if (elapsed_s < min_next_time) {
            elapsed_s = min_next_time;
        }
    }

    ui_plot_append_history_sample(
        elapsed_s,
        ui_plot_clamp_positive(pressure_value, 0.0f, 25.0f, 0.0f),
        ui_plot_clamp_positive(weight_value, 0.0f, 200.0f, 0.0f),
        ui_plot_clamp_positive(flow_value, 0.0f, 40.0f, 0.0f),
        ui_plot_clamp_positive(temperature_value, 0.0f, 120.0f, 0.0f));
    s_ui.plot_stream_started = true;
    ui_render_live_shot_plot();
}

/**
 * @brief Tear down the Live Shot range overlay and clear widget handles.
 */
static void ui_close_plot_range_overlay(void)
{
    if (s_ui.plot_range_overlay != NULL) {
        lv_obj_del(s_ui.plot_range_overlay);
    }

    s_ui.plot_range_overlay = NULL;
    s_ui.plot_range_pressure_value_label = NULL;
    s_ui.plot_range_weight_value_label = NULL;
    s_ui.plot_range_flow_value_label = NULL;
    s_ui.plot_range_temperature_value_label = NULL;
    s_ui.plot_range_pressure_roller = NULL;
    s_ui.plot_range_weight_roller = NULL;
    s_ui.plot_range_flow_roller = NULL;
    s_ui.plot_range_temperature_roller = NULL;
}

/**
 * @brief Refresh all value labels shown in the Live Shot range overlay.
 */
static void ui_plot_range_update_labels(void)
{
    if (s_ui.plot_range_pressure_value_label != NULL) {
        lv_label_set_text_fmt(
            s_ui.plot_range_pressure_value_label,
            "Pressure Maximal Value | %.1f Bar",
            (double)s_ui.plot_manual_pressure_max_bar);
    }
    if (s_ui.plot_range_weight_value_label != NULL) {
        lv_label_set_text_fmt(
            s_ui.plot_range_weight_value_label,
            "Weight Maximal Value | %.1f Gr",
            (double)s_ui.plot_manual_weight_max_g);
    }
    if (s_ui.plot_range_flow_value_label != NULL) {
        lv_label_set_text_fmt(
            s_ui.plot_range_flow_value_label,
            "Flow | %.1f ml/Sec",
            (double)s_ui.plot_manual_flow_max_ml_s);
    }
    if (s_ui.plot_range_temperature_value_label != NULL) {
        lv_label_set_text_fmt(
            s_ui.plot_range_temperature_value_label,
            "Temperature | %.1f C",
            (double)s_ui.plot_manual_temperature_max_c);
    }
}

/**
 * @brief Populate a roller with fixed one-decimal options.
 */
static void ui_plot_range_set_roller_options_tenths(lv_obj_t *roller,
                                                     int min_tenths,
                                                     int max_tenths,
                                                     int selected_tenths)
{
    size_t option_count = 0U;
    size_t buffer_size = 0U;
    char *options = NULL;
    size_t used = 0U;

    if (roller == NULL || max_tenths < min_tenths) {
        return;
    }

    if (selected_tenths < min_tenths) {
        selected_tenths = min_tenths;
    }
    if (selected_tenths > max_tenths) {
        selected_tenths = max_tenths;
    }

    option_count = (size_t)(max_tenths - min_tenths + 1);
    buffer_size = option_count * 10U + 1U;
    options = (char *)malloc(buffer_size);
    if (options == NULL) {
        return;
    }

    options[0] = '\0';
    for (int value = min_tenths; value <= max_tenths; value++) {
        int written = snprintf(options + used,
                               buffer_size - used,
                               (value == min_tenths) ? "%d.%d" : "\n%d.%d",
                               value / 10,
                               abs(value % 10));
        if (written <= 0 || (size_t)written >= (buffer_size - used)) {
            break;
        }
        used += (size_t)written;
    }

    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
    lv_roller_set_selected(roller, (uint16_t)(selected_tenths - min_tenths), LV_ANIM_OFF);
    free(options);
}

/**
 * @brief Build and show the Live Shot range overlay.
 */
static void ui_open_plot_range_overlay(void)
{
    lv_obj_t *panel = NULL;
    lv_obj_t *title = NULL;
    lv_obj_t *done_btn = NULL;
    lv_obj_t *done_lbl = NULL;
    const lv_coord_t line_start_y = 74;
    const lv_coord_t line_step_y = 104;
    const lv_coord_t label_x = 24;
    const lv_coord_t roller_x = 472;
    const lv_coord_t roller_w = 240;
    const lv_coord_t roller_h = 64;

    ui_close_plot_range_overlay();

    if (s_ui.root == NULL) {
        return;
    }

    s_ui.plot_range_overlay = lv_obj_create(s_ui.root);
    lv_obj_remove_style_all(s_ui.plot_range_overlay);
    lv_obj_set_size(s_ui.plot_range_overlay, 800, 480);
    lv_obj_set_style_bg_color(s_ui.plot_range_overlay, lv_color_hex(0x020617), 0);
    lv_obj_set_style_bg_opa(s_ui.plot_range_overlay, LV_OPA_70, 0);

    panel = lv_obj_create(s_ui.plot_range_overlay);
    lv_obj_set_size(panel, 760, 414);
    lv_obj_center(panel);
    ui_style_card(panel, UI_COLOR_PANEL);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_bottom(panel, 28, 0);

    title = lv_label_create(panel);
    lv_label_set_text(title, "Set Range");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

    s_ui.plot_range_pressure_value_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_ui.plot_range_pressure_value_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_ui.plot_range_pressure_value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_width(s_ui.plot_range_pressure_value_label, roller_x - label_x - 18);
    lv_obj_set_pos(s_ui.plot_range_pressure_value_label, label_x, line_start_y);

    s_ui.plot_range_weight_value_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_ui.plot_range_weight_value_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_ui.plot_range_weight_value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_width(s_ui.plot_range_weight_value_label, roller_x - label_x - 18);
    lv_obj_set_pos(s_ui.plot_range_weight_value_label, label_x, line_start_y + line_step_y);

    s_ui.plot_range_flow_value_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_ui.plot_range_flow_value_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_ui.plot_range_flow_value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_width(s_ui.plot_range_flow_value_label, roller_x - label_x - 18);
    lv_obj_set_pos(s_ui.plot_range_flow_value_label, label_x, line_start_y + (2 * line_step_y));

    s_ui.plot_range_temperature_value_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_ui.plot_range_temperature_value_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_ui.plot_range_temperature_value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_width(s_ui.plot_range_temperature_value_label, roller_x - label_x - 18);
    lv_obj_set_pos(s_ui.plot_range_temperature_value_label, label_x, line_start_y + (3 * line_step_y));

    s_ui.plot_range_pressure_roller = lv_roller_create(panel);
    lv_obj_set_size(s_ui.plot_range_pressure_roller, roller_w, roller_h);
    lv_obj_set_pos(s_ui.plot_range_pressure_roller, roller_x, line_start_y - 10);
    lv_obj_add_event_cb(s_ui.plot_range_pressure_roller,
                        ui_plot_range_roller_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)UI_PLOT_RANGE_ROLLER_PRESSURE);

    s_ui.plot_range_weight_roller = lv_roller_create(panel);
    lv_obj_set_size(s_ui.plot_range_weight_roller, roller_w, roller_h);
    lv_obj_set_pos(s_ui.plot_range_weight_roller, roller_x, line_start_y + line_step_y - 10);
    lv_obj_add_event_cb(s_ui.plot_range_weight_roller,
                        ui_plot_range_roller_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)UI_PLOT_RANGE_ROLLER_WEIGHT);

    s_ui.plot_range_flow_roller = lv_roller_create(panel);
    lv_obj_set_size(s_ui.plot_range_flow_roller, roller_w, roller_h);
    lv_obj_set_pos(s_ui.plot_range_flow_roller, roller_x, line_start_y + (2 * line_step_y) - 10);
    lv_obj_add_event_cb(s_ui.plot_range_flow_roller,
                        ui_plot_range_roller_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)UI_PLOT_RANGE_ROLLER_FLOW);

    s_ui.plot_range_temperature_roller = lv_roller_create(panel);
    lv_obj_set_size(s_ui.plot_range_temperature_roller, roller_w, roller_h);
    lv_obj_set_pos(s_ui.plot_range_temperature_roller, roller_x, line_start_y + (3 * line_step_y) - 10);
    lv_obj_add_event_cb(s_ui.plot_range_temperature_roller,
                        ui_plot_range_roller_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)UI_PLOT_RANGE_ROLLER_TEMPERATURE);

    ui_plot_range_set_roller_options_tenths(
        s_ui.plot_range_pressure_roller,
        10,
        200,
        (int)lroundf(s_ui.plot_manual_pressure_max_bar * 10.0f));
    ui_plot_range_set_roller_options_tenths(
        s_ui.plot_range_weight_roller,
        100,
        1000,
        (int)lroundf(s_ui.plot_manual_weight_max_g * 10.0f));
    ui_plot_range_set_roller_options_tenths(
        s_ui.plot_range_flow_roller,
        10,
        300,
        (int)lroundf(s_ui.plot_manual_flow_max_ml_s * 10.0f));
    ui_plot_range_set_roller_options_tenths(
        s_ui.plot_range_temperature_roller,
        300,
        1050,
        (int)lroundf(s_ui.plot_manual_temperature_max_c * 10.0f));

    ui_plot_range_update_labels();

    done_btn = lv_button_create(panel);
    lv_obj_set_size(done_btn, 240, 56);
    lv_obj_set_pos(done_btn, (760 - 240) / 2, line_start_y + (4 * line_step_y) + 22);
    ui_style_action_button(done_btn);
    lv_obj_add_event_cb(done_btn, ui_plot_range_done_event_cb, LV_EVENT_CLICKED, NULL);

    done_lbl = lv_label_create(done_btn);
    lv_label_set_text(done_lbl, "Done");
    ui_style_button_label(done_lbl);
    lv_obj_center(done_lbl);
}

/**
 * @brief Handle Live Shot autoscale checkbox toggles.
 */
static void ui_plot_checkbox_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    bool checked = lv_obj_has_state(obj, LV_STATE_CHECKED);
    ui_plot_checkbox_id_t checkbox_id = (ui_plot_checkbox_id_t)(intptr_t)lv_event_get_user_data(e);

    switch (checkbox_id) {
    case UI_PLOT_CHECKBOX_AUTOSCALE_PRESSURE:
        s_ui.plot_autoscale_pressure = checked;
        break;
    case UI_PLOT_CHECKBOX_AUTOSCALE_WEIGHT:
        s_ui.plot_autoscale_weight = checked;
        break;
    case UI_PLOT_CHECKBOX_AUTOSCALE_FLOW:
        s_ui.plot_autoscale_flow = checked;
        break;
    case UI_PLOT_CHECKBOX_AUTOSCALE_TEMPERATURE:
        s_ui.plot_autoscale_temperature = checked;
        break;
    default:
        break;
    }

    if (system_constants_set_live_shot_autoscale(
            s_ui.plot_autoscale_pressure,
            s_ui.plot_autoscale_weight,
            s_ui.plot_autoscale_flow,
            s_ui.plot_autoscale_temperature) != ESP_OK) {
        ESP_LOGW(TAG, "Live Shot autoscale apply failed.");
    } else if (system_constants_save_live_shot_ranges() != ESP_OK) {
        ESP_LOGW(TAG, "Live Shot autoscale save to SD failed.");
    }

    ui_render_live_shot_plot();
}

/**
 * @brief Open the Live Shot range configuration overlay.
 */
static void ui_plot_set_range_button_event_cb(lv_event_t *e)
{
    (void)e;
    ui_open_plot_range_overlay();
}

/**
 * @brief Handle one-decimal range roller changes.
 */
static void ui_plot_range_roller_event_cb(lv_event_t *e)
{
    ui_plot_range_roller_id_t roller_id = (ui_plot_range_roller_id_t)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *roller = lv_event_get_target(e);
    char selected_text[24] = {0};
    float value = 0.0f;

    if (roller == NULL) {
        return;
    }

    lv_roller_get_selected_str(roller, selected_text, sizeof(selected_text));
    value = strtof(selected_text, NULL);

    switch (roller_id) {
    case UI_PLOT_RANGE_ROLLER_PRESSURE:
        s_ui.plot_manual_pressure_max_bar = ui_plot_clamp_positive(value, 1.0f, 20.0f, 12.0f);
        break;
    case UI_PLOT_RANGE_ROLLER_WEIGHT:
        s_ui.plot_manual_weight_max_g = ui_plot_clamp_positive(value, 10.0f, 100.0f, 60.0f);
        break;
    case UI_PLOT_RANGE_ROLLER_FLOW:
        s_ui.plot_manual_flow_max_ml_s = ui_plot_clamp_positive(value, 1.0f, 30.0f, 5.0f);
        break;
    case UI_PLOT_RANGE_ROLLER_TEMPERATURE:
        s_ui.plot_manual_temperature_max_c = ui_plot_clamp_positive(value, 30.0f, 105.0f, 105.0f);
        break;
    default:
        break;
    }

    ui_plot_range_update_labels();
    ui_render_live_shot_plot();
}

/**
 * @brief Persist range settings and close the overlay.
 */
static void ui_plot_range_done_event_cb(lv_event_t *e)
{
    (void)e;
    if (system_constants_set_live_shot_ranges(
            s_ui.plot_manual_pressure_max_bar,
            s_ui.plot_manual_weight_max_g,
            s_ui.plot_manual_flow_max_ml_s,
            s_ui.plot_manual_temperature_max_c) != ESP_OK) {
        ESP_LOGW(TAG, "Live Shot range apply failed.");
    } else if (system_constants_save_live_shot_ranges() != ESP_OK) {
        ESP_LOGW(TAG, "Live Shot range save to SD failed.");
    }
    ui_close_plot_range_overlay();
    ui_render_live_shot_plot();
}

/**
 * @brief Ingest Home/Brew metrics from the latest downlink packet.
 *
 * @details Packet integer slots map as:
 * - i[1]: profile id
 * - i[2]: brew elapsed ms
 * - i[3]: brew duration ms
 * - i[6]: target temperature milli-C
 * - i[7]: water level percent x10
 * - i[8]: weight grams x100
 * - i[9]: warmup boolean
 * - i[10]: steam boolean
 * - i[12]: shot target grams x100
 * - i[13]: live pressure milli-bar
 * - i[14]: validity bit-mask
 */
static void ui_ingest_home_metrics_from_packet(const data_downlink_packet_t *packet, uint32_t packet_interval_us)
{
    const uint32_t sample_count = UI_PLOT_SAMPLES_PER_CHANNEL;
    uint32_t valid_mask = 0U;

    (void)packet_interval_us;

    if (packet == NULL) {
        return;
    }

    if (packet->i[LCD_CONTROLLER_BREW_SLOT_VALIDITY_MASK] >= 0) {
        valid_mask = (uint32_t)packet->i[LCD_CONTROLLER_BREW_SLOT_VALIDITY_MASK];
    }

    if (packet->i[1] >= 1 && packet->i[1] <= ui_get_constants()->profile_count) {
        s_ui.active_profile = packet->i[1];
    }
    if (packet->i[6] > 0) {
        s_ui.target_temp_c = (int)lroundf((float)packet->i[6] / 1000.0f);
    }

    if ((2U * sample_count) < DATA_SIZE_FLOATS) {
        s_ui.brew_live_temperature_c = packet->f[(2U * sample_count) + (sample_count - 1U)];
        s_ui.brew_server_temperature_valid = true;
    } else {
        s_ui.brew_server_temperature_valid = false;
    }

    if ((valid_mask & UI_BREW_VALID_SHOT_TIMER_MASK) != 0U && packet->i[2] >= 0) {
        s_ui.shot_s = (int)((uint32_t)packet->i[2] / 1000U);
        s_ui.brew_server_shot_valid = true;
    } else {
        s_ui.brew_server_shot_valid = false;
    }

    if ((valid_mask & UI_BREW_VALID_LIVE_PRESSURE_MASK) != 0U &&
        packet->i[LCD_CONTROLLER_BREW_SLOT_LIVE_PRESSURE_MBAR] >= 0) {
        s_ui.brew_live_pressure_bar =
            (float)packet->i[LCD_CONTROLLER_BREW_SLOT_LIVE_PRESSURE_MBAR] / 1000.0f;
        s_ui.brew_server_pressure_valid = true;
    } else {
        s_ui.brew_server_pressure_valid = false;
    }

    if ((valid_mask & UI_BREW_VALID_WATER_LEVEL_MASK) != 0U) {
        s_ui.brew_live_water_level_pct = (float)packet->i[7] / 10.0f;
        if (s_ui.brew_live_water_level_pct < 0.0f) {
            s_ui.brew_live_water_level_pct = 0.0f;
        }
        if (s_ui.brew_live_water_level_pct > 100.0f) {
            s_ui.brew_live_water_level_pct = 100.0f;
        }
        s_ui.brew_server_water_valid = true;
    } else {
        s_ui.brew_server_water_valid = false;
    }

    if ((valid_mask & UI_BREW_VALID_WEIGHT_MASK) != 0U) {
        s_ui.brew_live_weight_g = (float)packet->i[8] / 100.0f;
        if (s_ui.brew_live_weight_g < 0.0f) {
            s_ui.brew_live_weight_g = 0.0f;
        }
        if (s_ui.brew_live_weight_g > 200.0f) {
            s_ui.brew_live_weight_g = 200.0f;
        }
        s_ui.brew_server_weight_valid = true;
    } else {
        s_ui.brew_server_weight_valid = false;
    }

    if ((valid_mask & UI_BREW_VALID_WARMUP_MASK) != 0U && (packet->i[9] == 0 || packet->i[9] == 1)) {
        s_ui.brew_warmup_on = (packet->i[9] == 1);
        s_ui.brew_server_warmup_valid = true;
    } else {
        s_ui.brew_server_warmup_valid = false;
    }

    if (packet->i[10] == 0 || packet->i[10] == 1) {
        s_ui.brew_steam_indicator_on = (packet->i[10] == 1);
    }

    if (packet->i[12] >= 0) {
        s_ui.brew_shot_target_preview_g = (float)packet->i[12] / 100.0f;
    }

    lcd_controller_brew_home_state_t brew_state = {
        .profile_id = (s_ui.active_profile > 0) ? (uint8_t)s_ui.active_profile : 1U,
        .brew_elapsed_ms = (uint32_t)((packet->i[2] >= 0) ? packet->i[2] : 0),
        .brew_duration_ms = (uint32_t)((packet->i[3] >= 0) ? packet->i[3] : 0),
        .target_temperature_c = (float)s_ui.target_temp_c,
        .target_pressure_bar = (float)packet->i[4] / 1000.0f,
        .target_flow_ml_s = (float)packet->i[5] / 1000.0f,
        .live_pressure_bar = s_ui.brew_live_pressure_bar,
        .live_temperature_c = s_ui.brew_live_temperature_c,
        .live_water_level_pct = s_ui.brew_live_water_level_pct,
        .live_weight_g = s_ui.brew_live_weight_g,
        .shot_target_preview_g = s_ui.brew_shot_target_preview_g,
        .warmup_on = s_ui.brew_warmup_on,
        .steam_on = s_ui.brew_steam_indicator_on,
        .uptime_minutes = s_ui.brew_uptime_minutes,
    };
    (void)lcd_controller_protocol_publish_brew_home_state(&brew_state);

    ui_update_home_labels();
    ui_update_header_status();
}

/**
 * @brief Queue ProfileSelection payload for simulator-side profile binding.
 *
 * @details The simulator currently supports one active brew profile, but this
 * payload keeps profile metadata explicit so multi-profile expansion is easy.
 */
static void ui_send_profile_selection_command(bool offline_startup)
{
    char payload_text[96];
    esp_err_t ret;
    snprintf(
        payload_text,
        sizeof(payload_text),
        "ProfileSelection;profile=%d;offline=%d",
        s_ui.active_profile,
        offline_startup ? 1 : 0);
    ret = communication_functions_queue_data_text_command(payload_text);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ProfileSelection queue failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "ProfileSelection queued: %s", payload_text);
    }
}

/**
 * @brief Reset queued and visible Simulate Data stream state.
 *
 * @details Called when simulation transitions to OFF, either by local toggle
 * action or by simulator-side event synchronization.
 */
static void ui_reset_sim_data_stream_state(void)
{
    ui_clear_sim_data_fifo();
    ui_clear_sim_data_plot();
    ui_clear_plot_data();

    s_ui.sim_data_packets_received = 0;
    s_ui.sim_data_last_seq = 0;
    s_ui.sim_data_last_seq_valid = false;
    s_ui.sim_data_last_packet_rx_us = 0;
    s_ui.sim_data_packet_interval_us = 0;
    s_ui.sim_data_sample_period_us = 0;
    s_ui.sim_data_rx_fifo_packet_backlog_npackets = 0U;
    s_ui.sim_data_timing_valid = false;
    s_ui.sim_data_last_y_axis_limit = 0;
    s_ui.sim_data_last_axis_packet_interval_us = 0U;
    s_ui.sim_data_last_axis_refresh_us = 0;
    s_ui.sim_data_last_seq_gap_log_us = 0;
    s_ui.sim_data_last_chart_refresh_us = 0;
    s_ui.sim_data_last_status_refresh_us = 0;
    s_ui.shot_s = 0;
    s_ui.brew_server_shot_valid = false;
    s_ui.brew_server_pressure_valid = false;
    /* Keep last water-level reading visible after shot completion. */
    s_ui.brew_server_weight_valid = false;
    s_ui.brew_server_warmup_valid = false;
    s_ui.brew_server_temperature_valid = false;
    ui_update_sim_data_axis_labels(UI_SIM_DATA_DEFAULT_PACKET_INTERVAL_US, 200);
}

/**
 * @brief Refresh the Simulate Data status line.
 *
 * @details Shows stream enable state and packet progress so operators can
 * confirm that simulator data is flowing into the client graph.
 */
static void ui_update_sim_data_status_label(void)
{
    uint32_t backlog_packets = data_downlink_available();
    const char *stream_value = s_ui.sim_data_enabled ? "ON" : "OFF";
    char packets_value[24] = "0";
    char seq_value[24] = "N/A";
    char packet_dt_value[24] = "N/A";
    char sample_dt_value[24] = "N/A";

    if (backlog_packets > UINT16_MAX) {
        backlog_packets = UINT16_MAX;
    }
    s_ui.sim_data_rx_fifo_packet_backlog_npackets = (uint16_t)backlog_packets;

    if (s_ui.sim_data_status_label == NULL) {
        return;
    }

    if (s_ui.sim_data_packets_received > 0U) {
        snprintf(packets_value, sizeof(packets_value), "%" PRIu32, s_ui.sim_data_packets_received);
    }
    if (s_ui.sim_data_last_seq_valid) {
        snprintf(seq_value, sizeof(seq_value), "%" PRIu32, s_ui.sim_data_last_seq);
    }
    if (s_ui.sim_data_timing_valid) {
        snprintf(packet_dt_value, sizeof(packet_dt_value), "%" PRIu32, s_ui.sim_data_packet_interval_us);
        snprintf(sample_dt_value, sizeof(sample_dt_value), "%" PRIu32, s_ui.sim_data_sample_period_us);
    }

    snprintf(
        s_ui.sim_data_status_text,
        sizeof(s_ui.sim_data_status_text),
        "Stream: %s"
        "\nPackets: %s"
        "\nLast Seq: %s"
        "\nPacket dt [us]: %s"
        "\nSample dt [us]: %s"
        "\nRX_FIFO_PacketBacklogNPackets: %" PRIu16,
        stream_value,
        packets_value,
        seq_value,
        packet_dt_value,
        sample_dt_value,
        s_ui.sim_data_rx_fifo_packet_backlog_npackets);
    lv_label_set_text_static(s_ui.sim_data_status_label, s_ui.sim_data_status_text);
    s_ui.sim_data_last_status_refresh_us = esp_timer_get_time();
}

/**
 * @brief Parse simulator stream ON/OFF events from a text payload.
 *
 * @details Text comparisons are case-insensitive so payload variations from
 * bridge/runtime logs still map to a single stream toggle state.
 *
 * @param[in] payload_text Text payload from communication snapshot.
 * @param[out] out_enabled Parsed stream enable state.
 *
 * @return `true` when an ON/OFF event token is found; otherwise `false`.
 */
static bool ui_try_parse_sim_data_event_from_text(const char *payload_text, bool *out_enabled)
{
    char normalized[160] = {0};
    size_t index = 0;

    if (payload_text == NULL || out_enabled == NULL) {
        return false;
    }

    for (index = 0; payload_text[index] != '\0' && index < (sizeof(normalized) - 1U); index++) {
        normalized[index] = (char)tolower((unsigned char)payload_text[index]);
    }
    normalized[index] = '\0';

    if (strstr(normalized, "datasimulationoff") != NULL ||
        strstr(normalized, "brewcomplete") != NULL ||
        strstr(normalized, "shot_done_success") != NULL ||
        strstr(normalized, "stopbrew") != NULL) {
        *out_enabled = false;
        return true;
    }

    if (strstr(normalized, "datasimulationon") != NULL ||
        strstr(normalized, "startbrew") != NULL) {
        *out_enabled = true;
        return true;
    }

    return false;
}

/**
 * @brief Mirror backend ON/OFF simulation events into the local toggle UI.
 *
 * @details Consumes each newly received backend text-event exactly once using
 * the communication snapshot event counter, then updates the local stream
 * state and checkable button presentation without sending another command.
 */
static void ui_sync_sim_data_toggle_from_peer_event(void)
{
    communication_snapshot_t comm_snapshot = {0};
    bool enabled = false;
    bool brew_event = false;
    bool shot_done_event = false;
    char normalized[160] = {0};
    size_t index = 0;

    if (communication_functions_get_snapshot(&comm_snapshot) != ESP_OK) {
        return;
    }

    if (comm_snapshot.last_received_text_event_count == s_ui.sim_data_last_peer_event_count) {
        return;
    }
    s_ui.sim_data_last_peer_event_count = comm_snapshot.last_received_text_event_count;

    if (!ui_try_parse_sim_data_event_from_text(comm_snapshot.last_received_text, &enabled)) {
        return;
    }

    for (index = 0;
         comm_snapshot.last_received_text[index] != '\0' && index < (sizeof(normalized) - 1U);
         index++) {
        normalized[index] = (char)tolower((unsigned char)comm_snapshot.last_received_text[index]);
    }
    normalized[index] = '\0';
    shot_done_event = (strstr(normalized, "shot_done_success") != NULL);
    brew_event = (strstr(normalized, "startbrew") != NULL) ||
                 (strstr(normalized, "brewcomplete") != NULL) ||
                 shot_done_event ||
                 (strstr(normalized, "stopbrew") != NULL);

    s_ui.sim_data_enabled = enabled;
    if (enabled && brew_event) {
        ui_hide_coffee_preparation_success_msgbox();
        ui_clear_plot_data();
    }
    if (!enabled) {
        if (brew_event && s_ui.plot_history_count > 0U) {
            s_ui.plot_history_frozen = true;
            ui_render_live_shot_plot();
        } else {
            ui_reset_sim_data_stream_state();
        }
    }
    if (brew_event) {
        s_ui.brewing = enabled;
        if (s_ui.brew_toggle_btn != NULL) {
            if (enabled) {
                lv_obj_add_state(s_ui.brew_toggle_btn, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(s_ui.brew_toggle_btn, LV_STATE_CHECKED);
            }
        }
        ui_update_header_status();
        ui_update_brew_widgets();
    }
    if (shot_done_event) {
        ui_show_coffee_preparation_success_msgbox();
    }
    if (s_ui.sim_data_toggle_btn != NULL) {
        s_ui.sim_data_toggle_syncing = true;
        if (enabled) {
            lv_obj_add_state(s_ui.sim_data_toggle_btn, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_ui.sim_data_toggle_btn, LV_STATE_CHECKED);
        }
        s_ui.sim_data_toggle_syncing = false;
    }
    ui_update_sim_data_status_label();
}

/**
 * @brief Queue a simulator data-stream toggle command in communication task.
 *
 * @details Converts a UI toggle action into a low-level DATA event and mirrors
 * the accepted state back into the screen controls.
 *
 * @param[in] enabled Requested stream state.
 * @param[in] sync_toggle_button `true` to force button checked-state sync.
 *
 * @return ESP_OK on success, otherwise an ESP_ERR_* code.
 */
static esp_err_t ui_request_sim_data_toggle(bool enabled, bool sync_toggle_button)
{
    bool previous_enabled = s_ui.sim_data_enabled;
    communication_data_event_t event_id = enabled
                                              ? COMMUNICATION_DATA_EVENT_SIMULATION_ON
                                              : COMMUNICATION_DATA_EVENT_SIMULATION_OFF;
    esp_err_t ret = communication_functions_request_data_event(event_id);

    if (ret == ESP_OK) {
        s_ui.sim_data_enabled = enabled;
        if (!enabled) {
            ui_reset_sim_data_stream_state();
        }
        ESP_LOGI(TAG,
                 "Simulate Data toggle requested: %s",
                 enabled ? "DataSimulationOn" : "DataSimulationOFF");
    } else {
        s_ui.sim_data_enabled = previous_enabled;
        ESP_LOGW(TAG,
                 "Simulate Data toggle request failed (%s): %s",
                 enabled ? "DataSimulationOn" : "DataSimulationOFF",
                 esp_err_to_name(ret));
    }

    if (sync_toggle_button && s_ui.sim_data_toggle_btn != NULL) {
        s_ui.sim_data_toggle_syncing = true;
        if (s_ui.sim_data_enabled) {
            lv_obj_add_state(s_ui.sim_data_toggle_btn, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_ui.sim_data_toggle_btn, LV_STATE_CHECKED);
        }
        s_ui.sim_data_toggle_syncing = false;
    }

    ui_update_sim_data_status_label();
    return ret;
}

/**
 * @brief Update custom X/Y axis labels for the Simulate Data chart.
 *
 * @details LVGL v9 chart widget does not expose the legacy axis-tick label
 * API, so the UI renders lightweight axis labels as regular LVGL labels.
 *
 * @param[in] packet_interval_us Time range represented by one full packet.
 * @param[in] y_axis_limit Positive symmetric Y limit in chart-scaled units.
 */
static void ui_update_sim_data_axis_labels(uint32_t packet_interval_us, int32_t y_axis_limit)
{
    lv_coord_t chart_x = 0;
    lv_coord_t chart_y = 0;
    lv_coord_t chart_w = 0;
    lv_coord_t chart_h = 0;
    uint32_t x_denominator = (UI_SIM_DATA_X_LABEL_COUNT > 1U) ? (UI_SIM_DATA_X_LABEL_COUNT - 1U) : 1U;
    uint32_t y_denominator = (UI_SIM_DATA_Y_LABEL_COUNT > 1U) ? (UI_SIM_DATA_Y_LABEL_COUNT - 1U) : 1U;

    if (s_ui.sim_data_chart == NULL) {
        return;
    }

    chart_x = lv_obj_get_x(s_ui.sim_data_chart);
    chart_y = lv_obj_get_y(s_ui.sim_data_chart);
    chart_w = lv_obj_get_width(s_ui.sim_data_chart);
    chart_h = lv_obj_get_height(s_ui.sim_data_chart);

    for (uint32_t index = 0; index < UI_SIM_DATA_X_LABEL_COUNT; index++) {
        lv_obj_t *label = s_ui.sim_data_x_axis_labels[index];
        uint32_t value_us = (packet_interval_us * index) / x_denominator;
        uint32_t value_ms_x10 = (value_us + 50U) / 100U;
        lv_coord_t pos_x = chart_x + (lv_coord_t)(((chart_w - 1) * (int32_t)index) / (int32_t)x_denominator);

        if (label == NULL) {
            continue;
        }

        snprintf(s_ui.sim_data_x_axis_label_text[index],
                 sizeof(s_ui.sim_data_x_axis_label_text[index]),
                 "%" PRIu32 ".%" PRIu32 "ms",
                 value_ms_x10 / 10U,
                 value_ms_x10 % 10U);
        lv_label_set_text_static(label, s_ui.sim_data_x_axis_label_text[index]);
        lv_obj_update_layout(label);
        pos_x -= lv_obj_get_width(label) / 2;
        lv_obj_set_pos(label, pos_x, chart_y + chart_h - 18);
    }

    for (uint32_t index = 0; index < UI_SIM_DATA_Y_LABEL_COUNT; index++) {
        lv_obj_t *label = s_ui.sim_data_y_axis_labels[index];
        int32_t numerator = (int32_t)(2U * index);
        int32_t value_scaled = y_axis_limit - (int32_t)((2 * y_axis_limit * numerator) / (2 * (int32_t)y_denominator));
        int32_t value_x100 = (int32_t)lroundf((float)value_scaled / UI_SIM_DATA_CHART_SCALE_FACTOR * 100.0f);
        lv_coord_t pos_y = chart_y + (lv_coord_t)(((chart_h - 1) * (int32_t)index) / (int32_t)y_denominator);

        if (label == NULL) {
            continue;
        }

        snprintf(s_ui.sim_data_y_axis_label_text[index],
                 sizeof(s_ui.sim_data_y_axis_label_text[index]),
                 "%ld.%02d",
                 (long)(value_x100 / 100),
                 abs((int)(value_x100 % 100)));
        lv_label_set_text_static(label, s_ui.sim_data_y_axis_label_text[index]);
        lv_obj_update_layout(label);
        pos_y -= lv_obj_get_height(label) / 2;
        lv_obj_set_pos(label, chart_x + 4, pos_y);
    }
}

/**
 * @brief Check whether a periodic UI refresh deadline has elapsed.
 *
 * @param[in] now_us Current monotonic timestamp in microseconds.
 * @param[in] last_refresh_us Previous refresh timestamp in microseconds.
 * @param[in] period_ms Target refresh period in milliseconds.
 *
 * @return `true` when refresh should run now.
 */
static bool ui_sim_data_refresh_due(int64_t now_us, int64_t last_refresh_us, uint32_t period_ms)
{
    int64_t period_us = (int64_t)period_ms * 1000LL;

    if (last_refresh_us <= 0 || now_us <= last_refresh_us) {
        return true;
    }

    return (now_us - last_refresh_us) >= period_us;
}

/**
 * @brief Render one simulator packet onto the fixed-size line chart.
 *
 * @details Each incoming packet carries DATA_SIZE_FLOATS samples. The chart is
 * updated with the latest packet and auto-ranges vertically around the current
 * signal.
 *
 * @param[in] packet Received simulator downlink packet.
 */
static void ui_plot_sim_data_packet(const data_downlink_packet_t *packet, uint32_t packet_interval_us)
{
    int32_t max_abs = 0;
    int64_t now_us = 0;
    bool refresh_axes = false;

    if (packet == NULL || s_ui.sim_data_chart == NULL || s_ui.sim_data_series == NULL) {
        return;
    }

    for (uint32_t index = 0; index < DATA_SIZE_FLOATS; index++) {
        int32_t scaled_value = (int32_t)lroundf(packet->f[index] * UI_SIM_DATA_CHART_SCALE_FACTOR);
        s_ui.sim_data_chart_y_values[index] = scaled_value;
        int32_t abs_value = (scaled_value >= 0) ? scaled_value : -scaled_value;
        if (abs_value > max_abs) {
            max_abs = abs_value;
        }
    }

    if (max_abs < 20) {
        max_abs = 20;
    }
    int32_t margin = max_abs / 8;
    if (margin < 20) {
        margin = 20;
    }
    int32_t y_axis_limit = max_abs + margin;
    uint32_t x_interval_us = (packet_interval_us > 0U) ? packet_interval_us : UI_SIM_DATA_DEFAULT_PACKET_INTERVAL_US;
    lv_coord_t x_axis_max_ms = (lv_coord_t)((x_interval_us + 500U) / 1000U);
    if (x_axis_max_ms < 1) {
        x_axis_max_ms = 1;
    }

    now_us = esp_timer_get_time();
    if (s_ui.sim_data_last_axis_refresh_us <= 0) {
        refresh_axes = true;
    } else if ((now_us - s_ui.sim_data_last_axis_refresh_us) >= UI_SIM_DATA_AXIS_REFRESH_PERIOD_US) {
        refresh_axes = true;
    } else if (s_ui.sim_data_last_axis_packet_interval_us == 0U) {
        refresh_axes = true;
    } else if (x_interval_us > (s_ui.sim_data_last_axis_packet_interval_us + 2000U) ||
               x_interval_us + 2000U < s_ui.sim_data_last_axis_packet_interval_us) {
        refresh_axes = true;
    } else if (abs(y_axis_limit - s_ui.sim_data_last_y_axis_limit) >= 40) {
        refresh_axes = true;
    }

    if (refresh_axes) {
        lv_chart_set_range(s_ui.sim_data_chart,
                           LV_CHART_AXIS_PRIMARY_X,
                           0,
                           x_axis_max_ms);
        lv_chart_set_range(s_ui.sim_data_chart,
                           LV_CHART_AXIS_PRIMARY_Y,
                           (lv_coord_t)(-y_axis_limit),
                           (lv_coord_t)(y_axis_limit));
        ui_update_sim_data_axis_labels(x_interval_us, y_axis_limit);
        s_ui.sim_data_last_axis_packet_interval_us = x_interval_us;
        s_ui.sim_data_last_y_axis_limit = y_axis_limit;
        s_ui.sim_data_last_axis_refresh_us = now_us;
    }

    lv_chart_refresh(s_ui.sim_data_chart);
}

/**
 * @brief Drain received simulator downlink packets from the shared FIFO.
 *
 * @details The communication task pushes binary downlink packets into the FIFO.
 * This UI helper drains as many packets as possible per poll tick, while chart
 * rendering remains rate-limited to a lower cadence to avoid UI bottlenecks.
 */
static void ui_process_sim_data_fifo(void)
{
    data_downlink_packet_t *packet = &s_ui.sim_data_work_packet;
    int64_t now_us = 0;
    uint32_t packet_interval_us = 0U;
    uint32_t drained_packets = 0U;
    bool have_packet = false;
    uint32_t sequence_gap_count = 0U;
    uint32_t first_gap_expected = 0U;
    uint32_t first_gap_actual = 0U;
    bool first_gap_valid = false;

    if (!s_ui.sim_data_enabled) {
        while (drained_packets < UI_SIM_DATA_MAX_DRAIN_PER_TICK &&
               data_downlink_pop(packet) == DATA_FIFO_OK) {
            drained_packets++;
            have_packet = true;
        }
        if (have_packet) {
            ui_ingest_home_metrics_from_packet(packet, UI_SIM_DATA_DEFAULT_PACKET_INTERVAL_US);
            ui_update_header_status();
            ui_update_brew_widgets();
        }
        return;
    }

    while (drained_packets < UI_SIM_DATA_MAX_DRAIN_PER_TICK &&
           data_downlink_pop(packet) == DATA_FIFO_OK) {
        drained_packets++;
        have_packet = true;

        if (s_ui.sim_data_last_seq_valid) {
            uint32_t expected_seq = s_ui.sim_data_last_seq + 1U;
            if (packet->seq != expected_seq) {
                sequence_gap_count++;
                if (!first_gap_valid) {
                    first_gap_expected = expected_seq;
                    first_gap_actual = packet->seq;
                    first_gap_valid = true;
                }
            }
        }

        s_ui.sim_data_last_seq = packet->seq;
        s_ui.sim_data_last_seq_valid = true;
        s_ui.sim_data_packets_received++;
        ui_plot_realtime_packet(packet,
                                (s_ui.sim_data_packet_interval_us > 0U)
                                    ? s_ui.sim_data_packet_interval_us
                                    : UI_SIM_DATA_DEFAULT_PACKET_INTERVAL_US);
    }

    if (!have_packet) {
        return;
    }

    now_us = esp_timer_get_time();
    if (sequence_gap_count > 0U &&
        (s_ui.sim_data_last_seq_gap_log_us <= 0 ||
         (now_us - s_ui.sim_data_last_seq_gap_log_us) >= UI_SIM_DATA_GAP_LOG_PERIOD_US)) {
        ESP_LOGW(TAG,
                 "Simulate Data sequence gaps: count=%" PRIu32 ", first expected=%" PRIu32 ", got=%" PRIu32,
                 sequence_gap_count,
                 first_gap_expected,
                 first_gap_actual);
        s_ui.sim_data_last_seq_gap_log_us = now_us;
    }

    packet_interval_us = s_ui.sim_data_packet_interval_us;
    if (s_ui.sim_data_last_packet_rx_us > 0 && now_us > s_ui.sim_data_last_packet_rx_us) {
        packet_interval_us = (uint32_t)(now_us - s_ui.sim_data_last_packet_rx_us);
        s_ui.sim_data_timing_valid = true;
    } else if (packet_interval_us == 0U) {
        packet_interval_us = UI_SIM_DATA_DEFAULT_PACKET_INTERVAL_US;
        s_ui.sim_data_timing_valid = false;
    }
    s_ui.sim_data_last_packet_rx_us = now_us;
    s_ui.sim_data_packet_interval_us = packet_interval_us;
    s_ui.sim_data_sample_period_us = packet_interval_us / UI_PLOT_SAMPLES_PER_CHANNEL;
    ui_ingest_home_metrics_from_packet(packet, packet_interval_us);
    ui_update_header_status();
    ui_update_brew_widgets();

    if (s_ui.sim_data_overlay != NULL &&
        ui_sim_data_refresh_due(now_us,
                                s_ui.sim_data_last_chart_refresh_us,
                                UI_SIM_DATA_CHART_REFRESH_PERIOD_MS)) {
        ui_plot_sim_data_packet(packet, packet_interval_us);
        s_ui.sim_data_last_chart_refresh_us = now_us;
    }
}

/**
 * @brief Sync toggle-off state when stream activity has stopped.
 *
 * @details This keeps the client toggle aligned with simulator-side OFF events
 * even if text event forwarding is delayed by transport timing.
 */
static void ui_sync_sim_data_toggle_from_stream_activity(void)
{
    int64_t now_us = 0;
    uint32_t dynamic_timeout_us = UI_SIM_DATA_STREAM_IDLE_TIMEOUT_US;

    if (!s_ui.sim_data_enabled || s_ui.sim_data_last_packet_rx_us <= 0) {
        return;
    }

    if (s_ui.sim_data_packet_interval_us > 0U) {
        uint32_t scaled_timeout = s_ui.sim_data_packet_interval_us * 3U;
        if (scaled_timeout > dynamic_timeout_us) {
            dynamic_timeout_us = scaled_timeout;
        }
    }

    now_us = esp_timer_get_time();
    if ((now_us - s_ui.sim_data_last_packet_rx_us) <= (int64_t)dynamic_timeout_us) {
        return;
    }

    s_ui.sim_data_enabled = false;
    if (s_ui.plot_history_count > 0U) {
        s_ui.plot_history_frozen = true;
        ui_render_live_shot_plot();
    } else {
        ui_reset_sim_data_stream_state();
    }
    s_ui.brewing = false;
    if (s_ui.brew_toggle_btn != NULL) {
        lv_obj_clear_state(s_ui.brew_toggle_btn, LV_STATE_CHECKED);
    }
    if (s_ui.sim_data_toggle_btn != NULL) {
        s_ui.sim_data_toggle_syncing = true;
        lv_obj_clear_state(s_ui.sim_data_toggle_btn, LV_STATE_CHECKED);
        s_ui.sim_data_toggle_syncing = false;
    }
    ui_update_header_status();
    ui_update_brew_widgets();
    ui_update_sim_data_status_label();
}

/**
 * @brief Poll the simulator downlink FIFO for graph updates.
 *
 * @details Runs at the configured UI poll cadence
 * (`UI_SIM_DATA_POLL_PERIOD_MS`) so FIFO draining remains responsive, while
 * chart and status redraw work is throttled to their own cadences.
 *
 * @param[in] timer LVGL timer payload.
 */
static void ui_sim_data_poll_timer_cb(lv_timer_t *timer)
{
    int64_t now_us = 0;

    (void)timer;
    ui_sync_sim_data_toggle_from_peer_event();
    ui_process_sim_data_fifo();
    ui_sync_sim_data_toggle_from_stream_activity();
    if (s_ui.sim_data_overlay != NULL) {
        now_us = esp_timer_get_time();
        if (ui_sim_data_refresh_due(now_us,
                                    s_ui.sim_data_last_status_refresh_us,
                                    UI_SIM_DATA_STATUS_REFRESH_PERIOD_MS)) {
            ui_update_sim_data_status_label();
        }
    }
}

/**
 * @brief Format loaded system constants into a tree-style text view.
 *
 * @details Converts the active constants snapshot into a readable outline so
 * the hierarchy is clear on screen without showing raw XML tags.
 *
 * @return Pointer to a static formatted XML buffer.
 */
static const char *ui_get_system_constants_pretty_text(void)
{
    const system_constants_data_t *constants = ui_get_constants();
    size_t used = 0;
    int written = 0;

    s_system_constants_pretty_text[0] = '\0';

    if (constants == NULL) {
        snprintf(s_system_constants_pretty_text,
                 sizeof(s_system_constants_pretty_text),
                 "System Constants\n    Unavailable");
        return s_system_constants_pretty_text;
    }

    written = snprintf(
        s_system_constants_pretty_text,
        sizeof(s_system_constants_pretty_text),
        "System Constants\n"
        "    Versions\n"
        "        Client Version: %s\n"
        "        Compatible Version: %s\n"
        "    Connection\n"
        "        Host: %s\n"
        "        Port: %s\n"
        "        Baud Rate: %d\n"
        "    Limits\n"
        "        Temperature: %d-%d C\n"
        "        Pressure: %d.%d-%d.%d bar\n"
        "        Flow: %d.%d-%d.%d ml/s\n"
        "    Profiles (%d)",
        constants->client_version,
        constants->compatible_client_version,
        constants->connection_host,
        constants->connection_port,
        constants->connection_baud_rate,
        constants->temperature_min_c,
        constants->temperature_max_c,
        constants->pressure_min_tenths / 10,
        abs(constants->pressure_min_tenths % 10),
        constants->pressure_max_tenths / 10,
        abs(constants->pressure_max_tenths % 10),
        constants->flow_min_tenths / 10,
        abs(constants->flow_min_tenths % 10),
        constants->flow_max_tenths / 10,
        abs(constants->flow_max_tenths % 10),
        constants->profile_count);

    if (written < 0) {
        s_system_constants_pretty_text[0] = '\0';
        return s_system_constants_pretty_text;
    }

    used = (size_t)written;
    if (used >= sizeof(s_system_constants_pretty_text)) {
        used = sizeof(s_system_constants_pretty_text) - 1U;
    }

    for (int i = 0; i < constants->profile_count && used + 1U < sizeof(s_system_constants_pretty_text); i++) {
        written = snprintf(
            s_system_constants_pretty_text + used,
            sizeof(s_system_constants_pretty_text) - used,
            "\n"
            "        Profile %d\n"
            "            Name: %s\n"
            "            Target Temperature: %d C\n"
            "            Preinfusion: %d s\n"
            "            Target Pressure: %d.%d bar\n"
            "            Target Flow: %d.%d ml/s",
            i + 1,
            constants->profiles[i].name,
            constants->profiles[i].target_temperature_c,
            constants->profiles[i].preinfusion_seconds,
            constants->profiles[i].target_pressure_tenths / 10,
            abs(constants->profiles[i].target_pressure_tenths % 10),
            constants->profiles[i].target_flow_tenths / 10,
            abs(constants->profiles[i].target_flow_tenths % 10));
        if (written < 0) {
            break;
        }
        used += (size_t)written;
        if (used >= sizeof(s_system_constants_pretty_text)) {
            used = sizeof(s_system_constants_pretty_text) - 1U;
            break;
        }
    }

    s_system_constants_pretty_text[used] = '\0';
    return s_system_constants_pretty_text;
}

/**
 * @brief Apply the user-selected clock value to the RTC.
 *
 * @details Reads the active roller selections, converts them into a calendar
 * time, writes the new value into the RTC, then closes the overlay on success.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_clock_set_done_event_cb(lv_event_t *e)
{
    (void)e;

    if (!s_ui.clock_set_day_roller ||
        !s_ui.clock_set_month_roller ||
        !s_ui.clock_set_year_roller ||
        !s_ui.clock_set_hour_roller ||
        !s_ui.clock_set_minute_roller) {
        return;
    }

    struct tm new_tm = {0};
    new_tm.tm_mday = lv_roller_get_selected(s_ui.clock_set_day_roller) + 1;
    new_tm.tm_mon = lv_roller_get_selected(s_ui.clock_set_month_roller);
    new_tm.tm_year = (UI_CLOCK_SET_YEAR_START + lv_roller_get_selected(s_ui.clock_set_year_roller)) - 1900;
    new_tm.tm_hour = lv_roller_get_selected(s_ui.clock_set_hour_roller);
    new_tm.tm_min = lv_roller_get_selected(s_ui.clock_set_minute_roller);
    new_tm.tm_sec = 0;
    new_tm.tm_isdst = -1;

    esp_err_t ret = peripherals_manager_set_rtc_time(&new_tm);
    if (ret == ESP_OK) {
        ui_update_clock_bar();
        ui_close_clock_overlay();
        ESP_LOGI(TAG, "RTC updated from Settings page");
    } else {
        ESP_LOGW(TAG, "RTC update failed from Settings page: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Open the dedicated clock-setting overlay from the Settings tab.
 *
 * @details Builds a full-screen modal UI with rollers for date and time
 * selection and a bottom `Done` action that writes the chosen value to the RTC.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_set_clock_event_cb(lv_event_t *e)
{
    (void)e;

    ui_close_clock_overlay();

    struct tm rtc_tm = {0};
    bool have_rtc_time = (peripherals_manager_get_rtc_time(&rtc_tm) == ESP_OK);
    if (!have_rtc_time) {
        rtc_tm.tm_mday = 1;
        rtc_tm.tm_mon = 0;
        rtc_tm.tm_year = 2026 - 1900;
        rtc_tm.tm_hour = 12;
        rtc_tm.tm_min = 0;
    }

    lv_obj_t *scr = lv_screen_active();
    s_ui.clock_set_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.clock_set_overlay);
    lv_obj_set_size(s_ui.clock_set_overlay, 800, 480);
    lv_obj_set_style_bg_color(s_ui.clock_set_overlay, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.clock_set_overlay, LV_OPA_COVER, 0);

    lv_obj_t *panel = lv_obj_create(s_ui.clock_set_overlay);
    lv_obj_set_size(panel, 760, 440);
    lv_obj_center(panel);
    ui_style_card(panel, UI_COLOR_PANEL);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Set Clock");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    s_ui.clock_set_day_roller = ui_create_clock_roller(panel, "Day", 20, 76);
    s_ui.clock_set_month_roller = ui_create_clock_roller(panel, "Month", 164, 76);
    s_ui.clock_set_year_roller = ui_create_clock_roller(panel, "Year", 308, 76);
    s_ui.clock_set_hour_roller = ui_create_clock_roller(panel, "Hour", 492, 76);
    s_ui.clock_set_minute_roller = ui_create_clock_roller(panel, "Minute", 620, 76);

    ui_set_roller_numeric_options(s_ui.clock_set_day_roller, 1, 31, 2);
    ui_set_roller_numeric_options(s_ui.clock_set_month_roller, 1, 12, 2);
    ui_set_roller_numeric_options(s_ui.clock_set_year_roller, UI_CLOCK_SET_YEAR_START, UI_CLOCK_SET_YEAR_END, 4);
    ui_set_roller_numeric_options(s_ui.clock_set_hour_roller, 0, 23, 2);
    ui_set_roller_numeric_options(s_ui.clock_set_minute_roller, 0, 59, 2);

    lv_roller_set_selected(s_ui.clock_set_day_roller, rtc_tm.tm_mday - 1, LV_ANIM_OFF);
    lv_roller_set_selected(s_ui.clock_set_month_roller, rtc_tm.tm_mon, LV_ANIM_OFF);
    int year_index = (rtc_tm.tm_year + 1900) - UI_CLOCK_SET_YEAR_START;
    if (year_index < 0) {
        year_index = 0;
    }
    if (year_index > (UI_CLOCK_SET_YEAR_END - UI_CLOCK_SET_YEAR_START)) {
        year_index = UI_CLOCK_SET_YEAR_END - UI_CLOCK_SET_YEAR_START;
    }
    lv_roller_set_selected(s_ui.clock_set_year_roller, year_index, LV_ANIM_OFF);
    lv_roller_set_selected(s_ui.clock_set_hour_roller, rtc_tm.tm_hour, LV_ANIM_OFF);
    lv_roller_set_selected(s_ui.clock_set_minute_roller, rtc_tm.tm_min, LV_ANIM_OFF);

    lv_obj_t *done_btn = lv_button_create(panel);
    lv_obj_set_size(done_btn, 720, 58);
    lv_obj_align(done_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    ui_style_action_button(done_btn);
    lv_obj_add_event_cb(done_btn, ui_clock_set_done_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *done_lbl = lv_label_create(done_btn);
    lv_label_set_text(done_lbl, "Done");
    ui_style_button_label(done_lbl);
    lv_obj_center(done_lbl);
}

/**
 * @brief Close the connection-info screen and return to Settings.
 *
 * @details Dismisses the modal connection-information overlay created from the
 * Settings page.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_connection_info_done_event_cb(lv_event_t *e)
{
    (void)e;
    ui_close_connection_info_overlay();
}

/**
 * @brief Start a low-level connection reset from the Connection Info overlay.
 *
 * @details Queues a `reset -> initialize -> connect` cycle in the dedicated
 * communication task, then refreshes the overlay to show the new state.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_connection_info_reset_event_cb(lv_event_t *e)
{
    (void)e;
    s_ui.connection_info_show_scan_results = false;
    communication_functions_request_reset();
    ui_update_connection_info_overlay_contents();
}

/**
 * @brief Start a Wi-Fi device scan from the Connection Info overlay.
 *
 * @details Clears the current text immediately, switches the overlay into scan
 * view, and lets the background communication task populate the list after the
 * fixed 10-second discovery window.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_connection_info_scan_event_cb(lv_event_t *e)
{
    (void)e;
    s_ui.connection_info_show_scan_results = true;
    if (s_ui.connection_info_details_label) {
        lv_label_set_text(s_ui.connection_info_details_label, "");
    }
    communication_functions_request_scan();
    ui_update_connection_info_overlay_contents();
}

/**
 * @brief Update Auto Reconnect policy from the Connection Info toggle.
 *
 * @details Mirrors the checkable LVGL button state into the communication task
 * so retry-limit exhaustion either auto-retries or follows the legacy path.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_connection_info_auto_reconnect_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    bool auto_reconnect_enabled = lv_obj_has_state(obj, LV_STATE_CHECKED);
    communication_functions_set_auto_reconnect_enabled(auto_reconnect_enabled);
    ui_update_connection_info_overlay_contents();
}

/**
 * @brief Close the system-constants screen and return to Settings.
 *
 * @details Dismisses the modal XML viewer overlay created from the Settings
 * page.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_system_constants_done_event_cb(lv_event_t *e)
{
    (void)e;
    ui_close_system_constants_overlay();
}

/**
 * @brief Open the connection-info overlay from the Settings tab.
 *
 * @details Shows the current connection snapshot, including IP address,
 * controller transport port, and a textual telemetry summary, plus a bottom
 * `Done` action that returns to the main UI.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_connection_info_event_cb(lv_event_t *e)
{
    (void)e;
    communication_snapshot_t comm_snapshot = {0};
    bool auto_reconnect_enabled = false;

    ui_close_connection_info_overlay();
    if (communication_functions_get_snapshot(&comm_snapshot) == ESP_OK) {
        auto_reconnect_enabled = comm_snapshot.auto_reconnect_enabled;
    }

    lv_obj_t *scr = lv_screen_active();
    s_ui.connection_info_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.connection_info_overlay);
    lv_obj_set_size(s_ui.connection_info_overlay, 800, 480);
    lv_obj_set_style_bg_color(s_ui.connection_info_overlay, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.connection_info_overlay, LV_OPA_COVER, 0);

    lv_obj_t *panel = lv_obj_create(s_ui.connection_info_overlay);
    lv_obj_set_size(panel, 760, 440);
    lv_obj_center(panel);
    ui_style_card(panel, UI_COLOR_PANEL);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Connection Info");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    lv_obj_t *reset_btn = lv_button_create(panel);
    lv_obj_set_size(reset_btn, 320, 52);
    lv_obj_align(reset_btn, LV_ALIGN_TOP_LEFT, 20, 60);
    ui_style_action_button(reset_btn);
    lv_obj_add_event_cb(reset_btn, ui_connection_info_reset_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "Reset Connection");
    ui_style_button_label(reset_lbl);
    lv_obj_center(reset_lbl);

    lv_obj_t *scan_btn = lv_button_create(panel);
    lv_obj_set_size(scan_btn, 320, 52);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -20, 60);
    ui_style_action_button(scan_btn);
    lv_obj_add_event_cb(scan_btn, ui_connection_info_scan_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, "Scan for Devices");
    ui_style_button_label(scan_lbl);
    lv_obj_center(scan_lbl);

    s_ui.connection_info_auto_reconnect_btn = ui_create_toggle_button(panel,
                                                                      "Auto Reconnect",
                                                                      auto_reconnect_enabled,
                                                                      20,
                                                                      126,
                                                                      ui_connection_info_auto_reconnect_event_cb);
    lv_obj_set_width(s_ui.connection_info_auto_reconnect_btn, 320);

    lv_obj_t *info_body = lv_obj_create(panel);
    lv_obj_set_size(info_body, 720, LV_SIZE_CONTENT);
    lv_obj_align(info_body, LV_ALIGN_TOP_MID, 0, 184);
    ui_style_card(info_body, UI_COLOR_CARD);
    lv_obj_set_scrollbar_mode(info_body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(info_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(info_body, 18, 0);
    lv_obj_set_style_pad_row(info_body, 14, 0);
    lv_obj_set_layout(info_body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(info_body, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *telemetry_card = lv_obj_create(info_body);
    lv_obj_set_width(telemetry_card, lv_pct(100));
    lv_obj_set_height(telemetry_card, LV_SIZE_CONTENT);
    ui_style_card(telemetry_card, UI_COLOR_CARD_ALT);
    lv_obj_clear_flag(telemetry_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(telemetry_card, 18, 0);

    s_ui.connection_info_details_label = lv_label_create(telemetry_card);
    lv_obj_set_width(s_ui.connection_info_details_label, 650);
    lv_label_set_long_mode(s_ui.connection_info_details_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_ui.connection_info_details_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.connection_info_details_label, lv_color_hex(UI_COLOR_TEXT), 0);
    s_ui.connection_info_show_scan_results = false;
    ui_update_connection_info_overlay_contents();

    lv_obj_t *done_btn = lv_button_create(panel);
    lv_obj_set_size(done_btn, 300, 58);
    ui_style_action_button(done_btn);
    lv_obj_add_event_cb(done_btn, ui_connection_info_done_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *done_lbl = lv_label_create(done_btn);
    lv_label_set_text(done_lbl, "Done");
    ui_style_button_label(done_lbl);
    lv_obj_center(done_lbl);

    lv_obj_update_layout(panel);
    lv_coord_t lowest_bottom = lv_obj_get_y(info_body) + lv_obj_get_height(info_body);
    lv_coord_t telemetry_bottom = lv_obj_get_y(info_body) + lv_obj_get_y(telemetry_card) + lv_obj_get_height(telemetry_card);
    if (telemetry_bottom > lowest_bottom) {
        lowest_bottom = telemetry_bottom;
    }
    lv_obj_set_pos(done_btn,
                   (lv_obj_get_width(panel) - lv_obj_get_width(done_btn)) / 2,
                   lowest_bottom + 10);
}

/**
 * @brief Open the System Constants XML viewer from the Settings tab.
 *
 * @details Builds a scrollable overlay that presents the embedded
 * `SystemConstants.xml` text in an indented hierarchy view, with a bottom
 * `Done` button positioned 10 pixels below the rendered text.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_system_constants_event_cb(lv_event_t *e)
{
    (void)e;

    ui_close_system_constants_overlay();

    lv_obj_t *scr = lv_screen_active();
    s_ui.system_constants_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.system_constants_overlay);
    lv_obj_set_size(s_ui.system_constants_overlay, 800, 480);
    lv_obj_set_style_bg_color(s_ui.system_constants_overlay, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.system_constants_overlay, LV_OPA_COVER, 0);

    lv_obj_t *panel = lv_obj_create(s_ui.system_constants_overlay);
    lv_obj_set_size(panel, 760, 440);
    lv_obj_center(panel);
    ui_style_card(panel, UI_COLOR_PANEL);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "System Constants");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    lv_obj_t *xml_body = lv_obj_create(panel);
    lv_obj_set_size(xml_body, 720, 340);
    lv_obj_align(xml_body, LV_ALIGN_TOP_MID, 0, 70);
    ui_style_card(xml_body, UI_COLOR_CARD);
    lv_obj_set_scrollbar_mode(xml_body, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(xml_body, 16, 0);
    lv_obj_set_scroll_dir(xml_body, LV_DIR_VER);

    lv_obj_t *xml_label = lv_label_create(xml_body);
    lv_label_set_long_mode(xml_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(xml_label, 680);
    lv_obj_set_style_text_font(xml_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(xml_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(xml_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(xml_label, ui_get_system_constants_pretty_text());

    lv_obj_t *done_btn = lv_button_create(xml_body);
    lv_obj_set_size(done_btn, 300, 58);
    ui_style_action_button(done_btn);
    lv_obj_add_event_cb(done_btn, ui_system_constants_done_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *done_lbl = lv_label_create(done_btn);
    lv_label_set_text(done_lbl, "Done");
    ui_style_button_label(done_lbl);
    lv_obj_center(done_lbl);

    lv_obj_update_layout(xml_body);
    lv_coord_t text_bottom = lv_obj_get_y(xml_label) + lv_obj_get_height(xml_label);
    lv_obj_set_pos(done_btn,
                   (lv_obj_get_width(xml_body) - lv_obj_get_width(done_btn)) / 2,
                   text_bottom + 10);
}

/**
 * @brief Build reusable page title inside content region.
 *
 * @param[in] parent Parent LVGL container.
 * @param[in] title Page title text.
 *
 * @return Created title label object.
 */
static lv_obj_t *ui_build_page_title(lv_obj_t *parent, const char *title)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 4);

    s_ui.connection_fault_label = lv_label_create(parent);
    lv_obj_set_style_text_font(s_ui.connection_fault_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.connection_fault_label, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_align(s_ui.connection_fault_label, LV_ALIGN_TOP_RIGHT, -8, 10);
    lv_obj_add_flag(s_ui.connection_fault_label, LV_OBJ_FLAG_HIDDEN);
    ui_update_connection_fault_indicator();
    return lbl;
}

/**
 * @brief Build the per-page live status summary block.
 *
 * @details Keeps status text inside the active tab content so the only
 * persistent out-of-tab element is the bottom clock bar.
 *
 * @param[in] parent Parent tab page container.
 */
static void ui_build_page_live_summary(lv_obj_t *parent)
{
    s_ui.page_status = lv_label_create(parent);
    lv_obj_set_style_text_font(s_ui.page_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.page_status, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.page_status, LV_ALIGN_TOP_LEFT, 8, 38);

    s_ui.page_runtime = lv_label_create(parent);
    lv_obj_set_style_text_font(s_ui.page_runtime, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.page_runtime, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(s_ui.page_runtime, LV_ALIGN_TOP_LEFT, 8, 60);
}

/**
 * @brief Create a button-style toggle control.
 *
 * @details Uses a checkable LVGL button so toggle actions share the same
 * pressed animation and color behavior as regular push buttons.
 *
 * @param[in] parent Parent container.
 * @param[in] text Button label text.
 * @param[in] checked Initial checked state.
 * @param[in] x X alignment offset from the top-left.
 * @param[in] y Y alignment offset from the top-left.
 * @param[in] cb Event callback for value changes.
 *
 * @return Created LVGL button object.
 */
static lv_obj_t *ui_create_toggle_button(lv_obj_t *parent,
                                         const char *text,
                                         bool checked,
                                         lv_coord_t x,
                                         lv_coord_t y,
                                         lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 220, 58);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    ui_style_action_button(btn);
    if (checked) {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    }

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    ui_style_button_label(lbl);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return btn;
}

/**
 * @brief Handle page navigation button click.
 *
 * @details Reads target page enum from event user data and triggers content
 * rebuild for the selected section.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_tabview_event_cb(lv_event_t *e);
void ui_screen_create(void);

/**
 * @brief Rebuild the currently selected tab page.
 *
 * @details Clears and redraws the active tab content while preserving shared UI
 * state stored in `s_ui`.
 */
static void ui_render_active_page(void);

/**
 * @brief Build the full main application UI after initialization completes.
 *
 * @details Creates the tabbed workflow layout for the upper 90% of the screen
 * plus the persistent bottom clock bar.
 */
static void ui_build_main_screen(void);

/**
 * @brief Handle brew toggle button state transitions.
 *
 * @details Toggles brew mode and resets shot timer when brew starts.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_brew_toggle_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    esp_err_t ret = ESP_OK;
    s_ui.brewing = lv_obj_has_state(obj, LV_STATE_CHECKED);
    if (s_ui.brewing) {
        char payload_text[96];
        s_ui.shot_s = 0;
        s_ui.brew_server_shot_valid = false;
        s_ui.brew_server_pressure_valid = false;
        s_ui.brew_server_water_valid = false;
        s_ui.brew_server_weight_valid = false;
        s_ui.brew_server_warmup_valid = false;
        s_ui.brew_server_temperature_valid = false;
        s_ui.brew_live_weight_g = 0.0f;
        s_ui.steaming = false;
        s_ui.brew_steam_indicator_on = false;
        if (s_ui.steam_toggle_btn) {
            lv_obj_clear_state(s_ui.steam_toggle_btn, LV_STATE_CHECKED);
        }
        ui_hide_coffee_preparation_success_msgbox();
        ui_clear_plot_data();
        s_ui.sim_data_enabled = true;
        snprintf(
            payload_text,
            sizeof(payload_text),
            "StartBrew;profile=%d;packet_interval_ms=100",
            s_ui.active_profile);
        ret = communication_functions_queue_data_text_command(payload_text);
        if (ret != ESP_OK) {
            s_ui.brewing = false;
            s_ui.sim_data_enabled = false;
            lv_obj_clear_state(obj, LV_STATE_CHECKED);
            ESP_LOGW(TAG, "StartBrew queue failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "brew ON");
        }
    } else {
        (void)communication_functions_queue_data_text_command("StopBrew");
        s_ui.sim_data_enabled = false;
        if (s_ui.plot_history_count > 0U) {
            s_ui.plot_history_frozen = true;
            ui_render_live_shot_plot();
        }
        s_ui.brew_server_shot_valid = false;
        s_ui.brew_server_pressure_valid = false;
        s_ui.brew_server_water_valid = false;
        s_ui.brew_server_weight_valid = false;
        s_ui.brew_server_warmup_valid = false;
        s_ui.brew_server_temperature_valid = false;
        ESP_LOGI(TAG, "brew OFF");
    }
    ui_update_header_status();
    ui_update_brew_widgets();
}

/**
 * @brief Handle steam toggle button state transitions.
 *
 * @details Toggles steam mode and clears brew mode when steam is activated.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_steam_toggle_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    s_ui.steaming = lv_obj_has_state(obj, LV_STATE_CHECKED);
    s_ui.brew_steam_indicator_on = s_ui.steaming;
    if (s_ui.steaming) {
        s_ui.brewing = false;
        s_ui.sim_data_enabled = false;
        (void)communication_functions_queue_data_text_command("StopBrew");
        if (s_ui.brew_toggle_btn) {
            lv_obj_clear_state(s_ui.brew_toggle_btn, LV_STATE_CHECKED);
        }
        ESP_LOGI(TAG, "steam ON");
    } else {
        ESP_LOGI(TAG, "steam OFF");
    }
    ui_update_header_status();
    ui_update_brew_widgets();
}

/**
 * @brief Handle Brew-page dropdown profile selection changes.
 *
 * @details Dropdown options are zero-based while profile indices in the data
 * model are one-based, so this callback remaps and applies the selection.
 */
static void ui_profile_dropdown_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    uint16_t selected = lv_dropdown_get_selected(obj);
    int profile = (int)selected + 1;
    lcd_controller_profile_catalog_t protocol_catalog = {0};

    if (lcd_controller_protocol_get_profile_catalog(&protocol_catalog) == ESP_OK &&
        selected < protocol_catalog.count &&
        protocol_catalog.entries[selected].profile_id > 0U) {
        profile = (int)protocol_catalog.entries[selected].profile_id;
    }

    if (s_ui.brew_profile_dropdown_syncing) {
        return;
    }
    if (profile < 1 || profile > ui_get_constants()->profile_count) {
        return;
    }

    ui_apply_profile_defaults(profile);
    s_ui.brew_shot_target_preview_g = ui_estimate_shot_target_preview_g(s_ui.active_profile, 30.0f);
    ui_update_home_labels();
    ui_update_header_status();
    ui_send_profile_selection_command(s_ui.startup_mode == UI_INIT_MODE_OFFLINE);
    ESP_LOGI(TAG, "profile %d selected from Profile Select dropdown", s_ui.active_profile);
}

/**
 * @brief Handle settings slider value changes.
 *
 * @details Updates target temperature or preinfusion duration and mirrors the
 * numeric value in adjacent labels.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    intptr_t id = (intptr_t)lv_event_get_user_data(e);

    if (id == 0) {
        s_ui.target_temp_c = (int)lv_slider_get_value(slider);
        if (s_ui.settings_target_value) {
            char txt[24];
            snprintf(txt, sizeof(txt), "%d C", s_ui.target_temp_c);
            lv_label_set_text(s_ui.settings_target_value, txt);
        }
    } else if (id == 1) {
        s_ui.preinf_s = (int)lv_slider_get_value(slider);
        if (s_ui.settings_preinf_value) {
            char txt[24];
            snprintf(txt, sizeof(txt), "%d s", s_ui.preinf_s);
            lv_label_set_text(s_ui.settings_preinf_value, txt);
        }
    }

    ui_update_home_labels();
    ui_update_header_status();
}

/**
 * @brief Construct the Live Shot page.
 *
 * @details Builds a Brew-style title header, a full-width 4-signal chart,
 * and bottom controls (autoscale toggles + Set Range popup).
 */
static void ui_build_page_live_shot(void)
{
    lv_obj_t *title = ui_build_page_title(s_ui.content, "Live Shot");
    lv_obj_t *controls = NULL;
    lv_obj_t *set_range_btn = NULL;
    lv_obj_t *set_range_lbl = NULL;
    lv_obj_t *legend_pressure = NULL;
    lv_obj_t *legend_weight = NULL;
    lv_obj_t *legend_flow = NULL;
    lv_obj_t *legend_temperature = NULL;
    lv_coord_t content_w = lv_obj_get_content_width(s_ui.content);
    lv_coord_t content_h = lv_obj_get_content_height(s_ui.content);
    const lv_coord_t axis_reserved_w = 160;
    lv_coord_t top_bottom = 0;
    lv_coord_t legend_y = 0;
    lv_coord_t chart_top = 0;
    lv_coord_t controls_h = 92;
    lv_coord_t controls_top = 0;
    lv_coord_t chart_w = 0;
    lv_coord_t chart_h = 0;
    const system_constants_data_t *constants = ui_get_constants();

    if (content_w <= 0) {
        content_w = lv_obj_get_width(s_ui.content);
    }
    if (content_h <= 0) {
        content_h = lv_obj_get_height(s_ui.content);
    }

    if (constants != NULL) {
        s_ui.plot_manual_pressure_max_bar = ui_plot_clamp_positive(
            constants->live_shot_pressure_max_bar, 1.0f, 20.0f, 12.0f);
        s_ui.plot_manual_weight_max_g = ui_plot_clamp_positive(
            constants->live_shot_weight_max_g, 10.0f, 100.0f, 60.0f);
        s_ui.plot_manual_flow_max_ml_s = ui_plot_clamp_positive(
            constants->live_shot_flow_max_ml_s, 1.0f, 30.0f, 5.0f);
        s_ui.plot_manual_temperature_max_c = ui_plot_clamp_positive(
            constants->live_shot_temperature_max_c, 30.0f, 105.0f, 105.0f);
        s_ui.plot_autoscale_pressure = constants->live_shot_autoscale_pressure;
        s_ui.plot_autoscale_weight = constants->live_shot_autoscale_weight;
        s_ui.plot_autoscale_flow = constants->live_shot_autoscale_flow;
        s_ui.plot_autoscale_temperature = constants->live_shot_autoscale_temperature;
    }

    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_letter_space(title, 5, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xBFDBFE), LV_STATE_PRESSED);
    lv_obj_set_y(title, lv_obj_get_y(title) - 14);
    ui_build_page_live_summary(s_ui.content);
    if (s_ui.page_status != NULL) {
        lv_obj_add_flag(s_ui.page_status, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.page_runtime != NULL) {
        lv_obj_set_style_text_font(s_ui.page_runtime, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_ui.page_runtime, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align_to(s_ui.page_runtime, title, LV_ALIGN_OUT_RIGHT_BOTTOM, 20, -6);
    }

    top_bottom = lv_obj_get_y(title) + lv_obj_get_height(title);
    if (s_ui.page_runtime != NULL) {
        lv_coord_t runtime_bottom = lv_obj_get_y(s_ui.page_runtime) + lv_obj_get_height(s_ui.page_runtime);
        if (runtime_bottom > top_bottom) {
            top_bottom = runtime_bottom;
        }
    }
    chart_top = top_bottom + 5;
    legend_y = chart_top - 28;
    controls_top = content_h - controls_h - 10;
    chart_w = content_w - axis_reserved_w + 10;
    chart_h = controls_top - chart_top;
    if (chart_w < 420) {
        chart_w = 420;
    }
    if (chart_h < 120) {
        chart_h = 120;
    }

    s_ui.plot_chart = lv_chart_create(s_ui.content);
    lv_obj_set_pos(s_ui.plot_chart, 0, chart_top);
    lv_obj_set_size(s_ui.plot_chart, chart_w, chart_h);
    lv_obj_set_style_bg_color(s_ui.plot_chart, lv_color_hex(UI_COLOR_CARD_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.plot_chart, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.plot_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.plot_chart, lv_color_hex(UI_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_ui.plot_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(s_ui.plot_chart, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_ui.plot_chart, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_width(s_ui.plot_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(s_ui.plot_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.plot_chart, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_chart_set_type(s_ui.plot_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_ui.plot_chart, UI_PLOT_POINT_COUNT);
    lv_chart_set_div_line_count(s_ui.plot_chart, 10, 20);
    lv_chart_set_range(s_ui.plot_chart, LV_CHART_AXIS_PRIMARY_Y, 0, UI_PLOT_NORMALIZED_MAX);

    s_ui.plot_pressure_series = lv_chart_add_series(
        s_ui.plot_chart,
        lv_color_hex(0xF97316),
        LV_CHART_AXIS_PRIMARY_Y);
    s_ui.plot_weight_series = lv_chart_add_series(
        s_ui.plot_chart,
        lv_color_hex(0xFACC15),
        LV_CHART_AXIS_PRIMARY_Y);
    s_ui.plot_flow_series = lv_chart_add_series(
        s_ui.plot_chart,
        lv_color_hex(0x38BDF8),
        LV_CHART_AXIS_PRIMARY_Y);
    s_ui.plot_temperature_series = lv_chart_add_series(
        s_ui.plot_chart,
        lv_color_hex(0xFB7185),
        LV_CHART_AXIS_PRIMARY_Y);

    for (uint32_t index = 0; index < UI_PLOT_POINT_COUNT; index++) {
        s_ui.plot_pressure_chart_y_values[index] = LV_CHART_POINT_NONE;
        s_ui.plot_weight_chart_y_values[index] = LV_CHART_POINT_NONE;
        s_ui.plot_flow_chart_y_values[index] = LV_CHART_POINT_NONE;
        s_ui.plot_temperature_chart_y_values[index] = LV_CHART_POINT_NONE;
    }
    if (s_ui.plot_pressure_series != NULL) {
        lv_chart_set_series_ext_y_array(
            s_ui.plot_chart,
            s_ui.plot_pressure_series,
            s_ui.plot_pressure_chart_y_values);
    }
    if (s_ui.plot_weight_series != NULL) {
        lv_chart_set_series_ext_y_array(
            s_ui.plot_chart,
            s_ui.plot_weight_series,
            s_ui.plot_weight_chart_y_values);
    }
    if (s_ui.plot_flow_series != NULL) {
        lv_chart_set_series_ext_y_array(
            s_ui.plot_chart,
            s_ui.plot_flow_series,
            s_ui.plot_flow_chart_y_values);
    }
    if (s_ui.plot_temperature_series != NULL) {
        lv_chart_set_series_ext_y_array(
            s_ui.plot_chart,
            s_ui.plot_temperature_series,
            s_ui.plot_temperature_chart_y_values);
    }

    for (uint32_t index = 0; index < UI_PLOT_X_LABEL_COUNT; index++) {
        s_ui.plot_x_axis_labels[index] = lv_label_create(s_ui.content);
        lv_obj_set_style_text_font(s_ui.plot_x_axis_labels[index], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_ui.plot_x_axis_labels[index], lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    }
    for (uint32_t index = 0; index < UI_PLOT_Y_LABEL_COUNT; index++) {
        s_ui.plot_pressure_y_axis_labels[index] = lv_label_create(s_ui.content);
        lv_obj_set_style_text_font(s_ui.plot_pressure_y_axis_labels[index], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_ui.plot_pressure_y_axis_labels[index], lv_color_hex(0xF97316), 0);

        s_ui.plot_weight_y_axis_labels[index] = lv_label_create(s_ui.content);
        lv_obj_set_style_text_font(s_ui.plot_weight_y_axis_labels[index], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_ui.plot_weight_y_axis_labels[index], lv_color_hex(0xFACC15), 0);

        s_ui.plot_flow_y_axis_labels[index] = lv_label_create(s_ui.content);
        lv_obj_set_style_text_font(s_ui.plot_flow_y_axis_labels[index], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_ui.plot_flow_y_axis_labels[index], lv_color_hex(0x38BDF8), 0);

        s_ui.plot_temperature_y_axis_labels[index] = lv_label_create(s_ui.content);
        lv_obj_set_style_text_font(s_ui.plot_temperature_y_axis_labels[index], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_ui.plot_temperature_y_axis_labels[index], lv_color_hex(0xFB7185), 0);
    }

    legend_pressure = lv_label_create(s_ui.content);
    lv_label_set_text(legend_pressure, "Pressure");
    lv_obj_set_style_text_font(legend_pressure, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(legend_pressure, lv_color_hex(0xF97316), 0);
    lv_obj_set_pos(legend_pressure, 36, legend_y);

    legend_weight = lv_label_create(s_ui.content);
    lv_label_set_text(legend_weight, "Weight");
    lv_obj_set_style_text_font(legend_weight, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(legend_weight, lv_color_hex(0xFACC15), 0);
    lv_obj_set_pos(legend_weight, 186, legend_y);

    legend_flow = lv_label_create(s_ui.content);
    lv_label_set_text(legend_flow, "Flow");
    lv_obj_set_style_text_font(legend_flow, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(legend_flow, lv_color_hex(0x38BDF8), 0);
    lv_obj_set_pos(legend_flow, 318, legend_y);

    legend_temperature = lv_label_create(s_ui.content);
    lv_label_set_text(legend_temperature, "Temperature");
    lv_obj_set_style_text_font(legend_temperature, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(legend_temperature, lv_color_hex(0xFB7185), 0);
    lv_obj_set_pos(legend_temperature, 428, legend_y);

    controls = lv_obj_create(s_ui.content);
    lv_obj_set_pos(controls, 0, controls_top);
    lv_obj_set_size(controls, content_w, controls_h);
    lv_obj_set_style_radius(controls, 10, 0);
    lv_obj_set_style_bg_color(controls, lv_color_hex(UI_COLOR_PANEL_ALT), 0);
    lv_obj_set_style_bg_opa(controls, LV_OPA_40, 0);
    lv_obj_set_style_border_width(controls, 1, 0);
    lv_obj_set_style_border_color(controls, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_pad_all(controls, 8, 0);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.plot_autoscale_pressure_checkbox = lv_checkbox_create(controls);
    lv_checkbox_set_text(s_ui.plot_autoscale_pressure_checkbox, "Auto Pressure");
    lv_obj_set_style_text_color(s_ui.plot_autoscale_pressure_checkbox, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(s_ui.plot_autoscale_pressure_checkbox, 16, 8);
    lv_obj_add_event_cb(s_ui.plot_autoscale_pressure_checkbox,
                        ui_plot_checkbox_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)UI_PLOT_CHECKBOX_AUTOSCALE_PRESSURE);
    if (s_ui.plot_autoscale_pressure) {
        lv_obj_add_state(s_ui.plot_autoscale_pressure_checkbox, LV_STATE_CHECKED);
    }

    s_ui.plot_autoscale_weight_checkbox = lv_checkbox_create(controls);
    lv_checkbox_set_text(s_ui.plot_autoscale_weight_checkbox, "Auto Weight");
    lv_obj_set_style_text_color(s_ui.plot_autoscale_weight_checkbox, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(s_ui.plot_autoscale_weight_checkbox, 232, 8);
    lv_obj_add_event_cb(s_ui.plot_autoscale_weight_checkbox,
                        ui_plot_checkbox_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)UI_PLOT_CHECKBOX_AUTOSCALE_WEIGHT);
    if (s_ui.plot_autoscale_weight) {
        lv_obj_add_state(s_ui.plot_autoscale_weight_checkbox, LV_STATE_CHECKED);
    }

    s_ui.plot_autoscale_flow_checkbox = lv_checkbox_create(controls);
    lv_checkbox_set_text(s_ui.plot_autoscale_flow_checkbox, "Auto Flow");
    lv_obj_set_style_text_color(s_ui.plot_autoscale_flow_checkbox, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(s_ui.plot_autoscale_flow_checkbox, 16, 48);
    lv_obj_add_event_cb(s_ui.plot_autoscale_flow_checkbox,
                        ui_plot_checkbox_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)UI_PLOT_CHECKBOX_AUTOSCALE_FLOW);
    if (s_ui.plot_autoscale_flow) {
        lv_obj_add_state(s_ui.plot_autoscale_flow_checkbox, LV_STATE_CHECKED);
    }

    s_ui.plot_autoscale_temperature_checkbox = lv_checkbox_create(controls);
    lv_checkbox_set_text(s_ui.plot_autoscale_temperature_checkbox, "Auto Temp");
    lv_obj_set_style_text_color(s_ui.plot_autoscale_temperature_checkbox, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_pos(s_ui.plot_autoscale_temperature_checkbox, 232, 48);
    lv_obj_add_event_cb(s_ui.plot_autoscale_temperature_checkbox,
                        ui_plot_checkbox_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)UI_PLOT_CHECKBOX_AUTOSCALE_TEMPERATURE);
    if (s_ui.plot_autoscale_temperature) {
        lv_obj_add_state(s_ui.plot_autoscale_temperature_checkbox, LV_STATE_CHECKED);
    }

    set_range_btn = lv_button_create(controls);
    lv_obj_set_size(set_range_btn, 128, 56);
    lv_obj_align(set_range_btn, LV_ALIGN_RIGHT_MID, -8, 0);
    ui_style_action_button(set_range_btn);
    lv_obj_add_event_cb(set_range_btn, ui_plot_set_range_button_event_cb, LV_EVENT_CLICKED, NULL);

    set_range_lbl = lv_label_create(set_range_btn);
    lv_label_set_text(set_range_lbl, "Set Range");
    ui_style_button_label(set_range_lbl);
    lv_obj_center(set_range_lbl);

    ui_render_live_shot_plot();
}

/**
 * @brief Construct brew operation page.
 *
 * @details Provides run-time controls for brew and steam operations.
 */
static void ui_build_page_brew(void)
{
    const system_constants_data_t *constants = ui_get_constants();
    char dropdown_options[512] = {0};
    size_t used = 0;

    lv_obj_t *brew_title = ui_build_page_title(s_ui.content, "BREW");
    lv_obj_set_style_text_font(brew_title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_letter_space(brew_title, 5, 0);
    lv_obj_set_style_text_color(brew_title, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_color(brew_title, lv_color_hex(0xBFDBFE), LV_STATE_PRESSED);
    lv_obj_set_y(brew_title, lv_obj_get_y(brew_title) - 14);
    ui_build_page_live_summary(s_ui.content);
    if (s_ui.page_status != NULL) {
        lv_obj_add_flag(s_ui.page_status, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.page_runtime != NULL) {
        lv_obj_set_style_text_font(s_ui.page_runtime, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_ui.page_runtime, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align_to(s_ui.page_runtime, brew_title, LV_ALIGN_OUT_RIGHT_BOTTOM, 20, -6);
    }

    s_ui.home_active_profile = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.home_active_profile, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.home_active_profile, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.home_active_profile, LV_ALIGN_TOP_LEFT, 20, 94);

    s_ui.brew_profile_dropdown = lv_dropdown_create(s_ui.content);
    lv_obj_set_size(s_ui.brew_profile_dropdown, 200, 44);
    lv_obj_align_to(
        s_ui.brew_profile_dropdown,
        s_ui.home_active_profile,
        LV_ALIGN_OUT_RIGHT_MID,
        16,
        0);
    lv_obj_set_style_radius(s_ui.brew_profile_dropdown, 16, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.brew_profile_dropdown, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.brew_profile_dropdown, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.brew_profile_dropdown, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.brew_profile_dropdown, lv_color_hex(UI_COLOR_CARD_ALT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(s_ui.brew_profile_dropdown, lv_color_hex(UI_COLOR_CARD_ALT), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(s_ui.brew_profile_dropdown, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ui.brew_profile_dropdown, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_ui.brew_profile_dropdown, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_ui.brew_profile_dropdown, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.brew_profile_dropdown, lv_color_hex(UI_COLOR_ACCENT_ALT), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(s_ui.brew_profile_dropdown, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_add_event_cb(s_ui.brew_profile_dropdown, ui_profile_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *profile_list = lv_dropdown_get_list(s_ui.brew_profile_dropdown);
    if (profile_list != NULL) {
        lv_obj_set_style_radius(profile_list, 12, LV_PART_MAIN);
        lv_obj_set_style_border_width(profile_list, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(profile_list, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN);
        lv_obj_set_style_bg_color(profile_list, lv_color_hex(UI_COLOR_CARD), LV_PART_MAIN);
        lv_obj_set_style_text_color(profile_list, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
        lv_obj_set_style_text_font(profile_list, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_bg_color(profile_list, lv_color_hex(UI_COLOR_ACCENT_ALT), LV_PART_SELECTED);
        lv_obj_set_style_text_color(profile_list, lv_color_hex(0xFFFFFF), LV_PART_SELECTED);
    }

    if (constants->profile_count > 0) {
        for (int i = 0; i < constants->profile_count && used + 1U < sizeof(dropdown_options); i++) {
            int written = snprintf(
                dropdown_options + used,
                sizeof(dropdown_options) - used,
                "%s%s",
                constants->profiles[i].name,
                (i + 1 < constants->profile_count) ? "\n" : "");
            if (written < 0) {
                break;
            }
            used += (size_t)written;
            if (used >= sizeof(dropdown_options)) {
                used = sizeof(dropdown_options) - 1U;
                break;
            }
        }
        lv_dropdown_set_options(s_ui.brew_profile_dropdown, dropdown_options);
    } else {
        lv_dropdown_set_options(s_ui.brew_profile_dropdown, "Profile 1");
    }

    lcd_controller_profile_catalog_t protocol_catalog = {0};
    if (lcd_controller_protocol_get_profile_catalog(&protocol_catalog) == ESP_OK &&
        protocol_catalog.count > 0U) {
        ui_refresh_profile_dropdown_from_protocol(&protocol_catalog);
    }
    ui_sync_brew_profile_dropdown();

    lv_obj_t *temp_title = lv_label_create(s_ui.content);
    lv_label_set_text(temp_title, "Temperature");
    lv_obj_set_style_text_font(temp_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(temp_title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(temp_title, LV_ALIGN_TOP_LEFT, 182, 40);
    lv_obj_add_flag(temp_title, LV_OBJ_FLAG_HIDDEN);

    s_ui.brew_temperature_value_label = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.brew_temperature_value_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.brew_temperature_value_label, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_align(s_ui.brew_temperature_value_label, LV_ALIGN_TOP_LEFT, 182, 60);
    lv_obj_add_flag(s_ui.brew_temperature_value_label, LV_OBJ_FLAG_HIDDEN);

    s_ui.brew_weight_value_label = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.brew_weight_value_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_ui.brew_weight_value_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.brew_weight_value_label, LV_ALIGN_TOP_LEFT, 20, 154);

    s_ui.brew_weight_scale_bar = lv_bar_create(s_ui.content);
    lv_obj_set_size(s_ui.brew_weight_scale_bar, 460, 28);
    lv_obj_align(s_ui.brew_weight_scale_bar, LV_ALIGN_TOP_LEFT, 20, 186);
    lv_bar_set_range(s_ui.brew_weight_scale_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_ui.brew_weight_scale_bar, lv_color_hex(UI_COLOR_PANEL_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.brew_weight_scale_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.brew_weight_scale_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.brew_weight_scale_bar, lv_color_hex(0x93C5FD), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.brew_weight_scale_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.brew_weight_scale_bar, lv_color_hex(0x60A5FA), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.brew_weight_scale_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    s_ui.brew_water_level_label = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.brew_water_level_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_ui.brew_water_level_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.brew_water_level_label, LV_ALIGN_TOP_LEFT, 20, 222);

    s_ui.brew_water_level_bar = lv_bar_create(s_ui.content);
    lv_obj_set_size(s_ui.brew_water_level_bar, 460, 28);
    lv_obj_align(s_ui.brew_water_level_bar, LV_ALIGN_TOP_LEFT, 20, 254);
    lv_bar_set_range(s_ui.brew_water_level_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_ui.brew_water_level_bar, lv_color_hex(UI_COLOR_PANEL_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.brew_water_level_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.brew_water_level_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.brew_water_level_bar, lv_color_hex(0x93C5FD), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.brew_water_level_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.brew_water_level_bar, lv_color_hex(0x60A5FA), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.brew_water_level_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    s_ui.brew_warmup_led = lv_led_create(s_ui.content);
    lv_obj_align(s_ui.brew_warmup_led, LV_ALIGN_BOTTOM_LEFT, 20, -10);
    lv_led_set_color(s_ui.brew_warmup_led, lv_color_hex(0xF59E0B));

    s_ui.brew_warmup_label = lv_label_create(s_ui.content);
    lv_label_set_text(s_ui.brew_warmup_label, "Warmup N/A");
    lv_obj_set_style_text_font(s_ui.brew_warmup_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.brew_warmup_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align_to(s_ui.brew_warmup_label, s_ui.brew_warmup_led, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    s_ui.brew_steam_led = lv_led_create(s_ui.content);
    lv_obj_align_to(s_ui.brew_steam_led, s_ui.brew_warmup_label, LV_ALIGN_OUT_RIGHT_MID, 28, 0);
    lv_led_set_color(s_ui.brew_steam_led, lv_color_hex(UI_COLOR_ACCENT));

    s_ui.brew_steam_label = lv_label_create(s_ui.content);
    lv_label_set_text(s_ui.brew_steam_label, "Steam OFF");
    lv_obj_set_style_text_font(s_ui.brew_steam_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.brew_steam_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align_to(s_ui.brew_steam_label, s_ui.brew_steam_led, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    s_ui.brew_uptime_label = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.brew_uptime_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.brew_uptime_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.brew_uptime_label, LV_ALIGN_BOTTOM_RIGHT, -30, -14);

    s_ui.brew_toggle_btn = ui_create_toggle_button(s_ui.content,
                                                   "Brew",
                                                   s_ui.brewing,
                                                   500,
                                                   92,
                                                   ui_brew_toggle_event_cb);
    lv_obj_align(s_ui.brew_toggle_btn, LV_ALIGN_TOP_RIGHT, -30, 92);

    s_ui.steam_toggle_btn = ui_create_toggle_button(s_ui.content,
                                                    "Steam",
                                                    s_ui.steaming,
                                                    500,
                                                    174,
                                                    ui_steam_toggle_event_cb);
    lv_obj_align(s_ui.steam_toggle_btn, LV_ALIGN_TOP_RIGHT, -30, 174);

    ui_update_home_labels();
}

/**
 * @brief Handle Settings-page backlight button presses.
 *
 * @details Toggles the current backlight state through the hardware layer.
 * A later touch anywhere on the panel will still wake the backlight again.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_backlight_toggle_event_cb(lv_event_t *e)
{
    (void)e;
    bool enabled = !hardware_get_backlight_enabled();
    esp_err_t ret = hardware_set_backlight_enabled(enabled);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "settings backlight %s", enabled ? "ON" : "OFF");
    } else {
        ESP_LOGW(TAG, "settings backlight change failed: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Close Simulate Data screen and stop simulator stream.
 *
 * @details Ensures the stream toggle is switched off before returning to the
 * Settings page so background traffic does not continue unintentionally.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_sim_data_done_event_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_request_sim_data_toggle(false, true);
    ui_close_sim_data_overlay();
}

/**
 * @brief Handle Simulate Data toggle state changes.
 *
 * @details Queues `DataSimulationOn` / `DataSimulationOFF` events for the
 * communication task and mirrors accepted state into the toggle button.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_sim_data_toggle_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(obj, LV_STATE_CHECKED);

    if (s_ui.sim_data_toggle_syncing) {
        return;
    }

    (void)ui_request_sim_data_toggle(enabled, true);
}

/**
 * @brief Open the Simulate Data service screen from Settings.
 *
 * @details Builds a dedicated overlay with a top simulation toggle and a live
 * graph that plots all DATA_SIZE_FLOATS samples from each simulator downlink
 * packet.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_simulate_data_event_cb(lv_event_t *e)
{
    (void)e;

    ui_close_sim_data_overlay();

    lv_obj_t *scr = lv_screen_active();
    s_ui.sim_data_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.sim_data_overlay);
    lv_obj_set_size(s_ui.sim_data_overlay, 800, 480);
    lv_obj_set_style_bg_color(s_ui.sim_data_overlay, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.sim_data_overlay, LV_OPA_COVER, 0);

    lv_obj_t *panel = lv_obj_create(s_ui.sim_data_overlay);
    lv_obj_set_size(panel, 760, 460);
    lv_obj_center(panel);
    ui_style_card(panel, UI_COLOR_PANEL);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Simulate Data");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    s_ui.sim_data_toggle_btn = ui_create_toggle_button(panel,
                                                       "Simulate Data",
                                                       s_ui.sim_data_enabled,
                                                       20,
                                                       60,
                                                       ui_sim_data_toggle_event_cb);
    lv_obj_set_width(s_ui.sim_data_toggle_btn, 300);

    s_ui.sim_data_status_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_ui.sim_data_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.sim_data_status_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_width(s_ui.sim_data_status_label, 360);
    lv_label_set_long_mode(s_ui.sim_data_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_ui.sim_data_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_ui.sim_data_status_label, LV_ALIGN_TOP_RIGHT, -20, 78);
    ui_update_sim_data_status_label();

    lv_obj_t *chart_card = lv_obj_create(panel);
    lv_obj_set_size(chart_card, 720, 168);
    lv_obj_align(chart_card, LV_ALIGN_TOP_MID, 0, 204);
    ui_style_card(chart_card, UI_COLOR_CARD);
    lv_obj_clear_flag(chart_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(chart_card, 12, 0);

    s_ui.sim_data_chart = lv_chart_create(chart_card);
    lv_obj_set_size(s_ui.sim_data_chart, 696, 144);
    lv_obj_center(s_ui.sim_data_chart);
    lv_obj_set_style_bg_color(s_ui.sim_data_chart, lv_color_hex(UI_COLOR_CARD_ALT), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.sim_data_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.sim_data_chart, lv_color_hex(UI_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_ui.sim_data_chart, 2, LV_PART_ITEMS);
    lv_chart_set_type(s_ui.sim_data_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_ui.sim_data_chart, DATA_SIZE_FLOATS);
    lv_chart_set_div_line_count(s_ui.sim_data_chart, 5, 8);
    lv_chart_set_range(s_ui.sim_data_chart, LV_CHART_AXIS_PRIMARY_Y, -200, 200);
    s_ui.sim_data_series = lv_chart_add_series(s_ui.sim_data_chart,
                                               lv_color_hex(UI_COLOR_ACCENT),
                                               LV_CHART_AXIS_PRIMARY_Y);
    if (s_ui.sim_data_series != NULL) {
        for (uint32_t index = 0; index < DATA_SIZE_FLOATS; index++) {
            s_ui.sim_data_chart_y_values[index] = 0;
        }
        lv_chart_set_series_ext_y_array(s_ui.sim_data_chart,
                                        s_ui.sim_data_series,
                                        s_ui.sim_data_chart_y_values);
    }
    for (uint32_t index = 0; index < UI_SIM_DATA_X_LABEL_COUNT; index++) {
        s_ui.sim_data_x_axis_labels[index] = lv_label_create(chart_card);
        lv_obj_set_style_text_font(s_ui.sim_data_x_axis_labels[index], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_ui.sim_data_x_axis_labels[index], lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
        snprintf(s_ui.sim_data_x_axis_label_text[index],
                 sizeof(s_ui.sim_data_x_axis_label_text[index]),
                 "0.0ms");
        lv_label_set_text_static(s_ui.sim_data_x_axis_labels[index],
                                 s_ui.sim_data_x_axis_label_text[index]);
    }
    for (uint32_t index = 0; index < UI_SIM_DATA_Y_LABEL_COUNT; index++) {
        s_ui.sim_data_y_axis_labels[index] = lv_label_create(chart_card);
        lv_obj_set_style_text_font(s_ui.sim_data_y_axis_labels[index], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_ui.sim_data_y_axis_labels[index], lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
        snprintf(s_ui.sim_data_y_axis_label_text[index],
                 sizeof(s_ui.sim_data_y_axis_label_text[index]),
                 "0.00");
        lv_label_set_text_static(s_ui.sim_data_y_axis_labels[index],
                                 s_ui.sim_data_y_axis_label_text[index]);
    }
    ui_update_sim_data_axis_labels(UI_SIM_DATA_DEFAULT_PACKET_INTERVAL_US, 200);
    lv_chart_refresh(s_ui.sim_data_chart);

    lv_obj_t *done_btn = lv_button_create(panel);
    lv_obj_set_size(done_btn, 300, 58);
    lv_obj_align(done_btn,
                 LV_ALIGN_TOP_MID,
                 0,
                 (lv_coord_t)(lv_obj_get_y(chart_card) + lv_obj_get_height(chart_card) + 20));
    ui_style_action_button(done_btn);
    lv_obj_add_event_cb(done_btn, ui_sim_data_done_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *done_lbl = lv_label_create(done_btn);
    lv_label_set_text(done_lbl, "Done");
    ui_style_button_label(done_lbl);
    lv_obj_center(done_lbl);
}

/**
 * @brief Return the client UI to the initialization state.
 *
 * @details Rebuilds the splash screen so the client visually returns to the
 * initialization state without issuing a hardware reset.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_reboot_client_event_cb(lv_event_t *e)
{
    (void)e;
    (void)ui_request_sim_data_toggle(false, false);
    s_ui.brewing = false;
    s_ui.steaming = false;
    s_ui.shot_s = 0;
    s_ui.reinit_requested = true;
    s_ui.init_mode_selection = UI_INIT_MODE_NONE;
    s_ui.startup_mode = UI_INIT_MODE_NONE;
    s_ui.init_failure_confirm_requested = false;
    ui_screen_create();
    ESP_LOGI(TAG, "client UI returned to initialization state");
}

/**
 * @brief Handle reset requests from the persistent error screen.
 *
 * @details Reuses the same client-side reinitialization path as the Settings
 * page reboot action so the application returns to the startup prompt without a
 * hardware reset.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_error_reset_event_cb(lv_event_t *e)
{
    ui_settings_reboot_client_event_cb(e);
}

/**
 * @brief Construct settings page.
 *
 * @details Offers quick numeric tuning of target temperature and preinfusion.
 */
static void ui_build_page_settings(void)
{
    ui_build_page_title(s_ui.content, "Settings");
    ui_build_page_live_summary(s_ui.content);

    lv_obj_t *temp_lbl = lv_label_create(s_ui.content);
    lv_label_set_text(temp_lbl, "Target Temperature");
    lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(temp_lbl, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(temp_lbl, LV_ALIGN_TOP_LEFT, 20, 92);

    s_ui.settings_target_slider = lv_slider_create(s_ui.content);
    lv_obj_set_size(s_ui.settings_target_slider, 540, 8);
    lv_obj_align(s_ui.settings_target_slider, LV_ALIGN_TOP_LEFT, 20, 132);
    ui_style_slider(s_ui.settings_target_slider);
    lv_slider_set_range(s_ui.settings_target_slider,
                        ui_get_constants()->temperature_min_c,
                        ui_get_constants()->temperature_max_c);
    lv_slider_set_value(s_ui.settings_target_slider, s_ui.target_temp_c, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.settings_target_slider, ui_settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)0);

    s_ui.settings_target_value = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.settings_target_value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.settings_target_value, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.settings_target_value, LV_ALIGN_TOP_RIGHT, -40, 118);

    lv_obj_t *pre_lbl = lv_label_create(s_ui.content);
    lv_label_set_text(pre_lbl, "Preinfusion");
    lv_obj_set_style_text_font(pre_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pre_lbl, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(pre_lbl, LV_ALIGN_TOP_LEFT, 20, 170);

    s_ui.settings_preinf_slider = lv_slider_create(s_ui.content);
    lv_obj_set_size(s_ui.settings_preinf_slider, 540, 8);
    lv_obj_align(s_ui.settings_preinf_slider, LV_ALIGN_TOP_LEFT, 20, 210);
    ui_style_slider(s_ui.settings_preinf_slider);
    lv_slider_set_range(s_ui.settings_preinf_slider, 0, 12);
    lv_slider_set_value(s_ui.settings_preinf_slider, s_ui.preinf_s, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.settings_preinf_slider, ui_settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)1);

    s_ui.settings_preinf_value = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.settings_preinf_value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.settings_preinf_value, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.settings_preinf_value, LV_ALIGN_TOP_RIGHT, -40, 196);

    if (s_ui.settings_target_value) {
        char txt[24];
        snprintf(txt, sizeof(txt), "%d C", s_ui.target_temp_c);
        lv_label_set_text(s_ui.settings_target_value, txt);
    }
    if (s_ui.settings_preinf_value) {
        char txt[24];
        snprintf(txt, sizeof(txt), "%d s", s_ui.preinf_s);
        lv_label_set_text(s_ui.settings_preinf_value, txt);
    }

    const lv_coord_t action_btn_width = 360;
    const lv_coord_t action_btn_height = 48;
    const lv_coord_t action_left_x = 20;
    const lv_coord_t action_right_x = 400;
    const lv_coord_t action_row1_y = 238;
    const lv_coord_t action_row2_y = 296;
    const lv_coord_t action_row3_y = 354;

    s_ui.settings_backlight_toggle = lv_button_create(s_ui.content);
    lv_obj_set_size(s_ui.settings_backlight_toggle, action_btn_width, action_btn_height);
    lv_obj_align(s_ui.settings_backlight_toggle, LV_ALIGN_TOP_LEFT, action_left_x, action_row1_y);
    ui_style_action_button(s_ui.settings_backlight_toggle);
    lv_obj_add_event_cb(s_ui.settings_backlight_toggle,
                        ui_settings_backlight_toggle_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    lv_obj_t *backlight_lbl = lv_label_create(s_ui.settings_backlight_toggle);
    lv_label_set_text(backlight_lbl, "Backlight");
    ui_style_button_label(backlight_lbl);
    lv_obj_center(backlight_lbl);

    lv_obj_t *set_clock_btn = lv_button_create(s_ui.content);
    lv_obj_set_size(set_clock_btn, action_btn_width, action_btn_height);
    lv_obj_align(set_clock_btn, LV_ALIGN_TOP_LEFT, action_right_x, action_row1_y);
    ui_style_action_button(set_clock_btn);
    lv_obj_add_event_cb(set_clock_btn, ui_settings_set_clock_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *set_clock_lbl = lv_label_create(set_clock_btn);
    lv_label_set_text(set_clock_lbl, "Set Clock");
    ui_style_button_label(set_clock_lbl);
    lv_obj_center(set_clock_lbl);

    lv_obj_t *connection_btn = lv_button_create(s_ui.content);
    lv_obj_set_size(connection_btn, action_btn_width, action_btn_height);
    lv_obj_align(connection_btn, LV_ALIGN_TOP_LEFT, action_left_x, action_row2_y);
    ui_style_action_button(connection_btn);
    lv_obj_add_event_cb(connection_btn, ui_settings_connection_info_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *connection_lbl = lv_label_create(connection_btn);
    lv_label_set_text(connection_lbl, "Connection Info");
    ui_style_button_label(connection_lbl);
    lv_obj_center(connection_lbl);

    lv_obj_t *reboot_btn = lv_button_create(s_ui.content);
    lv_obj_set_size(reboot_btn, action_btn_width, action_btn_height);
    lv_obj_align(reboot_btn, LV_ALIGN_TOP_LEFT, action_right_x, action_row2_y);
    ui_style_action_button(reboot_btn);
    lv_obj_add_event_cb(reboot_btn, ui_settings_reboot_client_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reboot_lbl = lv_label_create(reboot_btn);
    lv_label_set_text(reboot_lbl, "Reboot Client");
    ui_style_button_label(reboot_lbl);
    lv_obj_center(reboot_lbl);

    lv_obj_t *system_constants_btn = lv_button_create(s_ui.content);
    lv_obj_set_size(system_constants_btn, action_btn_width, action_btn_height);
    lv_obj_align(system_constants_btn, LV_ALIGN_TOP_LEFT, action_left_x, action_row3_y);
    ui_style_action_button(system_constants_btn);
    lv_obj_add_event_cb(system_constants_btn, ui_settings_system_constants_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *system_constants_lbl = lv_label_create(system_constants_btn);
    lv_label_set_text(system_constants_lbl, "System Constants");
    ui_style_button_label(system_constants_lbl);
    lv_obj_center(system_constants_lbl);

    lv_obj_t *simulate_data_btn = lv_button_create(s_ui.content);
    lv_obj_set_size(simulate_data_btn, action_btn_width, action_btn_height);
    lv_obj_align(simulate_data_btn, LV_ALIGN_TOP_LEFT, action_right_x, action_row3_y);
    ui_style_action_button(simulate_data_btn);
    lv_obj_add_event_cb(simulate_data_btn, ui_settings_simulate_data_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *simulate_data_lbl = lv_label_create(simulate_data_btn);
    lv_label_set_text(simulate_data_lbl, "Simulate Data");
    ui_style_button_label(simulate_data_lbl);
    lv_obj_center(simulate_data_lbl);
}

/**
 * @brief Rebuild content area for currently active page.
 *
 * @details Clears dynamic content widgets then draws selected workflow page.
 */
static void ui_render_active_page(void)
{
    ui_close_plot_range_overlay();

    s_ui.page_status = NULL;
    s_ui.page_runtime = NULL;
    s_ui.plot_chart = NULL;
    s_ui.plot_pressure_series = NULL;
    s_ui.plot_weight_series = NULL;
    s_ui.plot_flow_series = NULL;
    s_ui.plot_temperature_series = NULL;
    s_ui.plot_show_pressure_checkbox = NULL;
    s_ui.plot_show_weight_checkbox = NULL;
    s_ui.plot_show_flow_checkbox = NULL;
    s_ui.plot_show_temperature_checkbox = NULL;
    s_ui.plot_autoscale_pressure_checkbox = NULL;
    s_ui.plot_autoscale_weight_checkbox = NULL;
    s_ui.plot_autoscale_flow_checkbox = NULL;
    s_ui.plot_autoscale_temperature_checkbox = NULL;
    for (size_t i = 0; i < UI_PLOT_X_LABEL_COUNT; i++) {
        s_ui.plot_x_axis_labels[i] = NULL;
    }
    for (size_t i = 0; i < UI_PLOT_Y_LABEL_COUNT; i++) {
        s_ui.plot_pressure_y_axis_labels[i] = NULL;
        s_ui.plot_weight_y_axis_labels[i] = NULL;
        s_ui.plot_flow_y_axis_labels[i] = NULL;
        s_ui.plot_temperature_y_axis_labels[i] = NULL;
    }
    s_ui.brew_profile_dropdown = NULL;
    s_ui.home_active_profile = NULL;
    s_ui.home_target_label = NULL;
    s_ui.brew_temperature_value_label = NULL;
    s_ui.brew_water_level_label = NULL;
    s_ui.brew_water_level_bar = NULL;
    s_ui.brew_weight_value_label = NULL;
    s_ui.brew_weight_scale_bar = NULL;
    s_ui.brew_warmup_led = NULL;
    s_ui.brew_warmup_label = NULL;
    s_ui.brew_steam_led = NULL;
    s_ui.brew_steam_label = NULL;
    s_ui.brew_uptime_label = NULL;
    s_ui.coffee_preparation_success_msgbox = NULL;
    s_ui.coffee_preparation_success_ok_btn = NULL;

    switch (s_ui.active_page) {
    case UI_PAGE_BREW:
        s_ui.content = s_ui.tab_pages[UI_PAGE_BREW];
        lv_obj_clean(s_ui.content);
        s_ui.brew_toggle_btn = NULL;
        s_ui.steam_toggle_btn = NULL;
        ui_build_page_brew();
        break;
    case UI_PAGE_LIVE_SHOT:
        s_ui.content = s_ui.tab_pages[UI_PAGE_LIVE_SHOT];
        lv_obj_clean(s_ui.content);
        ui_build_page_live_shot();
        break;
    case UI_PAGE_SETTINGS:
        s_ui.content = s_ui.tab_pages[UI_PAGE_SETTINGS];
        lv_obj_clean(s_ui.content);
        s_ui.settings_target_slider = NULL;
        s_ui.settings_target_value = NULL;
        s_ui.settings_preinf_slider = NULL;
        s_ui.settings_preinf_value = NULL;
        s_ui.settings_backlight_toggle = NULL;
        ui_build_page_settings();
        break;
    default:
        s_ui.active_page = UI_PAGE_BREW;
        s_ui.content = s_ui.tab_pages[UI_PAGE_BREW];
        lv_obj_clean(s_ui.content);
        s_ui.brew_toggle_btn = NULL;
        s_ui.steam_toggle_btn = NULL;
        ui_build_page_brew();
        break;
    }

    ui_update_tab_style();
    ui_update_header_status();
    ui_update_header_runtime();
    ui_update_connection_fault_indicator();
}

/**
 * @brief Handle tab selection changes.
 *
 * @details Switches active page and triggers full content re-render.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_tabview_event_cb(lv_event_t *e)
{
    s_ui.active_page = (ui_page_t)lv_tabview_get_tab_active(lv_event_get_target(e));
    if (s_ui.active_page != UI_PAGE_BREW) {
        ui_hide_coffee_preparation_success_msgbox();
    }
    ui_render_active_page();
}

/**
 * @brief Periodic UI heartbeat updater.
 *
 * @details Refreshes local uptime (time since client power-on), active-page
 * status text, and the persistent bottom clock bar.
 *
 * @param[in] timer LVGL timer handle.
 */
static void ui_heartbeat_timer_cb(lv_timer_t *timer)
{
    communication_snapshot_t comm_snapshot = {0};
    int64_t uptime_us = esp_timer_get_time();

    (void)timer;
    if (uptime_us < 0) {
        uptime_us = 0;
    }
    s_ui.brew_uptime_minutes = (float)uptime_us / 60000000.0f;
    if (communication_functions_get_snapshot(&comm_snapshot) == ESP_OK) {
        (void)lcd_controller_protocol_process_peer_text_event(comm_snapshot.last_received_text_event_count,
                                                              comm_snapshot.last_received_text);
    }
    ui_update_header_status();
    ui_update_header_runtime();
    ui_update_brew_widgets();
    ui_update_clock_bar();
    ui_update_connection_fault_indicator();
    ui_update_connection_info_overlay_contents();
}

/**
 * @brief Build root layout and initialize workflow UI.
 *
 * @details Creates the top tab region for the main interface and the bottom
 * clock bar, then renders the default brew tab.
 */
static void ui_build_main_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);

    if (s_ui.root) {
        lv_obj_del(s_ui.root);
        s_ui.root = NULL;
    }

    s_ui.root = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.root);
    lv_obj_set_size(s_ui.root, 800, 480);
    lv_obj_center(s_ui.root);

    s_ui.tabview = lv_tabview_create(s_ui.root);
    lv_obj_set_size(s_ui.tabview, 800, UI_TABVIEW_HEIGHT);
    lv_obj_align(s_ui.tabview, LV_ALIGN_TOP_MID, 0, 0);
    lv_tabview_set_tab_bar_position(s_ui.tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(s_ui.tabview, 56);
    lv_obj_set_style_bg_color(s_ui.tabview, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.tabview, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.tabview, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_ui.tabview, ui_tabview_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(s_ui.tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(UI_COLOR_PANEL_ALT), LV_PART_MAIN);
    lv_obj_set_style_border_width(tab_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(tab_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tab_bar, 8, LV_PART_MAIN);

    s_ui.tab_pages[UI_PAGE_BREW] = lv_tabview_add_tab(s_ui.tabview, "Brew");
    s_ui.tab_pages[UI_PAGE_LIVE_SHOT] = lv_tabview_add_tab(s_ui.tabview, "Live Shot");
    s_ui.tab_pages[UI_PAGE_SETTINGS] = lv_tabview_add_tab(s_ui.tabview, "Settings");

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        lv_obj_t *tab = s_ui.tab_pages[i];
        lv_obj_set_style_bg_color(tab, lv_color_hex(UI_COLOR_PANEL), 0);
        lv_obj_set_style_border_width(tab, 0, 0);
        lv_obj_set_style_pad_all(tab, 12, 0);
        lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
    }
    /* Keep the tabview content scrollable so horizontal swipe gestures can
     * switch tabs with the built-in LVGL slide animation. */

    lv_obj_t *clock_bar = lv_obj_create(s_ui.root);
    lv_obj_set_size(clock_bar, 800, UI_CLOCK_BAR_HEIGHT);
    lv_obj_align(clock_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(clock_bar, 0, 0);
    lv_obj_set_style_bg_color(clock_bar, lv_color_hex(UI_COLOR_PANEL_ALT), 0);
    lv_obj_set_style_border_width(clock_bar, 0, 0);
    lv_obj_set_style_pad_all(clock_bar, 0, 0);

    s_ui.clock_label = lv_label_create(clock_bar);
    lv_obj_set_style_text_font(s_ui.clock_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_ui.clock_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.clock_label, LV_ALIGN_CENTER, 0, 0);

    ui_update_tab_style();

    if (ui_get_constants()->profile_count > 0) {
        ui_apply_profile_defaults(1);
        if (s_ui.startup_mode == UI_INIT_MODE_OFFLINE) {
            ui_send_profile_selection_command(true);
        }
    }
    s_ui.active_page = UI_PAGE_BREW;
    ui_render_active_page();
    lv_tabview_set_active(s_ui.tabview, UI_PAGE_BREW, LV_ANIM_OFF);
    ui_update_clock_bar();

    if (!s_ui.heartbeat_timer) {
        s_ui.heartbeat_timer = lv_timer_create(ui_heartbeat_timer_cb, 1000, NULL);
    }
    if (!s_ui.sim_data_poll_timer) {
        s_ui.sim_data_poll_timer = lv_timer_create(ui_sim_data_poll_timer_cb, UI_SIM_DATA_POLL_PERIOD_MS, NULL);
    }

    if (lcd_controller_protocol_init(ui_lcd_protocol_send_command_cb, NULL) == ESP_OK) {
        lcd_controller_protocol_set_profile_catalog_hook(ui_lcd_protocol_profile_catalog_hook, NULL);
        lcd_controller_protocol_set_brew_state_hook(ui_lcd_protocol_brew_state_hook, NULL);
        if (lcd_controller_protocol_initialize_hooks() != ESP_OK) {
            ESP_LOGW(TAG, "LCD protocol bootstrap command queue failed.");
        }
    } else {
        ESP_LOGW(TAG, "LCD protocol init failed; Brew communication hooks are disabled.");
    }

    ESP_LOGI(TAG, "UI screen created successfully");
}

/**
 * @brief Create the initialization splash screen before the main UI.
 *
 * @details Shows a centered `Initializing System...` message and a dynamic
 * status line while startup checks are running.
 */
void ui_screen_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_clean(scr);

    s_ui.root = NULL;
    s_ui.init_title_label = NULL;
    s_ui.init_status_label = NULL;
    s_ui.init_mode_prompt_label = NULL;
    s_ui.init_mode_countdown_label = NULL;
    s_ui.init_fail_label = NULL;
    s_ui.init_mode_yes_btn = NULL;
    s_ui.init_mode_no_btn = NULL;
    s_ui.init_confirm_btn = NULL;
    s_ui.error_screen = NULL;
    s_ui.tabview = NULL;
    s_ui.connection_fault_label = NULL;
    s_ui.clock_set_overlay = NULL;
    s_ui.connection_info_overlay = NULL;
    s_ui.system_constants_overlay = NULL;
    s_ui.sim_data_overlay = NULL;
    s_ui.connection_info_details_label = NULL;
    s_ui.connection_info_auto_reconnect_btn = NULL;
    s_ui.sim_data_toggle_btn = NULL;
    s_ui.sim_data_status_label = NULL;
    s_ui.sim_data_chart = NULL;
    s_ui.sim_data_series = NULL;
    s_ui.connection_info_show_scan_results = false;
    s_ui.sim_data_toggle_syncing = false;
    s_ui.plot_chart = NULL;
    s_ui.plot_pressure_series = NULL;
    s_ui.plot_weight_series = NULL;
    s_ui.plot_flow_series = NULL;
    s_ui.plot_temperature_series = NULL;
    s_ui.plot_show_pressure_checkbox = NULL;
    s_ui.plot_show_weight_checkbox = NULL;
    s_ui.plot_show_flow_checkbox = NULL;
    s_ui.plot_show_temperature_checkbox = NULL;
    s_ui.plot_autoscale_pressure_checkbox = NULL;
    s_ui.plot_autoscale_weight_checkbox = NULL;
    s_ui.plot_autoscale_flow_checkbox = NULL;
    s_ui.plot_autoscale_temperature_checkbox = NULL;
    s_ui.plot_range_overlay = NULL;
    s_ui.plot_range_pressure_value_label = NULL;
    s_ui.plot_range_weight_value_label = NULL;
    s_ui.plot_range_flow_value_label = NULL;
    s_ui.plot_range_temperature_value_label = NULL;
    s_ui.plot_range_pressure_roller = NULL;
    s_ui.plot_range_weight_roller = NULL;
    s_ui.plot_range_flow_roller = NULL;
    s_ui.plot_range_temperature_roller = NULL;
    for (size_t i = 0; i < UI_PLOT_X_LABEL_COUNT; i++) {
        s_ui.plot_x_axis_labels[i] = NULL;
    }
    for (size_t i = 0; i < UI_PLOT_Y_LABEL_COUNT; i++) {
        s_ui.plot_pressure_y_axis_labels[i] = NULL;
        s_ui.plot_weight_y_axis_labels[i] = NULL;
        s_ui.plot_flow_y_axis_labels[i] = NULL;
        s_ui.plot_temperature_y_axis_labels[i] = NULL;
    }
    s_ui.clock_set_day_roller = NULL;
    s_ui.clock_set_month_roller = NULL;
    s_ui.clock_set_year_roller = NULL;
    s_ui.clock_set_hour_roller = NULL;
    s_ui.clock_set_minute_roller = NULL;
    s_ui.content = NULL;
    s_ui.page_status = NULL;
    s_ui.page_runtime = NULL;
    s_ui.clock_label = NULL;
    s_ui.brew_toggle_btn = NULL;
    s_ui.steam_toggle_btn = NULL;
    s_ui.brew_profile_dropdown = NULL;
    s_ui.home_active_profile = NULL;
    s_ui.home_target_label = NULL;
    s_ui.brew_temperature_value_label = NULL;
    s_ui.brew_water_level_label = NULL;
    s_ui.brew_water_level_bar = NULL;
    s_ui.brew_weight_value_label = NULL;
    s_ui.brew_weight_scale_bar = NULL;
    s_ui.brew_warmup_led = NULL;
    s_ui.brew_warmup_label = NULL;
    s_ui.brew_steam_led = NULL;
    s_ui.brew_steam_label = NULL;
    s_ui.brew_uptime_label = NULL;
    s_ui.brew_profile_dropdown_syncing = false;
    s_ui.shot_s = 0;
    s_ui.brew_live_pressure_bar = 0.2f;
    s_ui.brew_live_temperature_c = 93.0f;
    s_ui.brew_live_water_level_pct = 92.0f;
    s_ui.brew_live_weight_g = 0.0f;
    s_ui.brew_shot_target_preview_g = 36.0f;
    s_ui.brew_warmup_on = true;
    s_ui.brew_steam_indicator_on = false;
    s_ui.brew_server_shot_valid = false;
    s_ui.brew_server_pressure_valid = false;
    s_ui.brew_server_water_valid = false;
    s_ui.brew_server_weight_valid = false;
    s_ui.brew_server_warmup_valid = false;
    s_ui.brew_server_temperature_valid = false;
    s_ui.brew_uptime_minutes = 0.0f;
    s_ui.sim_data_enabled = false;
    s_ui.sim_data_packets_received = 0;
    s_ui.sim_data_last_seq = 0;
    s_ui.sim_data_last_seq_valid = false;
    s_ui.sim_data_last_peer_event_count = 0;
    memset(&s_ui.sim_data_work_packet, 0, sizeof(s_ui.sim_data_work_packet));
    s_ui.sim_data_last_packet_rx_us = 0;
    s_ui.sim_data_packet_interval_us = 0;
    s_ui.sim_data_sample_period_us = 0;
    s_ui.sim_data_rx_fifo_packet_backlog_npackets = 0U;
    s_ui.sim_data_timing_valid = false;
    s_ui.sim_data_last_y_axis_limit = 0;
    s_ui.sim_data_last_axis_packet_interval_us = 0U;
    s_ui.sim_data_last_axis_refresh_us = 0;
    s_ui.sim_data_last_seq_gap_log_us = 0;
    s_ui.sim_data_last_chart_refresh_us = 0;
    s_ui.sim_data_last_status_refresh_us = 0;
    s_ui.plot_stream_started = false;
    s_ui.plot_history_count = 0U;
    s_ui.plot_history_frozen = false;
    memset(s_ui.plot_history_time_sec, 0, sizeof(s_ui.plot_history_time_sec));
    memset(s_ui.plot_history_pressure_bar, 0, sizeof(s_ui.plot_history_pressure_bar));
    memset(s_ui.plot_history_weight_g, 0, sizeof(s_ui.plot_history_weight_g));
    memset(s_ui.plot_history_flow_ml_s, 0, sizeof(s_ui.plot_history_flow_ml_s));
    memset(s_ui.plot_history_temperature_c, 0, sizeof(s_ui.plot_history_temperature_c));
    s_ui.init_mode_selection = UI_INIT_MODE_NONE;
    s_ui.startup_mode = UI_INIT_MODE_NONE;
    s_ui.init_mode_countdown_seconds = UI_INIT_MODE_DEFAULT_COUNTDOWN_SEC;
    s_ui.init_failure_confirm_requested = false;
    if (s_ui.init_mode_countdown_timer != NULL) {
        lv_timer_del(s_ui.init_mode_countdown_timer);
        s_ui.init_mode_countdown_timer = NULL;
    }

    lv_obj_t *splash = lv_obj_create(scr);
    lv_obj_remove_style_all(splash);
    lv_obj_set_size(splash, 800, 480);
    lv_obj_center(splash);
    lv_obj_set_style_bg_color(splash, lv_color_hex(UI_COLOR_BG), 0);

    s_ui.init_title_label = lv_label_create(splash);
    lv_label_set_text(s_ui.init_title_label, "Select Startup Mode");
    lv_obj_set_style_text_font(s_ui.init_title_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_ui.init_title_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.init_title_label, LV_ALIGN_CENTER, 0, -78);

    s_ui.init_mode_prompt_label = lv_label_create(splash);
    lv_obj_set_style_text_font(s_ui.init_mode_prompt_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.init_mode_prompt_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_align(s_ui.init_mode_prompt_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.init_mode_prompt_label, LV_ALIGN_CENTER, 0, -20);
    ui_update_init_mode_prompt_text();

    s_ui.init_mode_countdown_label = lv_label_create(splash);
    lv_obj_set_style_text_font(s_ui.init_mode_countdown_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.init_mode_countdown_label, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_text_align(s_ui.init_mode_countdown_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.init_mode_countdown_label, LV_ALIGN_CENTER, 0, 28);
    ui_update_init_mode_prompt_text();

    s_ui.init_mode_yes_btn = lv_button_create(splash);
    lv_obj_set_size(s_ui.init_mode_yes_btn, 220, 58);
    ui_style_action_button(s_ui.init_mode_yes_btn);
    lv_obj_add_event_cb(s_ui.init_mode_yes_btn,
                        ui_init_mode_select_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)UI_INIT_MODE_OFFLINE);
    lv_obj_align_to(s_ui.init_mode_yes_btn,
                    s_ui.init_mode_countdown_label,
                    LV_ALIGN_OUT_BOTTOM_MID,
                    -130,
                    10);

    lv_obj_t *yes_lbl = lv_label_create(s_ui.init_mode_yes_btn);
    lv_label_set_text(yes_lbl, "Offline");
    ui_style_button_label(yes_lbl);
    lv_obj_center(yes_lbl);

    s_ui.init_mode_no_btn = lv_button_create(splash);
    lv_obj_set_size(s_ui.init_mode_no_btn, 220, 58);
    ui_style_action_button(s_ui.init_mode_no_btn);
    lv_obj_add_event_cb(s_ui.init_mode_no_btn,
                        ui_init_mode_select_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)UI_INIT_MODE_ONLINE);
    lv_obj_align_to(s_ui.init_mode_no_btn,
                    s_ui.init_mode_countdown_label,
                    LV_ALIGN_OUT_BOTTOM_MID,
                    130,
                    10);

    lv_obj_t *no_lbl = lv_label_create(s_ui.init_mode_no_btn);
    lv_label_set_text(no_lbl, "Online");
    ui_style_button_label(no_lbl);
    lv_obj_center(no_lbl);

    s_ui.init_status_label = lv_label_create(splash);
    lv_label_set_text(s_ui.init_status_label, "Preparing startup checks...");
    lv_obj_set_style_text_font(s_ui.init_status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.init_status_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_align(s_ui.init_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.init_status_label, LV_ALIGN_CENTER, 0, 72);
    lv_obj_add_flag(s_ui.init_status_label, LV_OBJ_FLAG_HIDDEN);

    s_ui.init_fail_label = lv_label_create(splash);
    lv_label_set_text(s_ui.init_fail_label, "FAIL");
    lv_obj_set_style_text_font(s_ui.init_fail_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.init_fail_label, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_align(s_ui.init_fail_label, LV_ALIGN_CENTER, 0, 84);
    lv_obj_add_flag(s_ui.init_fail_label, LV_OBJ_FLAG_HIDDEN);

    s_ui.init_confirm_btn = lv_button_create(splash);
    lv_obj_set_size(s_ui.init_confirm_btn, 260, 58);
    ui_style_action_button(s_ui.init_confirm_btn);
    lv_obj_align(s_ui.init_confirm_btn, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_add_event_cb(s_ui.init_confirm_btn, ui_init_failure_confirm_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_ui.init_confirm_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *confirm_lbl = lv_label_create(s_ui.init_confirm_btn);
    lv_label_set_text(confirm_lbl, "Confirm");
    ui_style_button_label(confirm_lbl);
    lv_obj_center(confirm_lbl);

    s_ui.init_mode_countdown_timer = lv_timer_create(ui_init_mode_countdown_timer_cb, 1000, NULL);

    ESP_LOGI(TAG, "Initialization splash screen created");
}

/**
 * @brief Update the splash screen initialization status line.
 *
 * @details Replaces the dynamic status text shown underneath the initialization
 * title while startup checks are in progress.
 *
 * @param[in] status_text New text to show.
 */
void ui_screen_set_init_status(const char *status_text)
{
    if (!s_ui.init_status_label || status_text == NULL) {
        return;
    }

    lv_label_set_text(s_ui.init_status_label, status_text);
}

/**
 * @brief Mark the current initialization status as pass/fail.
 *
 * @details Highlights failed initialization text in red, shows a fail banner,
 * and restores the normal muted appearance when no failure is active.
 *
 * @param[in] failed `true` to show failure styling, `false` otherwise.
 */
void ui_screen_set_init_failed(bool failed)
{
    if (!s_ui.init_status_label) {
        return;
    }

    lv_obj_set_style_text_color(s_ui.init_status_label,
                                failed ? lv_palette_main(LV_PALETTE_RED)
                                       : lv_color_hex(UI_COLOR_TEXT_MUTED),
                                0);
    if (s_ui.init_fail_label) {
        if (failed) {
            lv_obj_clear_flag(s_ui.init_fail_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.init_fail_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief Transition the startup UI from prompt mode into progress mode.
 *
 * @details Hides the offline-mode selection controls and shows the normal
 * `Initializing System...` splash layout for the staged startup sequence.
 *
 * @param[in] mode Selected initialization mode.
 */
void ui_screen_begin_initialization(ui_init_mode_t mode)
{
    s_ui.startup_mode = mode;

    if (s_ui.init_mode_countdown_timer != NULL) {
        lv_timer_del(s_ui.init_mode_countdown_timer);
        s_ui.init_mode_countdown_timer = NULL;
    }

    if (s_ui.init_title_label) {
        lv_label_set_text(s_ui.init_title_label, "Initializing System...");
        lv_obj_align(s_ui.init_title_label, LV_ALIGN_CENTER, 0, -74);
    }

    if (s_ui.init_mode_prompt_label) {
        const char *mode_text = (mode == UI_INIT_MODE_OFFLINE)
                                    ? "Offline mode selected. Full initialization continues and failures are logged as warnings."
                                    : "Online mode selected. Full initialization continues and failures remain blocking errors.";
        lv_label_set_text(s_ui.init_mode_prompt_label, mode_text);
        lv_obj_align(s_ui.init_mode_prompt_label, LV_ALIGN_CENTER, 0, -30);
    }
    if (s_ui.init_mode_countdown_label) {
        lv_obj_add_flag(s_ui.init_mode_countdown_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_ui.init_mode_yes_btn) {
        lv_obj_add_flag(s_ui.init_mode_yes_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.init_mode_no_btn) {
        lv_obj_add_flag(s_ui.init_mode_no_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.init_status_label) {
        lv_obj_clear_flag(s_ui.init_status_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.init_fail_label) {
        lv_obj_add_flag(s_ui.init_fail_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.init_confirm_btn) {
        lv_obj_add_flag(s_ui.init_confirm_btn, LV_OBJ_FLAG_HIDDEN);
    }
    s_ui.init_failure_confirm_requested = false;
    ui_screen_set_init_failed(false);
}

/**
 * @brief Consume the operator's startup mode choice.
 *
 * @details Returns the stored prompt selection and clears it so startup begins
 * only once per prompt response.
 *
 * @return Selected initialization mode or `UI_INIT_MODE_NONE`.
 */
ui_init_mode_t ui_screen_take_init_mode_selection(void)
{
    ui_init_mode_t selected = s_ui.init_mode_selection;
    s_ui.init_mode_selection = UI_INIT_MODE_NONE;
    return selected;
}

/**
 * @brief Show the confirm action after initialization failure.
 *
 * @details Makes the bottom confirm button visible so the operator can
 * acknowledge a failed initialization result before moving to the error screen.
 */
void ui_screen_show_init_failure_confirm(void)
{
    if (s_ui.init_confirm_btn) {
        lv_obj_clear_flag(s_ui.init_confirm_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Consume the initialization-failure confirm request.
 *
 * @details Returns the current confirm flag and clears it so the main task can
 * react only once to each operator acknowledgement.
 *
 * @return `true` when the confirm button was pressed.
 */
bool ui_screen_take_init_failure_confirm(void)
{
    bool requested = s_ui.init_failure_confirm_requested;
    s_ui.init_failure_confirm_requested = false;
    return requested;
}

/**
 * @brief Replace the splash screen with the main workflow UI.
 *
 * @details Builds the normal tab-based UI after initialization checks and
 * controller startup handshake complete.
 */
void ui_screen_show_main(void)
{
    lv_obj_clean(lv_scr_act());
    s_ui.init_title_label = NULL;
    s_ui.init_mode_prompt_label = NULL;
    s_ui.init_status_label = NULL;
    s_ui.init_fail_label = NULL;
    s_ui.init_mode_yes_btn = NULL;
    s_ui.init_mode_no_btn = NULL;
    s_ui.init_confirm_btn = NULL;
    s_ui.error_screen = NULL;
    ui_build_main_screen();
}

/**
 * @brief Show the dedicated persistent error screen.
 *
 * @details Replaces the splash/main UI with a full-screen error view that
 * displays the provided summary and offers a single `Reset` action back to the
 * initialization prompt.
 *
 * @param[in] error_text Error summary to display.
 */
void ui_screen_show_error(const char *error_text)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_clean(scr);
    s_ui.init_title_label = NULL;
    s_ui.init_mode_prompt_label = NULL;
    s_ui.init_status_label = NULL;
    s_ui.init_fail_label = NULL;
    s_ui.init_mode_yes_btn = NULL;
    s_ui.init_mode_no_btn = NULL;
    s_ui.init_confirm_btn = NULL;

    s_ui.error_screen = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.error_screen);
    lv_obj_set_size(s_ui.error_screen, 800, 480);
    lv_obj_center(s_ui.error_screen);
    lv_obj_set_style_bg_color(s_ui.error_screen, lv_color_hex(UI_COLOR_BG), 0);

    lv_obj_t *title = lv_label_create(s_ui.error_screen);
    lv_label_set_text(title, "Error");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *error_box = lv_obj_create(s_ui.error_screen);
    lv_obj_set_size(error_box, 720, 260);
    lv_obj_align(error_box, LV_ALIGN_TOP_MID, 0, 86);
    ui_style_card(error_box, UI_COLOR_CARD);
    lv_obj_set_scrollbar_mode(error_box, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(error_box, 18, 0);

    lv_obj_t *error_label = lv_label_create(error_box);
    lv_obj_set_width(error_label, 680);
    lv_label_set_long_mode(error_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(error_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(error_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_text(error_label, (error_text != NULL) ? error_text : "Unknown initialization error.");

    lv_obj_t *reset_btn = lv_button_create(s_ui.error_screen);
    lv_obj_set_size(reset_btn, 260, 58);
    lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_MID, 0, -26);
    ui_style_action_button(reset_btn);
    lv_obj_add_event_cb(reset_btn, ui_error_reset_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "Reset");
    ui_style_button_label(reset_lbl);
    lv_obj_center(reset_lbl);
}

/**
 * @brief Consume any pending client re-initialization request.
 *
 * @details Returns the current request flag and clears it so the application
 * loop can rerun initialization exactly once per button press.
 *
 * @return `true` when a re-initialization request was pending.
 */
bool ui_screen_take_reinit_request(void)
{
    bool requested = s_ui.reinit_requested;
    s_ui.reinit_requested = false;
    return requested;
}

/**
 * @brief Synchronize the Settings backlight control with runtime state.
 *
 * @details The Settings control is a momentary push button, so there is no
 * persistent checked state to synchronize. The hook is kept so runtime
 * backlight logic can call a single UI function without needing to know the
 * current control style.
 *
 * @param[in] enabled `true` when the backlight is on, `false` when off.
 */
void ui_screen_set_backlight_toggle_state(bool enabled)
{
    (void)enabled;
}
