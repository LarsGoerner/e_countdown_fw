/* SPDX-License-Identifier: MIT */
/**
 * @file display.c
 * @brief Display driver and LVGL adapter setup and display handling.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lv_adapter.h"

#include "epd4in26.h"

#include "display.h"

#define DSP_SPI		SPI2_HOST
#define DSP_MOSI_NUM	GPIO_NUM_9
#define DSP_SCK_NUM	GPIO_NUM_10
#define DSP_CS_NUM	GPIO_NUM_11
#define DSP_DC_NUM	GPIO_NUM_12
#define DSP_RST_NUM	GPIO_NUM_13
#define DSP_BUSY_NUM	GPIO_NUM_14

#define DSP_BUF_HEIGHT	20
#define DSP_BUF_SIZE_PX	(EPD4IN26_WIDTH * DSP_BUF_HEIGHT)

static const char * TAG = "DISPLAY";
static esp_lcd_panel_handle_t panel_handle = NULL;

static void epd_lvgl_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
	esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
	esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
	lv_display_flush_ready(disp);
}

/* ========================================================================== */
/* == INTERFACE ============================================================= */
/* ========================================================================== */

esp_err_t display_init(void)
{
	// SPI BUS: Display
	spi_bus_config_t spi_cfg = {
		.mosi_io_num = DSP_MOSI_NUM,
		.miso_io_num = -1,
		.sclk_io_num = DSP_SCK_NUM,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.data4_io_num = -1,
		.data5_io_num = -1,
		.data6_io_num = -1,
		.data7_io_num = -1,
		.max_transfer_sz = EPD4IN26_WIDTH * EPD4IN26_HEIGHT / 8 + 64 // + 64 for padding
	};
	ESP_RETURN_ON_ERROR(spi_bus_initialize(DSP_SPI, &spi_cfg, SPI_DMA_CH_AUTO),
			    TAG, "Failed to init SPI bus");

	esp_lcd_panel_io_handle_t io_handle = NULL;
	ESP_LOGI(TAG, "Initialize panel IO");
	esp_lcd_panel_io_spi_config_t io_cfg = {
		.dc_gpio_num = DSP_DC_NUM,
		.cs_gpio_num = DSP_CS_NUM,
		.pclk_hz = EPD4IN26_CLK_HZ,
		.lcd_cmd_bits = 8,
		.lcd_param_bits = 8,
		.spi_mode = 0,
		.trans_queue_depth = 10,
		.on_color_trans_done = NULL,
	};
	ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DSP_SPI,
			    &io_cfg, &io_handle), TAG, "Failed to create panel io");

	esp_lcd_epd4in26_config_t epd4in26_cfg = {
		.busy_gpio_num = DSP_BUSY_NUM
	};
	esp_lcd_panel_dev_config_t panel_cfg = {
		.reset_gpio_num = DSP_RST_NUM,
		.flags.reset_active_high = false,
		.vendor_config = &epd4in26_cfg,
		.bits_per_pixel = 16,
		.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
		.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
	};
	ESP_RETURN_ON_ERROR(esp_lcd_new_panel_epd4in26(io_handle, &panel_cfg, &panel_handle),
			    TAG, "Failed to create new panel");

	// Init lvgl adapter
	ESP_LOGI(TAG, "Initialize lvgl adapter");
	esp_lv_adapter_config_t lv_ada_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
	ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&lv_ada_cfg), TAG, "Failed to init lv adapter");
	//esp_lv_adapter_display_config_t lv_disp_cfg = ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(
	//	panel_handle, io_handle, EPD4IN26_WIDTH, EPD4IN26_HEIGHT, ESP_LV_ADAPTER_ROTATE_0);
	esp_lv_adapter_display_config_t lv_disp_cfg = {
		.panel = panel_handle,
		.panel_io = io_handle,
		.profile = {
			.interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
			.rotation = ESP_LV_ADAPTER_ROTATE_0,
			.hor_res = EPD4IN26_WIDTH,
			.ver_res = EPD4IN26_HEIGHT,

			.buffer_height = DSP_BUF_HEIGHT,
			.use_psram = false,
			.require_double_buffer = false,

			.enable_ppa_accel = false,
			.mono_layout = ESP_LV_ADAPTER_MONO_LAYOUT_NONE,
		},
		.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
		.te_sync = { .gpio_num = -1 }
	};
	lv_display_t * disp = esp_lv_adapter_register_display(&lv_disp_cfg);
	ESP_RETURN_ON_FALSE(disp, ESP_ERR_NOT_FINISHED, TAG, "Failed to register lv adapter");
	lv_display_set_user_data(disp, panel_handle);
	lv_display_set_flush_cb(disp, epd_lvgl_flush_cb);
	ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "Failed to start lv adapter");
	lv_display_set_render_mode(lv_display_get_default(), LV_DISPLAY_RENDER_MODE_PARTIAL);
	return ESP_OK;
}

esp_err_t display_on(void)
{
	ESP_LOGD(TAG, "Resetting display");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "Failed to reset panel");
	ESP_LOGD(TAG, "Turning on display");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "Failed to turn on panel");
	return ESP_OK;
}

esp_err_t display_gen_test(uint8_t tid)
{
	ESP_RETURN_ON_ERROR(epd4in26_gen_test(panel_handle, (epd4in26_test_id_t)tid),
			    TAG, "Failed to send test image generator settings");
	return ESP_OK;
}

esp_err_t display_refresh(void)
{
	ESP_LOGI(TAG, "Trigger display refresh");
	ESP_RETURN_ON_ERROR(epd4in26_refresh(panel_handle), TAG, "Failed to refresh panel");
	return ESP_OK;
}

esp_err_t display_off(void)
{
	ESP_LOGI(TAG, "Turn off display");
	ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, false), TAG, "Failed to turn off panel");
	return ESP_OK;
}
