/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the UI screen with toggle button
 *
 * @details Builds the active screen object tree for the brew control user
 * interface and registers control callbacks.
 */
void ui_screen_create(void);

#ifdef __cplusplus
}
#endif

