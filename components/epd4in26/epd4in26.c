/* SPDX-License-Identifier: MIT */
/**
 * @file epd4in26.c
 * @brief Implementations for waveshare 4.26inch display driver.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <byteswap.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"

#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"

#include "epd4in26.h"

/* == DITHERING ============================================================= */

/**
 * @name Dithering algorithm options
 * @{
 */

/**
 * @def DITHER_ALGO_SL
 * @brief Sierra Lite dithering.
 */
//#define DITHER_ALGO_SL

/**
 * @def DITHER_ALGO_JJN
 * @brief Jarvis, Judice and Ninke dithering.
 */
#define DITHER_ALGO_JJN

/** @} */

/* == COLOR DEPTH =========================================================== */

/**
 * @name Pixel format options
 * @{
 */

/**
 * @def DSP_BPP_1
 * @brief 1 bit per pixel.
 */
//#define DSP_BPP_1
/**
 * @def DSP_BPP_2
 * @brief 2 bit per pixel.
 */
#define DSP_BPP_2

/** @} */

/** @brief epd4in26 panel structure */
typedef struct {
	esp_lcd_panel_t base;			/**< LCD Panel base struct. */
	esp_lcd_panel_io_handle_t io;		/**< LCD panel IO handle. */
	gpio_num_t busy_gpio_num;		/**< BUSY GPIO number */
	gpio_num_t reset_gpio_num;		/**< RESET GPIO number */
	uint32_t reset_level;			/** GPIO level for reset state */

	uint32_t bits_per_pixel;		/**< Bits per pixel */
	lcd_rgb_element_order_t rgb_ele_order;	/**< RGB or BGR */
	lcd_rgb_data_endian_t px_data_endian;	/**< Pixel data endianess */
	uint8_t * _framebuffer_bw;		/**< internal BW working buffer */
	uint8_t * _framebuffer_red;		/**< internal RED working buffer */
	uint8_t * _gl_work_buf;			/**< internal 8bit graylevel working buffer */
} epd4in26_panel_t;

/**
 * @name Logging helper constants
 * @{
 */

/** @brief Logger tag string. */
static const char * TAG = "EPD4IN26";
/** @brief Default busy fail message. */
#define ERR_MSG_BUSY "BUSY wait failed"
/** @brief Default command send fail message. */
#define ERR_MSG_CMD "Failed to sent command"
/** @brief Default data send fail message */
#define ERR_MSG_DATA "Failed to send data"

/** @} */

/** @brief LUT data for 2bpp display mode */
const unsigned char LUT_DATA_4Gray[112] = { //112bytes
0x80,	0x48,	0x4A,	0x22,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,
0x0A,	0x48,	0x68,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,
0x88,	0x48,	0x60,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,
0xA8,	0x48,	0x45,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,
0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,	0x00,
0x07,	0x1E,	0x1C,	0x02,	0x00,
0x05,	0x01,	0x05,	0x01,	0x02,
0x08,	0x01,	0x01,	0x04,	0x04,
0x00,	0x02,	0x00,	0x02,	0x01,
0x00,	0x00,	0x00,	0x00,	0x00,
0x00,	0x00,	0x00,	0x00,	0x00,
0x00,	0x00,	0x00,	0x00,	0x00,
0x00,	0x00,	0x00,	0x00,	0x00,
0x00,	0x00,	0x00,	0x00,	0x00,
0x00,	0x00,	0x00,	0x00,	0x01,
0x22,	0x22,	0x22,	0x22,	0x22,
0x17,	0x41,	0xA8,	0x32,	0x30,
0x00,	0x00 };

/* == COMMANDS ============================================================== */

/**
 * @name Display Commands
 * @{
 */

