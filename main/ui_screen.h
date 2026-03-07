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

#ifdef __cplusplus
}
#endif

