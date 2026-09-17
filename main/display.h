/* SPDX-License-Identifier: MIT */
/**
 * @file display.h
 * @brief Definitions for display driver and LVGL adapter handling.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#ifndef MAIN_DISPLAY_H_
#define MAIN_DISPLAY_H_

#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Initialize display driver and lv_adapter.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR* on failure.
 */
esp_err_t display_init(void);

/**
 * @brief Turn on display.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR_* on failure.
 */
esp_err_t display_on(void);

/**
 * @brief Trigger a display refresh.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR_* on failure.
 */
esp_err_t display_refresh(void);

/**
 * @brief Refresh display with a test image.
 * @sa epd4in26_test_id_t.
 * 
 * @param[in] tid Test image ID.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR_* on failure.
 */
esp_err_t display_gen_test(uint8_t tid);

/**
 * @brief Turn off display.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR_* on failure.
 */
esp_err_t display_off(void);

#endif // MAIN_DISPLAY_H_