/** @brief Driver output control */
#define CMD_DOC		0x01
/** @brief Gate driving voltage control */
#define CMD_GDVC	0x03
/** @brief Source driving voltage control */
#define CMD_SDVC	0x04
/** @brief Booster soft-start control */
#define CMD_BSSC	0x0C
/** @brief Deep sleep mode */
#define CMD_DSM		0x10
/** @brief Data entry mode setting */
#define CMD_DEMS	0x11
/** @brief SW reset */
#define CMD_SW_RST	0x12
/** @brief Temperature sensor control */
#define CMD_TSC		0x18
/** @brief Master activation */
#define CMD_MA		0x20
/** @brief Display update control 2 */
#define CMD_DUC2	0x22
/** @brief Write BW RAM */
#define CMD_RAM_BW	0x24
/** @brief Write RED RAM */
#define CMD_RAM_RED	0x26
/** @brief Write VCOM register */
#define CMD_VCOM	0x2C
/** @brief Write LUT register */
#define CMD_LUT		0x32
/** @brief Border waveform control */
#define CMD_BWC		0x3C
/** @brief Set RAM X - address */
#define CMD_RAM_X	0x44
/** @brief Set RAM Y - address */
#define CMD_RAM_Y	0x45
/** @brief Auto write RED RAM for regular pattern */
#define CMD_AUTO_RED	0x46
/** @brief Auto write BW RAM for regular pattern */
#define CMD_AUTO_BW	0x47
/** @brief Set RAM X address counter */
#define CMD_RAM_X_CNT	0x4E
/** @brief Set RAM Y address counter */
#define CMD_RAM_Y_CNT	0x4F

/** @} */

/* ========================================================================== */
/* == INTERNAL FUNCTIONS ==================================================== */
/* ========================================================================== */

/**
 * @brief Wait for BUSY signal to go off
 * 
 * @param p Display panel reference
 * 
 * @retval ESP_OK on success
 * @retval ESP_ERR_TIMEOUT when timeout happened
 */
static esp_err_t wait_busy(const epd4in26_panel_t * p)
{
	unsigned timeout_count = 5000 / 20; // timeout 5 seconds @ 20ms delay
	while (timeout_count--) {
		// BUSY == 1
		if (gpio_get_level(p->busy_gpio_num) == 0) { return ESP_OK; }
		vTaskDelay(pdMS_TO_TICKS(20));
	}
	return ESP_ERR_TIMEOUT;
}

/**
 * @brief Set image area
 * 
 * @param[in] p Display panel reference
 * @param[in] xs  X start index
 * @param[in] ys  Y start index
 * @param[in] xe  X last index
 * @param[in] ye  Y last index
 * 
 * @retval ESP_OK on success
 * @retval ESP_ERR_INVALID_ARG if param is invalid
 * @retval ESP_ERR_* on failure
 */
static esp_err_t set_area(const epd4in26_panel_t * p, uint16_t xs, uint16_t ys,
			  uint16_t xe, uint16_t ye)
{
	uint8_t send_buf[4];

	ESP_RETURN_ON_FALSE(p, ESP_ERR_INVALID_ARG, TAG, "panel reference is NULL");

	send_buf[0] = xs & 0xFF;
	send_buf[1] = (xs >> 8) & 0x03;
	send_buf[2] = xe & 0xFF;
	send_buf[3] = (xe >> 8) & 0x03;
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_RAM_X, send_buf, sizeof(send_buf)), TAG, "Failed to send X RAM area");

	send_buf[0] = ys & 0xFF;
	send_buf[1] = (ys >> 8) & 0x03;
	send_buf[2] = ye & 0xFF;
	send_buf[3] = (ye >> 8) & 0x03;
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_RAM_Y, send_buf, sizeof(send_buf)), TAG, "Failed to send Y RAM area");
	return ESP_OK;
}

/**
 * @brief Set RAM position
 * 
 * @param[in] p Display panel reference
 * @param[in] xs  X start index
 * @param[in] ys  Y start index
 * 
 * @retval ESP_OK on success
 * @retval ESP_ERR_INVALID_ARG if param is invalid
 * @retval ESP_ERR_* on failure
 */
static esp_err_t set_cursor(const epd4in26_panel_t * p, uint16_t xs, uint16_t ys)
{
	uint8_t send_buf[2];

	ESP_RETURN_ON_FALSE(p, ESP_ERR_INVALID_ARG, TAG, "Panel reference is NULL");

	send_buf[0] = xs & 0xFF;
	send_buf[1] = (xs >> 8) & 0x03;
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_RAM_X_CNT, send_buf, sizeof(send_buf)), TAG, "Failed to send X RAM counter");

	send_buf[0] = ys & 0xFF;
	send_buf[1] = (ys >> 8) & 0x03;
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_RAM_Y_CNT, send_buf, sizeof(send_buf)), TAG, "Failed to send Y RAM counter");
	return ESP_OK;
}

