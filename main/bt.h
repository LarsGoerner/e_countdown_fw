/* SPDX-License-Identifier: MIT */
/**
 * @file bt.h
 * @brief Definitions for Bluetooth LE.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#ifndef MAIN_BT_H_
#define MAIN_BT_H_

#include "esp_err.h"

/**
 * @brief Initialize Bluetooth LE interface.
 * 
 * @retval ESP_OK on success.
 * @retval ESP_ERR_* on failure.
 */
esp_err_t bt_init(void);

#endif // MAIN_BT_H_