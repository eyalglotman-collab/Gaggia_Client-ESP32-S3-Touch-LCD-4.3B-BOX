/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ProtocolLCD_Controller.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LCD_CONTROLLER_PROFILE_ITEMS_MAX_TEXT (320U)

static const char *TAG = "LCDProtocol";

typedef struct {
    SemaphoreHandle_t mutex;
    bool initialized;
    bool hooks_requested;
    uint32_t last_event_count;
    lcd_controller_protocol_link_state_t link_state;
    lcd_controller_profile_catalog_t profile_catalog;
    lcd_controller_brew_home_state_t brew_state;
    lcd_controller_send_command_fn_t send_command_cb;
    void *send_command_user_ctx;
    lcd_controller_profile_catalog_hook_t profile_hook_cb;
    void *profile_hook_user_ctx;
    lcd_controller_brew_state_hook_t brew_hook_cb;
    void *brew_hook_user_ctx;
} lcd_controller_protocol_context_t;

static lcd_controller_protocol_context_t s_protocol = {
    .mutex = NULL,
    .initialized = false,
    .hooks_requested = false,
    .last_event_count = 0U,
    .link_state = LCD_CONTROLLER_PROTOCOL_STATE_UNINITIALIZED,
    .profile_catalog = {0},
    .brew_state = {0},
    .send_command_cb = NULL,
    .send_command_user_ctx = NULL,
    .profile_hook_cb = NULL,
    .profile_hook_user_ctx = NULL,
    .brew_hook_cb = NULL,
    .brew_hook_user_ctx = NULL,
};

/**
 * @brief Compare a prefix in a case-insensitive way.
 */
static bool lcd_controller_starts_with_ignore_case(const char *text, const char *prefix)
{
    size_t index = 0U;

    if (text == NULL || prefix == NULL) {
        return false;
    }

    while (prefix[index] != '\0') {
        if (text[index] == '\0') {
            return false;
        }

        if (tolower((unsigned char)text[index]) != tolower((unsigned char)prefix[index])) {
            return false;
        }
        index++;
    }

    return true;
}

/**
 * @brief Extract one semicolon-delimited `key=value` token value.
 */
static bool lcd_controller_extract_payload_value(const char *payload_text,
                                                 const char *key_text,
                                                 char *out_value,
                                                 size_t out_value_size)
{
    const size_t key_len = (key_text != NULL) ? strlen(key_text) : 0U;
    const char *cursor = payload_text;

    if (payload_text == NULL || key_text == NULL || out_value == NULL || out_value_size == 0U || key_len == 0U) {
        return false;
    }

    out_value[0] = '\0';

    while (cursor != NULL && *cursor != '\0') {
        const bool token_start = (cursor == payload_text) || (*(cursor - 1) == ';');

        if (token_start &&
            strncmp(cursor, key_text, key_len) == 0 &&
            cursor[key_len] == '=') {
            const char *value_start = cursor + key_len + 1U;
            const char *value_end = strchr(value_start, ';');
            size_t value_len = 0U;

            if (value_end == NULL) {
                value_end = value_start + strlen(value_start);
            }

            value_len = (size_t)(value_end - value_start);
            if (value_len >= out_value_size) {
                value_len = out_value_size - 1U;
            }

            if (value_len > 0U) {
                memcpy(out_value, value_start, value_len);
            }
            out_value[value_len] = '\0';
            return true;
        }

        cursor = strchr(cursor, ';');
        if (cursor != NULL) {
            cursor++;
        }
    }

    return false;
}

/**
 * @brief Parse one `id:name` profile token and write it into catalog entry.
 */
static bool lcd_controller_parse_profile_item(const char *item_text,
                                              lcd_controller_profile_summary_t *out_entry)
{
    char local_copy[96] = {0};
    char *colon = NULL;
    char *end_ptr = NULL;
    unsigned long profile_id = 0UL;

    if (item_text == NULL || out_entry == NULL || item_text[0] == '\0') {
        return false;
    }

    snprintf(local_copy, sizeof(local_copy), "%s", item_text);
    colon = strchr(local_copy, ':');
    if (colon == NULL) {
        return false;
    }

    *colon = '\0';
    profile_id = strtoul(local_copy, &end_ptr, 10);
    if (end_ptr == local_copy || *end_ptr != '\0' || profile_id > UINT8_MAX) {
        return false;
    }

    memset(out_entry, 0, sizeof(*out_entry));
    out_entry->profile_id = (uint8_t)profile_id;
    snprintf(out_entry->profile_name, sizeof(out_entry->profile_name), "%s", colon + 1);
    return true;
}