/**
 * @brief Set look-up tables
 * 
 * @param[in] p Display panel reference
 * 
 * @retval ESP_OK on success
 * @retval ESP_ERR_* on failure
 */
static esp_err_t send_lut(const epd4in26_panel_t * p)
{
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_LUT, LUT_DATA_4Gray, 105), TAG, "Failed to send LUT");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_GDVC, &LUT_DATA_4Gray[105], 1), TAG, "Failed to set GDVC");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_SDVC, &LUT_DATA_4Gray[106], 3), TAG, "Failed to set SDVC");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_VCOM, &LUT_DATA_4Gray[109], 1), TAG, "Failed to set VCOM");
	return ESP_OK;
}

/**
 * @brief Turn on display & initialize
 * 
 * @param[in] p Display panel reference
 * 
 * @retval ESP_OK on success
 * @retval ESP_ERR_INVALID_ARG if an argument is invalid
 */
static esp_err_t epd4in26_on(epd4in26_panel_t * p)
{
	ESP_RETURN_ON_FALSE(p, ESP_ERR_INVALID_ARG, TAG, "Panel reference is NULL");

	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_SW_RST, NULL, 0), TAG, "SW reset failed");
	ESP_RETURN_ON_ERROR(wait_busy(p), TAG, "BUSY wait failed");

	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_TSC,
			    (uint8_t[]){ 0x80 }, 1), TAG, "Failed to setup temperature sensor");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_BSSC,
			    (uint8_t[]){ 0xAE, 0xC7, 0xC3, 0xC0, 0x80 }, 5), TAG, "Failed to set BSSC");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_DOC,
			    (uint8_t[]){ (EPD4IN26_HEIGHT - 1) % 256,
			    (EPD4IN26_HEIGHT - 1) / 256, 0x02 }, 3), TAG, "Failed to set DOC");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_BWC,
			    (uint8_t[]){ 0x01 }, 1), TAG, "Failed to set BWC");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_DEMS,
			    (uint8_t[]){ 0x01 }, 1), TAG, "Failed to set BWC");
	ESP_RETURN_ON_ERROR(set_area(p, 0, EPD4IN26_HEIGHT - 1, EPD4IN26_WIDTH - 1,
			    0), TAG, "Failed to set area");
	ESP_RETURN_ON_ERROR(set_cursor(p, 0, 0), TAG, "Failed to set cursor");
	ESP_RETURN_ON_ERROR(wait_busy(p), TAG, ERR_MSG_BUSY);

#ifdef DSP_BPP_2
	ESP_RETURN_ON_ERROR(send_lut(p), TAG, "Failed to send LUT");
#endif
	return ESP_OK;
}

/**
 * @brief Turn off display
 * 
 * @param[in] p Display panel reference
 * 
 * @retval ESP_OK on success
 * @retval ESP_ERR_INVALID_ARG if an argument is invalid
 */
static esp_err_t epd4in26_off(const epd4in26_panel_t * p)
{
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, CMD_DSM, (uint8_t[]){ 0x03 }, 1), TAG, "Failed to set sleep mode");
	return ESP_OK;
}

/**
 * @brief Turn display on/off
 * 
 * @param[in] panel LCD panel handle
 * @param[in] on_off True to turn on display, False to turn off display
 * 
 * @retval ESP_OK on success
 * @retval ESP_ERR_INVALID_ARG if an argument is invalid
 */
static esp_err_t epd4in26_on_off(esp_lcd_panel_t *panel, bool on_off)
{
	ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG, "Panel handle is NULL");
	epd4in26_panel_t * disp_panel = __containerof(panel, epd4in26_panel_t, base);

	if (on_off) { return epd4in26_on(disp_panel); }
	else { return epd4in26_off(disp_panel); }
}

/**
 * @brief Destroy LCD panel
 * 
 * @param[in] panel LCD panel handle
 * 
 * @retval ESP_OK on success
 */
