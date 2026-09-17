/* SPDX-License-Identifier: MIT */
/**
 * @file draw.h
 * @brief Definitions for display content drawing an background image handling.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#ifndef MAIN_DRAW_H_
#define MAIN_DRAW_H_

#include "esp_err.h"

/**
 * @brief Initialize style and theme.
 */
esp_err_t draw_init(void);

/**
 * @brief Draw all display content.
 */
void draw_content(void);

/**
 * @brief Save the background image onto the flash storage.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR_NOT_FOUND if storage partition was not found.
 * @retval ESP_ERR_NO_MEM if storage is too small to fit the background image.
 * @retval ESP_ERR_* on other failure.
 */
esp_err_t save_image_to_flash(void);

/**
 * @brief Load background image from flash storage.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR_NOT_FOUND if storage partition was not found.
 * @retval ESP_ERR_INVALID_STATE if the stored content is not a valid image.
 * @retval ESP_ERR_NO_MEM if unable to allocate enough memory to hold the image data.
 * @retval ESP_ERR_* on other failure.
 */
esp_err_t load_image_from_flash(void);

/**
 * @brief Load default image into background.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR_NO_MEM if failed to allocate background image memory.
 */
esp_err_t set_image_default(void);

#endif // MAIN_DRAW_H_