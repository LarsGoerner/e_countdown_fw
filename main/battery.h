/* SPDX-License-Identifier: MIT */
/**
 * @file battery.h
 * @brief Definitions for battery measuring task.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#ifndef MAIN_BATTERY_H_
#define MAIN_BATTERY_H_

#include "esp_err.h"

/** @brief Battery state. */
typedef enum {
	BAT_STA_FULL,
	BAT_STA_3,
	BAT_STA_2,
	BAT_STA_1,
	BAT_STA_EMPTY,
} bat_state_t;

/**
 * @brief Initialize and start battery task.
 */
void battery_init(void);

#endif // MAIN_BATTERY_H_