static esp_err_t epd4in26_panel_del(esp_lcd_panel_t * panel)
{
	epd4in26_panel_t * disp_panel = __containerof(panel, epd4in26_panel_t, base);
	gpio_reset_pin(disp_panel->busy_gpio_num);
	if (disp_panel->_framebuffer_bw) { free(disp_panel->_framebuffer_bw); }
	if (disp_panel->_framebuffer_red) { free(disp_panel->_framebuffer_red); }
	ESP_LOGD(TAG, "Deleting epd4in26 panel @%p", disp_panel);
	free(disp_panel);
	return ESP_OK;
}

/**
 * @brief Reset LCD panel
 * 
 * @param[in] panel LCD panel handle
 * 
 * @retval ESP_OK on success
 * @retval ESP_ERR_INVALID_ARG if an argument is invalid
 * @retval ESP_ERR_TIMEOUT if BUSY timout
 */
static esp_err_t epd4in26_panel_reset(esp_lcd_panel_t * panel)
{
	epd4in26_panel_t * disp_panel = __containerof(panel, epd4in26_panel_t, base);
	
	ESP_RETURN_ON_ERROR(gpio_set_level(disp_panel->reset_gpio_num, disp_panel->reset_level), TAG, "Failed to set reset active");
	vTaskDelay(pdMS_TO_TICKS(10));
	ESP_RETURN_ON_ERROR(gpio_set_level(disp_panel->reset_gpio_num, !disp_panel->reset_level), TAG, "Failed to set reset inactive");
	vTaskDelay(pdMS_TO_TICKS(10));
	ESP_RETURN_ON_ERROR(wait_busy(disp_panel), TAG, "BUSY timeout");

	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(disp_panel->io, CMD_SW_RST, NULL, 0), TAG, "SW reset failed");
	ESP_RETURN_ON_ERROR(wait_busy(disp_panel), TAG, "BUSY timeout");
	return ESP_OK;
}

/* == DRAWING FUNCTIONS ===================================================== */

