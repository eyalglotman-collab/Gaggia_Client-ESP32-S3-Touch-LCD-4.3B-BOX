/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialization startup mode chosen by the operator.
 *
 * @details Captures whether startup should continue in normal online mode,
 * simulated offline mode, or remain waiting for the operator to choose.
 */
typedef enum {
    UI_INIT_MODE_NONE = 0,
    UI_INIT_MODE_ONLINE,
    UI_INIT_MODE_OFFLINE,
} ui_init_mode_t;

/**
 * @brief Create the initialization splash screen.
 *
 * @details Builds the initialization splash screen that is shown while the
 * application performs startup checks and peripheral bring-up.
 */
void ui_screen_create(void);

/**
 * @brief Update the initialization progress text.
 *
 * @details Replaces the dynamic status line shown under the static
 * `Initializing System...` splash title.
 *
 * @param[in] status_text New initialization status text.
 */
void ui_screen_set_init_status(const char *status_text);

/**
 * @brief Mark the current initialization status as pass/fail.
 *
 * @details Updates the initialization splash styling so failed steps are shown
 * in red with an explicit fail indication while non-failed steps use the normal
 * muted informational styling.
 *
 * @param[in] failed `true` to show failure styling, `false` for normal styling.
 */
void ui_screen_set_init_failed(bool failed);

/**
 * @brief Begin the staged initialization progress view.
 *
 * @details Hides the startup mode prompt, shows the `Initializing System...`
 * splash content, and notes whether the selected run mode is online or offline.
 *
 * @param[in] mode Operator-selected initialization mode.
 */
void ui_screen_begin_initialization(ui_init_mode_t mode);

/**
 * @brief Consume the operator's startup mode selection.
 *
 * @details Returns the current prompt selection once and clears it so the app
 * can begin initialization exactly once for each prompt interaction.
 *
 * @return The selected initialization mode, or `UI_INIT_MODE_NONE`.
 */
ui_init_mode_t ui_screen_take_init_mode_selection(void);

/**
 * @brief Show a confirm button after initialization fails.
 *
 * @details Displays a bottom confirmation action so the operator can acknowledge
 * the startup failure before the client switches to the dedicated error screen.
 */
void ui_screen_show_init_failure_confirm(void);

/**
 * @brief Consume an initialization-failure confirm request.
 *
 * @details Returns `true` once after the operator presses the confirm button on
 * the failed initialization screen.
 *
 * @return `true` when a confirm press is pending.
 */
bool ui_screen_take_init_failure_confirm(void);

/**
 * @brief Show the persistent error screen.
 *
 * @details Replaces the initialization splash with an error view containing the
 * provided failure text and a single `Reset` action that returns to the
 * initialization prompt flow.
 *
 * @param[in] error_text Error summary to display.
 */
void ui_screen_show_error(const char *error_text);

/**
 * @brief Replace the splash screen with the main application UI.
 *
 * @details Builds the full tab-based UI after initialization checks and
 * external controller handshake processing complete.
 */
void ui_screen_show_main(void);

/**
 * @brief Consume a pending client re-initialization request.
 *
 * @details Returns `true` once after the Settings-page `Reboot Client` action
 * has requested that the app rerun its initialization flow.
 *
 * @return `true` if a re-initialization was requested since the last call.
 */
bool ui_screen_take_reinit_request(void);

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
void ui_screen_set_backlight_toggle_state(bool enabled);

#ifdef __cplusplus
}
#endif