/**
 * @brief Parse `LCDProtoProfileCatalog` payload into a catalog structure.
 */
static bool lcd_controller_parse_profile_catalog_payload(const char *payload_text,
                                                         lcd_controller_profile_catalog_t *out_catalog)
{
    char count_text[16] = {0};
    char items_text[LCD_CONTROLLER_PROFILE_ITEMS_MAX_TEXT] = {0};
    unsigned long expected_count = 0UL;
    char *end_ptr = NULL;
    char *token = NULL;
    char *save_ptr = NULL;
    uint8_t parsed_count = 0U;

    if (payload_text == NULL || out_catalog == NULL) {
        return false;
    }

    memset(out_catalog, 0, sizeof(*out_catalog));

    if (!lcd_controller_extract_payload_value(payload_text, "count", count_text, sizeof(count_text))) {
        return false;
    }

    expected_count = strtoul(count_text, &end_ptr, 10);
    if (end_ptr == count_text || *end_ptr != '\0') {
        return false;
    }

    if (!lcd_controller_extract_payload_value(payload_text, "items", items_text, sizeof(items_text))) {
        return (expected_count == 0UL);
    }

    token = strtok_r(items_text, "|", &save_ptr);
    while (token != NULL && parsed_count < LCD_CONTROLLER_MAX_PROFILES) {
        if (lcd_controller_parse_profile_item(token, &out_catalog->entries[parsed_count])) {
            parsed_count++;
        }
        token = strtok_r(NULL, "|", &save_ptr);
    }

    out_catalog->count = parsed_count;
    return (parsed_count > 0U) || (expected_count == 0UL);
}

/**
 * @brief Safely read and return the current link-state snapshot.
 */