static void convert_to_graylevel(const epd4in26_panel_t * p, int x_start, int y_start, int x_end, int y_end, const uint16_t * px_buf)
{
	const int img_w = x_end - x_start;
	const int img_h = y_end - y_start;

	if (p->bits_per_pixel == 16) {
		int r, g, b;
		if (p->rgb_ele_order == LCD_RGB_ELEMENT_ORDER_RGB) {
			//   R      G     B
			// ..... ...... .....
			for (int y = y_start; y < y_end; y++) {
				for (int x = x_start; x < x_end; x++) {
					uint16_t px = *px_buf;// bswap_16(*px_buf);
					b = (px & 0x1F) << 3;
					g = (px & 0x7E0) >> 3;
					r = (px & 0xF800) >> 8;
					p->_gl_work_buf[(y * img_w) + x] = (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
					++px_buf;
				}
			}
		} else if (p->rgb_ele_order == LCD_RGB_ELEMENT_ORDER_BGR) {
			//   B      G     R
			// ..... ...... .....
			for (int y = y_start; y < y_end; y++) {
				for (int x = x_start; x < x_end; x++) {
					uint16_t px = bswap_16(*px_buf);
					r = (px & 0x1F) << 3;
					g = (px & 0x7E0) >> 3;
					b = (px & 0xF800) >> 8;
					p->_gl_work_buf[(y * img_w) + x] = (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
					++px_buf;
				}
			}
		}
	} else if (p->bits_per_pixel == 8) {
		for (int y = y_start; y < y_end; y++) {
			memcpy(&p->_gl_work_buf[(img_w * y) + x_start], px_buf, img_w);
			px_buf += img_w;
		}
	}
}

#if defined(DITHER_ALGO_SL)
static void dither_graylevel(const epd4in26_panel_t * p)
{
	// Sierra Lite
	//    x  2
	// 1  1
	//  (1/4)
	const int width = EPD4IN26_WIDTH;
	const int height = EPD4IN26_HEIGHT;
	uint8_t * gl_buf = p->_gl_work_buf;
	int16_t * cur_row = malloc(width * sizeof(int16_t));
	int16_t * nxt_row = malloc(width * sizeof(int16_t));
	if (cur_row == NULL || nxt_row == NULL) {
		free(cur_row);
		free(nxt_row);
		ESP_LOGE(TAG, "Failed to allocate dither buffer");
		return;
	}

	for (int x = 0; x < width; x++) {
		cur_row[x] = gl_buf[x];
	}

	for (int y = 0; y < height; y++) {

		if (y < height - 1) {
			const uint8_t * src = gl_buf + (y + 1) * width;
			for (int x = 0; x < width; x++) {
				nxt_row[x] = src[x];
			}
		}
		uint8_t * dst_row = gl_buf + y * width;

		for (int x = 0; x < width; x++) {
			int16_t old_val = cur_row[x];
			uint8_t new_val;

			if (p->mode == EPD4IN26_MODE_2GL) {
				new_val = old_val >= 128 ? UINT8_MAX : 0;
			} else { // 4GL
				if (old_val < 42) new_val = 0;
				else if (old_val < 128) new_val = 85;
				else if (old_val < 213) new_val = 170;
				else new_val = 255;
			}
			dst_row[x] = new_val;
			int err = old_val - new_val;

			if (x < width - 1) { cur_row[x+ 1] += err / 2; }
			if (y < height - 1) {
				nxt_row[x] += err / 4;
				if (x > 0) { nxt_row[x - 1] += err / 4; }
			}
		}
		int16_t * tmp = cur_row;
		cur_row = nxt_row;
		nxt_row = tmp;
	}
	free(cur_row);
	free(nxt_row);
}
#elif defined(DITHER_ALGO_JJN)
static void dither_graylevel(const epd4in26_panel_t * p)
{
	// Jarvis, Judice and Ninke
	//       x  7  5
	// 3  5  7  5  3
	// 1  3  5  3  1
	//     (1/48)
	const int width = EPD4IN26_WIDTH;
	const int height = EPD4IN26_HEIGHT;
	uint8_t * gl_buf = p->_gl_work_buf;
	int16_t * cur_row = malloc(width * sizeof(int16_t));
	int16_t * nxt_row = malloc(width * sizeof(int16_t));
	int16_t * nxtnxt_row = malloc(width * sizeof(int16_t));
	if (cur_row == NULL || nxt_row == NULL || nxtnxt_row == NULL) {
		free(cur_row);
		free(nxt_row);
		free(nxtnxt_row);
		ESP_LOGE(TAG, "Failed to allocate dither buffer");
		return;
	}

	for (int x = 0; x < width; x++) {
		cur_row[x] = gl_buf[x];
		nxt_row[x] = (height > 1) ? gl_buf[width + x] : 0;
		nxtnxt_row[x] = (height > 2) ? gl_buf[2 * width + x] : 0;
	}

	for (int y = 0; y < height; y++) {
		uint8_t * dst_row = gl_buf + y * width;

		for (int x = 0; x < width; x++) {
			int16_t old_val = cur_row[x];
			uint8_t new_val;
#if defined(DSP_BPP_1)
			new_val = old_val >= 128 ? UINT8_MAX : 0;
#elif defined(DSP_BPP_2)
			if (old_val < 42) new_val = 0;
			else if (old_val < 128) new_val = 85;
			else if (old_val < 213) new_val = 170;
			else new_val = 255;
#endif
			dst_row[x] = new_val;
			int err = old_val - new_val;
			int err_1_48 = err / 48;
			int err_3_48 = err * 3 / 48;
			int err_5_48 = err * 5 / 48;
			int err_7_48 = err * 7 / 48;

			if (x < width - 1) { cur_row[x + 1] += err_7_48; }
			if (x < width - 2) { cur_row[x + 2] += err_5_48; }
			if (y < height - 1) {
				if (x > 1) { nxt_row[x - 2] += err_3_48; }
				if (x > 0) { nxt_row[x - 1] += err_5_48; }
				nxt_row[x] += err_7_48;
				if (x < width - 1) { nxt_row[x + 1] += err_5_48; }
				if (x < width - 2) { nxt_row[x + 2] += err_3_48; }
			}
			if (y < height - 2) {
				if (x > 1) { nxtnxt_row[x - 2] += err_1_48; }
				if (x > 0) { nxtnxt_row[x - 1] += err_3_48; }
				nxtnxt_row[x] += err_5_48;
				if (x < width - 1) { nxtnxt_row[x + 1] += err_3_48; }
				if (x < width - 2) { nxtnxt_row[x + 2] += err_1_48; }
			}
		}
		int16_t * tmp = cur_row;
		cur_row = nxt_row;
		nxt_row = nxtnxt_row;
		nxtnxt_row = tmp;
		if (y + 3 < height) {
			const uint8_t * src = gl_buf + (y + 3) * width;
			for (int x = 0; x < width; x++) { nxtnxt_row[x] = src[x]; }
		}
	}
	free(cur_row);
	free(nxt_row);
	free(nxtnxt_row);
}
#endif

static void convert_graylevel_to_fb(const epd4in26_panel_t * p)
{
	const int px_num = EPD4IN26_WIDTH * EPD4IN26_HEIGHT;
	const uint8_t * gl_buf_it = p->_gl_work_buf;
	uint8_t * bw_buf_it = p->_framebuffer_bw;
	uint8_t * red_buf_it = p->_framebuffer_red;

#if defined(DSP_BPP_1)
	for (int i = 0; i < px_num; i += 8) {
		*bw_buf_it = 0;
		for (int j = 0; j < 8; j++) {
			*bw_buf_it <<= 1;
			*bw_buf_it |= *gl_buf_it++ & 0x80 ? 1 : 0;
		}
		++bw_buf_it;
	}
	memset(red_buf_it, 0x00, px_num / 8);
#elif defined(DSP_BPP_2)
	for (size_t i = 0; i < px_num; i += 8) {
		*bw_buf_it = 0;
		*red_buf_it = 0;
		for (int j = 0; j < 8; j++) {
			*bw_buf_it <<= 1;
			*red_buf_it <<= 1;
			*bw_buf_it |= (*gl_buf_it & BIT6) ? 0 : 1;
			*red_buf_it |= (*gl_buf_it & BIT7) ? 0 : 1;
			gl_buf_it++;
		}
		bw_buf_it++;
		red_buf_it++;
	}
#endif
}

/**
 * @brief Draw bitmap on LCD panel
 * @note color_data needs to be aligned to 8 pixel in x-direction
 *
 * @param[in] panel LCD panel handle
 * @param[in] x_start Start pixel index in the target frame buffer, on x-axis (x_start is included)
 * @param[in] y_start Start pixel index in the target frame buffer, on y-axis (y_start is included)
 * @param[in] x_end End pixel index in the target frame buffer, on x-axis (x_end is not included)
 * @param[in] y_end End pixel index in the target frame buffer, on y-axis (y_end is not included)
 * @param[in] color_data RGB color data that will be dumped to the specific window range
 * 
 * @retval ESP_OK on success
 * @retval ESP_ERR_INVALID_ARG if an argument is invalid
 * @retval ESP_ERR_NOT_SUPPORTED if color format is not supported
 */
static esp_err_t epd4in26_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
	ESP_RETURN_ON_FALSE(panel && color_data, ESP_ERR_INVALID_ARG, TAG, "1 or more args are NULL");
	epd4in26_panel_t * disp_panel = __containerof(panel, epd4in26_panel_t, base);

	convert_to_graylevel(disp_panel, x_start, y_start, x_end, y_end, color_data);
	return ESP_OK;
}

/* ========================================================================== */
/* == INTERFACE FUNCTIONS =================================================== */
/* ========================================================================== */

esp_err_t esp_lcd_new_panel_epd4in26(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t * cfg,
				     esp_lcd_panel_handle_t * panel)
{
	ESP_RETURN_ON_FALSE(io && cfg && panel, ESP_ERR_INVALID_ARG, TAG, "1 or more args are NULL");
	esp_lcd_epd4in26_config_t * extra_conf = cfg->vendor_config;
	esp_err_t ret;
	epd4in26_panel_t * disp_panel = malloc(sizeof(*disp_panel));
	ESP_RETURN_ON_FALSE(disp_panel, ESP_ERR_NO_MEM, TAG, "Failed to allocate panel struct memory");

	disp_panel->_framebuffer_bw = heap_caps_malloc(EPD4IN26_WIDTH * EPD4IN26_HEIGHT / 8, MALLOC_CAP_SPIRAM);
	ESP_GOTO_ON_FALSE(disp_panel->_framebuffer_bw, ESP_ERR_NO_MEM, err, TAG, "Failed to allocate BW framebuffer");
	disp_panel->_framebuffer_red = heap_caps_malloc(EPD4IN26_WIDTH * EPD4IN26_HEIGHT / 8, MALLOC_CAP_SPIRAM);
	ESP_GOTO_ON_FALSE(disp_panel->_framebuffer_red, ESP_ERR_NO_MEM, err, TAG, "Failed to allocate RED framebuffer");
	disp_panel->_gl_work_buf = heap_caps_malloc(EPD4IN26_WIDTH * EPD4IN26_HEIGHT, MALLOC_CAP_SPIRAM);
	ESP_GOTO_ON_FALSE(disp_panel->_gl_work_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to allocate working buffer");

	disp_panel->base.del = &epd4in26_panel_del;
	disp_panel->base.reset = &epd4in26_panel_reset;
	disp_panel->base.init = NULL;
	disp_panel->base.draw_bitmap = &epd4in26_draw_bitmap;
	disp_panel->base.invert_color = NULL;
	disp_panel->base.set_gap = NULL;
	disp_panel->base.mirror = NULL;
	disp_panel->base.swap_xy = NULL;
	disp_panel->base.disp_on_off = &epd4in26_on_off;
	disp_panel->io = io;
	disp_panel->busy_gpio_num = extra_conf->busy_gpio_num;
	disp_panel->reset_gpio_num = cfg->reset_gpio_num;
	disp_panel->reset_level = cfg->flags.reset_active_high;
	disp_panel->bits_per_pixel = cfg->bits_per_pixel;
	disp_panel->rgb_ele_order = cfg->rgb_ele_order;
	disp_panel->px_data_endian = cfg->data_endian;
	*panel = &(disp_panel->base);

	const gpio_config_t reset_gpio_cfg = {
		.pin_bit_mask = (1ULL << cfg->reset_gpio_num),
		.mode = GPIO_MODE_OUTPUT,
		.pull_down_en = false,
		.pull_up_en = true,
		.intr_type = GPIO_INTR_DISABLE
	};
	ESP_GOTO_ON_ERROR(gpio_config(&reset_gpio_cfg), err, TAG, "Failed to config RESET GPIO");

	const gpio_config_t busy_gpio_cfg = {
		.pin_bit_mask = (1ULL << disp_panel->busy_gpio_num),
		.mode = GPIO_MODE_INPUT,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.intr_type = GPIO_INTR_DISABLE
	};
	ESP_GOTO_ON_ERROR(gpio_config(&busy_gpio_cfg), err, TAG, "Failed to config BUSY GPIO");

	ESP_LOGD(TAG, "New epd4in26 panel created @%p", disp_panel);
	return ESP_OK;
err:
	if (disp_panel) {
		if (cfg->reset_gpio_num >= 0) { gpio_reset_pin(cfg->reset_gpio_num); }
		if (extra_conf->busy_gpio_num >= 0) { gpio_reset_pin(extra_conf->busy_gpio_num); }
		if (disp_panel->_framebuffer_bw) { free(disp_panel->_framebuffer_bw); }
		if (disp_panel->_framebuffer_red) { free(disp_panel->_framebuffer_red); }
		if (disp_panel->_gl_work_buf) { free(disp_panel->_gl_work_buf); }
		free(disp_panel);
	}
	return ret;
}

esp_err_t epd4in26_gen_test(esp_lcd_panel_handle_t panel, epd4in26_test_id_t tid)
{
	ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG, "Panel handle is NULL");
	epd4in26_panel_t * disp_panel = __containerof(panel, epd4in26_panel_t, base);
#if defined(DSP_BPP_1)
	uint8_t duc2_val = 0xF7;
#elif defined(DSP_BPP_2)
	uint8_t duc2_val = 0xC7;
#endif
	uint8_t auto_red_val;
	uint8_t auto_bw_val;

#if defined(DSP_BPP_1)
	switch (tid) {
	case TEST_ID_WHITE:
		auto_bw_val = 0xF7;
		break;
	case TEST_ID_BLACK:
		auto_bw_val = 0x77;
		break;
	case TEST_ID_CHECKER:
		auto_bw_val = 0x80;
		break;
	case TEST_ID_HLINES:
		auto_bw_val = 0x87;
		break;
	case TEST_ID_VLINES:
		auto_bw_val = 0xF0;
		break;
	default:
		return ESP_ERR_INVALID_ARG;
	}
#elif defined(DSP_BPP_2)
	//      BW RED
	// GL0: 1   1
	// GL1: 0   1
	// GL2: 1   0
	// GL3: 0   0
	switch (tid) {
	case TEST_ID_WHITE:
		auto_red_val = 0x77;
		auto_bw_val = 0x77;
		break;
	case TEST_ID_BLACK:
		auto_red_val = 0xF7;
		auto_bw_val = 0xF7;
		break;
	case TEST_ID_CHECKER:
		auto_red_val = 0x11;
		auto_bw_val = 0x00;
		break;
	case TEST_ID_HLINES:
		auto_red_val = 0x17;
		auto_bw_val = 0x07;
		break;
	case TEST_ID_VLINES:
		auto_red_val = 0x71;
		auto_bw_val = 0x70;
		break;
	default:
		return ESP_ERR_INVALID_ARG;
	}
#endif

	ESP_RETURN_ON_ERROR(set_area(disp_panel, 0, EPD4IN26_HEIGHT - 1, EPD4IN26_WIDTH - 1, 0),
			    TAG, "Failed to set image area");
	ESP_RETURN_ON_ERROR(set_cursor(disp_panel, 0, 0), TAG, "Failed to set cursor");

#ifdef DSP_BPP_2
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(disp_panel->io, CMD_AUTO_RED, &auto_red_val, sizeof(auto_red_val)), TAG, "Failed to send RED pattern");
	ESP_RETURN_ON_ERROR(wait_busy(disp_panel), TAG, ERR_MSG_BUSY);
#endif
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(disp_panel->io, CMD_AUTO_BW, &auto_bw_val, sizeof(auto_bw_val)), TAG, "Failed to send BW pattern");
	ESP_RETURN_ON_ERROR(wait_busy(disp_panel), TAG, ERR_MSG_BUSY);

	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(disp_panel->io, CMD_DUC2, &duc2_val, 1), TAG, "Failed to config update");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(disp_panel->io, CMD_MA, NULL, 0), TAG, "Failed to trigger update");
	ESP_RETURN_ON_ERROR(wait_busy(disp_panel), TAG, ERR_MSG_BUSY);
	return ESP_OK;
}

