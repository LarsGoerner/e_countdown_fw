/* SPDX-License-Identifier: MIT */
/**
 * @file message.h
 * @brief Definitions for Event handling.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#ifndef MAIN_MESSAGE_H_
#define MAIN_MESSAGE_H_

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/** @brief Event type enum. */
typedef enum {
	EV_BAT,		/**< Battery state update. */
	EV_BG_IMG,	/**< Background image update. */
	EV_STYLE,	/**< Display style update. */
	EV_CUR_DT,	/**< Current time update. */
	EV_TGT_D,	/**< Target date update. */
	EV_TST,		/**< Test request. */
} event_id_t;

/** @brief Font style data struct. */
typedef struct {
	uint8_t font_size;	/**< Font size. */

	uint8_t font_id : 4;	/**< Used font ID. */
	bool is_bold : 1;	/**< 1: Bold font, 0: Normal font. */
	bool is_italic : 1;	/**< 1: Italic font, 0: Normal font. */
	uint8_t f_fg_clr : 1;	/**< Font foreground color (0 - black, 1 - white). */
	uint8_t f_bg_clr : 1;	/**< Font background color (0 - black, 1 - white). */
} font_sty_t;

/** @brief Message type struct. */
typedef struct {
	event_id_t event_id; /**< Message event ID. */
	union {
		const void * bg_img_buf; /**< Background image update. */

		uint8_t test_id; /**< Test request. */

		font_sty_t style; /**< Display tyle update. */
	};
} message_t;

#endif // MAIN_MESSAGE_H_