lcd_controller_protocol_link_state_t lcd_controller_protocol_get_link_state(void)
{
    lcd_controller_protocol_link_state_t link_state = LCD_CONTROLLER_PROTOCOL_STATE_UNINITIALIZED;

    if (s_protocol.mutex == NULL) {
        return link_state;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    link_state = s_protocol.link_state;
    xSemaphoreGive(s_protocol.mutex);
    return link_state;
}

/**
 * @brief Initialize protocol context and register transport send callback.
 */
esp_err_t lcd_controller_protocol_init(lcd_controller_send_command_fn_t send_command_cb,
                                       void *send_command_user_ctx)
{
    if (s_protocol.mutex == NULL) {
        s_protocol.mutex = xSemaphoreCreateMutex();
        if (s_protocol.mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);

    s_protocol.initialized = true;
    s_protocol.hooks_requested = false;
    s_protocol.last_event_count = 0U;
    s_protocol.link_state = LCD_CONTROLLER_PROTOCOL_STATE_READY;
    memset(&s_protocol.profile_catalog, 0, sizeof(s_protocol.profile_catalog));
    memset(&s_protocol.brew_state, 0, sizeof(s_protocol.brew_state));
    s_protocol.send_command_cb = send_command_cb;
    s_protocol.send_command_user_ctx = send_command_user_ctx;

    xSemaphoreGive(s_protocol.mutex);

    ESP_LOGI(TAG, "Initialized Brew/Home protocol layer (schema=%u)", (unsigned)LCD_CONTROLLER_BREW_SCHEMA_VERSION);
    return ESP_OK;
}

/**
 * @brief Send protocol bootstrap commands (`init` + `profile catalog get`).
 */
esp_err_t lcd_controller_protocol_initialize_hooks(void)
{
    lcd_controller_send_command_fn_t send_cb = NULL;
    void *send_ctx = NULL;

    if (s_protocol.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    if (!s_protocol.initialized || s_protocol.send_command_cb == NULL) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    send_cb = s_protocol.send_command_cb;
    send_ctx = s_protocol.send_command_user_ctx;
    s_protocol.link_state = LCD_CONTROLLER_PROTOCOL_STATE_WAIT_SCHEMA_ACK;
    xSemaphoreGive(s_protocol.mutex);

    esp_err_t ret = send_cb(LCD_CONTROLLER_PROTOCOL_CMD_INIT, send_ctx);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = send_cb(LCD_CONTROLLER_PROTOCOL_CMD_PROFILE_CATALOG_GET, send_ctx);
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    s_protocol.hooks_requested = true;
    xSemaphoreGive(s_protocol.mutex);

    ESP_LOGI(TAG, "Bootstrap hooks requested: init + profile catalog poll queued.");
    return ESP_OK;
}

/**
 * @brief Decode one peer text-event and update local protocol caches.
 */
esp_err_t lcd_controller_protocol_process_peer_text_event(uint32_t event_count,
                                                          const char *payload_text)
{
    lcd_controller_profile_catalog_t parsed_catalog = {0};
    bool publish_catalog = false;
    lcd_controller_profile_catalog_hook_t catalog_hook = NULL;
    void *catalog_hook_ctx = NULL;

    if (payload_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_protocol.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    if (!s_protocol.initialized) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (event_count == s_protocol.last_event_count) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_OK;
    }
    s_protocol.last_event_count = event_count;
    xSemaphoreGive(s_protocol.mutex);

    if (lcd_controller_starts_with_ignore_case(payload_text, LCD_CONTROLLER_PROTOCOL_EVENT_PREFIX_ACK)) {
        xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
        s_protocol.link_state = LCD_CONTROLLER_PROTOCOL_STATE_SCHEMA_ACKED;
        xSemaphoreGive(s_protocol.mutex);
        return ESP_OK;
    }

    if (!lcd_controller_starts_with_ignore_case(payload_text, LCD_CONTROLLER_PROTOCOL_EVENT_PREFIX_PROFILE_CATALOG)) {
        return ESP_OK;
    }

    if (!lcd_controller_parse_profile_catalog_payload(payload_text, &parsed_catalog)) {
        ESP_LOGW(TAG, "Failed to parse profile catalog payload: %s", payload_text);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    s_protocol.profile_catalog = parsed_catalog;
    catalog_hook = s_protocol.profile_hook_cb;
    catalog_hook_ctx = s_protocol.profile_hook_user_ctx;
    publish_catalog = (catalog_hook != NULL);
    xSemaphoreGive(s_protocol.mutex);

    if (publish_catalog) {
        catalog_hook(&parsed_catalog, catalog_hook_ctx);
    }
    return ESP_OK;
}

/**
 * @brief Publish latest Brew/Home state into the protocol cache.
 */
esp_err_t lcd_controller_protocol_publish_brew_home_state(const lcd_controller_brew_home_state_t *state)
{
    lcd_controller_brew_state_hook_t state_hook = NULL;
    void *state_hook_ctx = NULL;

    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_protocol.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    if (!s_protocol.initialized) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    s_protocol.brew_state = *state;
    state_hook = s_protocol.brew_hook_cb;
    state_hook_ctx = s_protocol.brew_hook_user_ctx;
    xSemaphoreGive(s_protocol.mutex);

    if (state_hook != NULL) {
        state_hook(state, state_hook_ctx);
    }
    return ESP_OK;
}

/**
 * @brief Return the latest Brew/Home state snapshot.
 */
esp_err_t lcd_controller_protocol_get_brew_home_state(lcd_controller_brew_home_state_t *out_state)
{
    if (out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_protocol.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    if (!s_protocol.initialized) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    *out_state = s_protocol.brew_state;
    xSemaphoreGive(s_protocol.mutex);
    return ESP_OK;
}

/**
 * @brief Return the latest profile catalog snapshot.
 */
esp_err_t lcd_controller_protocol_get_profile_catalog(lcd_controller_profile_catalog_t *out_catalog)
{
    if (out_catalog == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_protocol.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    if (!s_protocol.initialized) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    *out_catalog = s_protocol.profile_catalog;
    xSemaphoreGive(s_protocol.mutex);
    return ESP_OK;
}

/**
 * @brief Register optional profile-catalog callback hook.
 */
void lcd_controller_protocol_set_profile_catalog_hook(lcd_controller_profile_catalog_hook_t hook_cb,
                                                      void *hook_user_ctx)
{
    if (s_protocol.mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    s_protocol.profile_hook_cb = hook_cb;
    s_protocol.profile_hook_user_ctx = hook_user_ctx;
    xSemaphoreGive(s_protocol.mutex);
}

/**
 * @brief Register optional Brew/Home-state callback hook.
 */
void lcd_controller_protocol_set_brew_state_hook(lcd_controller_brew_state_hook_t hook_cb,
                                                 void *hook_user_ctx)
{
    if (s_protocol.mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    s_protocol.brew_hook_cb = hook_cb;
    s_protocol.brew_hook_user_ctx = hook_user_ctx;
    xSemaphoreGive(s_protocol.mutex);
}