esp_err_t epd4in26_refresh(esp_lcd_panel_handle_t panel)
{
	ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG, "Panel handle is NULL");
	epd4in26_panel_t * disp_panel = __containerof(panel, epd4in26_panel_t, base);
#if defined(DSP_BPP_1)
	uint8_t duc2_val = 0xF7;
#elif defined(DSP_BPP_2)
	uint8_t duc2_val = 0xC7;
#endif
	const size_t send_size = EPD4IN26_WIDTH * EPD4IN26_HEIGHT / 8;

	dither_graylevel(disp_panel);
	convert_graylevel_to_fb(disp_panel);

	// send image
	ESP_RETURN_ON_ERROR(set_area(disp_panel, 0, EPD4IN26_HEIGHT - 1, EPD4IN26_WIDTH - 1, 0),
			    TAG, "Failed to set image area");
	ESP_RETURN_ON_ERROR(set_cursor(disp_panel, 0, 0), TAG, "Failed to set cursor");

	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(disp_panel->io, CMD_RAM_BW,
			    disp_panel->_framebuffer_bw, send_size), TAG, "Send BW buf failed");
#ifdef DSP_BPP_2
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(disp_panel->io, CMD_RAM_RED,
			    disp_panel->_framebuffer_red, send_size), TAG, "Send RED buf failed");
#endif

	// trigger update
	ESP_RETURN_ON_ERROR(set_area(disp_panel, 0, 0, EPD4IN26_WIDTH - 1, EPD4IN26_HEIGHT - 1), TAG, "Failed to set refresh area");
	ESP_RETURN_ON_ERROR(set_cursor(disp_panel, 0, 0), TAG, "Failed to set cursor");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(disp_panel->io, CMD_DUC2, &duc2_val, 1), TAG, "Failed to config update");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(disp_panel->io, CMD_MA, NULL, 0), TAG, "Failed to trigger update");
	ESP_RETURN_ON_ERROR(wait_busy(disp_panel), TAG, ERR_MSG_BUSY);
	return ESP_OK;
}