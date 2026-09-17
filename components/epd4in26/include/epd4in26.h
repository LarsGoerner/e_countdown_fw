/* SPDX-License-Identifier: MIT */
/**
 * @file epd4in26.h
 * @brief Definitions for waveshare 4.26inch display driver.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#ifndef EPD4IN26_H_
#define EPD4IN26_H_

#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#define EPD4IN26_CLK_HZ     (1 * 1000 * 1000)
#define EPD4IN26_WIDTH      800
#define EPD4IN26_HEIGHT     480

/**
 * @brief Additional display configurations.
 * @note Needs to be added to esp_lcd_panel_dev_config_t->vendor_config.
 */
typedef struct {
	gpio_num_t busy_gpio_num; /**< BUSY GPIO number. */
} esp_lcd_epd4in26_config_t;

/**
 * @brief Create LCD panel for epd4in26 display.
 * 
 * @param[in] io LCD panel io handle.
 * @param[in] cfg Panel device configuration.
 * @param[out] panel New LCD panel handle.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR_INVALID_ARG if parameter is invalid.
 * @retval ESP_ERR_NO_MEM if out of memory.
 */
esp_err_t esp_lcd_new_panel_epd4in26(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t * cfg,
				     esp_lcd_panel_handle_t * panel);

/** @brief Test image ID enum. */
typedef enum {
	TEST_ID_WHITE,		/**< Full white */
	TEST_ID_BLACK,		/**< Full black */
	TEST_ID_CHECKER,	/**< Checkerboard pattern */
	TEST_ID_HLINES,		/**< Horizontal lines */
	TEST_ID_VLINES,		/**< Vertical lines */
	TEST_ID_NUM
} epd4in26_test_id_t;

/**
 * @brief Generate test content and trigger display update.
 * 
 * @param[in] panel Panel handle.
 * @param[in] tid Test ID enum.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR_INVALID_ARG if panel handle is invalid.
 * @retval ESP_ERR_TIMEOUT if a timeout occured.
 * @retval ESP_ERR* on other failure.
 */
esp_err_t epd4in26_gen_test(esp_lcd_panel_handle_t panel, epd4in26_test_id_t tid);

/**
 * @brief Trigger a display refresh.
 * 
 * @param[in] panel Panel handle.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR_INVALID_ARG if panel handle is invalid.
 * @retval ESP_ERR_TIMEOUT if a timeout occured.
 * @retval ESP_ERR* on other failure.
 */
esp_err_t epd4in26_refresh(esp_lcd_panel_handle_t panel);

#endif /* EPD4IN26_H_ */