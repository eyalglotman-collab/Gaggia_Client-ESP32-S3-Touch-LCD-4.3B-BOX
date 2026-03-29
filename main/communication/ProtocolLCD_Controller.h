/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "DataStructuresLCD_Controller.h"
#include "SystemStatesLCD_Controller.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transport callback used by the LCD protocol layer to send DATA text.
 *
 * @param[in] payload_text Protocol text payload.
 * @param[in] user_ctx User context pointer set during initialization.
 *
 * @return
 *      - ESP_OK: Command accepted by transport layer
 *      - Any other `esp_err_t`: Command could not be queued/sent
 */
typedef esp_err_t (*lcd_controller_send_command_fn_t)(const char *payload_text, void *user_ctx);

/**
 * @brief Optional callback invoked when a new profile catalog is parsed.
 */
typedef void (*lcd_controller_profile_catalog_hook_t)(const lcd_controller_profile_catalog_t *catalog,
                                                      void *user_ctx);

/**
 * @brief Optional callback invoked when Brew/Home state is published.
 */
typedef void (*lcd_controller_brew_state_hook_t)(const lcd_controller_brew_home_state_t *state,
                                                 void *user_ctx);

/**
 * @brief Initialize protocol context and register transport send callback.
 */
esp_err_t lcd_controller_protocol_init(lcd_controller_send_command_fn_t send_command_cb,
                                       void *send_command_user_ctx);

/**
 * @brief Send bootstrap protocol commands (`Init` + `ProfileCatalogGet`).
 */
esp_err_t lcd_controller_protocol_initialize_hooks(void);

/**
 * @brief Decode one peer text-event snapshot and update protocol caches.
 */
esp_err_t lcd_controller_protocol_process_peer_text_event(uint32_t event_count,
                                                          const char *payload_text);

/**
 * @brief Publish latest Brew/Home state into protocol cache and invoke hook.
 */
esp_err_t lcd_controller_protocol_publish_brew_home_state(const lcd_controller_brew_home_state_t *state);

/**
 * @brief Read latest Brew/Home protocol cache snapshot.
 */
esp_err_t lcd_controller_protocol_get_brew_home_state(lcd_controller_brew_home_state_t *out_state);

/**
 * @brief Read latest profile catalog protocol cache snapshot.
 */
esp_err_t lcd_controller_protocol_get_profile_catalog(lcd_controller_profile_catalog_t *out_catalog);

/**
 * @brief Read current protocol link-state snapshot.
 */
lcd_controller_protocol_link_state_t lcd_controller_protocol_get_link_state(void);

/**
 * @brief Register optional profile-catalog update hook.
 */
void lcd_controller_protocol_set_profile_catalog_hook(lcd_controller_profile_catalog_hook_t hook_cb,
                                                      void *hook_user_ctx);

/**
 * @brief Register optional Brew/Home-state update hook.
 */
void lcd_controller_protocol_set_brew_state_hook(lcd_controller_brew_state_hook_t hook_cb,
                                                 void *hook_user_ctx);

#ifdef __cplusplus
}
#endif
