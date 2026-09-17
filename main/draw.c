/* SPDX-License-Identifier: MIT */
/**
 * @file draw.c
 * @brief Drawing implementations.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_mmap_assets.h"
#include "mmap_generate_assets_font.h"

#include "esp_lv_adapter.h"
#include "esp_lv_fs.h"
#include "lvgl.h"

#include "epd4in26.h"

#include "draw.h"
#include "battery.h"
#include "settings.h"
#include "message.h"

typedef struct {
	mmap_assets_handle_t mmap_handle;
	esp_lv_fs_handle_t fs_handle;
	char drive_letter;
	bool mounted;
} asset_ctx_t;

#define LVGL_LOCK_TIMEOUT_MS	(1000)

static asset_ctx_t font_assets = { .drive_letter = 'F' };

extern bat_state_t cur_bat_sta;
extern bool battery_charging;
extern lv_img_dsc_t img_dsc; /**< Defined and updated in bt.c */
static const char * TAG = "DRAW";

static const char * bat_syms[] = {
	[BAT_STA_FULL] = LV_SYMBOL_BATTERY_FULL,
	[BAT_STA_3] = LV_SYMBOL_BATTERY_3,
	[BAT_STA_2] = LV_SYMBOL_BATTERY_2,
	[BAT_STA_1] = LV_SYMBOL_BATTERY_1,
	[BAT_STA_EMPTY] = LV_SYMBOL_BATTERY_EMPTY
};

static const char * FONTS[] = {
	[0] = "F:Ballet.ttf",
	[1] = "F:BlakaHollow.ttf",
	[2] = "F:BungeeShade.ttf",
	[3] = "F:CherryBombOne.ttf",
	[4] = "F:Ewert.ttf",
	[5] = "F:FleurDeLeah.ttf",
	[6] = "F:KumarOneOutline.ttf",
	[7] = "F:Megrim.ttf",
	[8] = "F:Molle.ttf",
	[9] = "F:MomoTrustDisplay.ttf",
	[10] = "F:Ranchers.ttf",
	[11] = "F:SendFlowers.ttf",
	[12] = "F:UnifrakturMaguntia.ttf",
	[13] = "F:YuyuShort.ttf",
};

static void draw_bg_image(void)
{
	lv_obj_t * bg_img = lv_image_create(lv_scr_act());
	lv_image_set_src(bg_img, &img_dsc);
	lv_obj_set_size(bg_img, EPD4IN26_WIDTH, EPD4IN26_HEIGHT);
	lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);
}

static const char * day_names[] = {
	"Montag",
	"Dienstag",
	"Mittwoch",
	"Donnerstag",
	"Freitag",
	"Samstag",
	"Sonntag"
};

static const char * mon_names[] = {
	"Januar",
	"Februar",
	"März",
	"April",
	"Mai",
	"Juni",
	"Juli",
	"August",
	"September",
	"Oktober",
	"November",
	"Dezember"
};

static void draw_date(const lv_font_t * font, lv_color_t text_clr, lv_color_t bg_clr)
{
	time_t now;
	struct tm time_tm;
	time(&now);
	localtime_r(&now, &time_tm);
	lv_obj_t * date_lbl = lv_label_create(lv_scr_act());
	lv_label_set_text_fmt(date_lbl, "%s, der %02d. %s %04d",
			      day_names[time_tm.tm_wday], time_tm.tm_mday,
			      mon_names[time_tm.tm_mon + 1], time_tm.tm_year + 1900);
	ESP_LOGI(TAG, "%s, der %02d. %s %04d", day_names[time_tm.tm_wday], time_tm.tm_mday,
		 mon_names[time_tm.tm_mon + 1], time_tm.tm_year + 1900);
	lv_obj_align(date_lbl, LV_ALIGN_TOP_LEFT, 0, 0);
	if (font != NULL) {
		lv_obj_set_style_text_font(date_lbl, font, LV_PART_MAIN);
	}
	lv_obj_set_style_text_color(date_lbl, text_clr, LV_PART_MAIN);
	lv_obj_set_style_bg_color(date_lbl, bg_clr, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(date_lbl, LV_OPA_COVER, LV_PART_MAIN);
}

static void draw_rem_days(time_t tgt_time, const lv_font_t * font, lv_color_t text_clr, lv_color_t bg_clr)
{
	time_t now;

	time(&now);
	int days_diff = ceill(difftime(tgt_time, now) / (60 * 60 * 24));
	if (days_diff < 0) { return; }
	lv_obj_t * rem_lbl = lv_label_create(lv_scr_act());
	if (days_diff > 1) {
		lv_label_set_text_fmt(rem_lbl, "Nur noch %d Tage", days_diff);
	} else if (days_diff == 1) {
		lv_label_set_text_fmt(rem_lbl, "Morgen ist es soweit");
	} else if (days_diff == 0) {
		lv_label_set_text_fmt(rem_lbl, "Heute ist es soweit");
	}

	lv_obj_align(rem_lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
	if (font != NULL) {
		lv_obj_set_style_text_font(rem_lbl, font, LV_PART_MAIN);
	}
	lv_obj_set_style_text_color(rem_lbl, text_clr, LV_PART_MAIN);
	lv_obj_set_style_bg_color(rem_lbl, bg_clr, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(rem_lbl, LV_OPA_COVER, LV_PART_MAIN);
}

static void draw_battery_state(lv_color_t text_clr, lv_color_t bg_clr)
{
	const char * chg_sym = LV_SYMBOL_CHARGE;
	const char * bat_sym = bat_syms[cur_bat_sta];
	lv_obj_t * bat_lvl_lbl = lv_label_create(lv_scr_act());
	if (battery_charging) {
		lv_label_set_text_fmt(bat_lvl_lbl, "%s %s", chg_sym, bat_sym);
	} else {
		lv_label_set_text_fmt(bat_lvl_lbl, "%s", bat_sym);
	}
	lv_obj_align(bat_lvl_lbl, LV_ALIGN_TOP_RIGHT, -10, 0);
	lv_obj_set_style_text_color(bat_lvl_lbl, text_clr, LV_PART_MAIN);
	lv_obj_set_style_bg_color(bat_lvl_lbl, bg_clr, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(bat_lvl_lbl, LV_OPA_COVER, LV_PART_MAIN);
}

/* ========================================================================== */
/* == INTERFACE ============================================================= */
/* ========================================================================== */

esp_err_t draw_init(void)
{
	esp_err_t ret;

	const mmap_assets_config_t mmap_cfg = {
		.partition_label = "assets_font",
		.max_files = MMAP_ASSETS_FONT_FILES,
		.checksum = MMAP_ASSETS_FONT_CHECKSUM,
		.flags = { .mmap_enable = 1 }
	};
	ESP_RETURN_ON_ERROR(mmap_assets_new(&mmap_cfg, &font_assets.mmap_handle),
			    TAG, "Failed to init font mmap");
	size_t file_count = mmap_assets_get_stored_files(font_assets.mmap_handle);
	if (file_count == 0) {
		mmap_assets_del(font_assets.mmap_handle);
		font_assets.mmap_handle = NULL;
		return ESP_ERR_NOT_FOUND;
	}

	const fs_cfg_t fs_cfg = {
		.fs_letter = 'F',
		.fs_nums = (int)file_count,
		.fs_assets = font_assets.mmap_handle
	};
	if ((ret = esp_lv_adapter_fs_mount(&fs_cfg, &font_assets.fs_handle)) != ESP_OK) {
		mmap_assets_del(font_assets.mmap_handle);
		font_assets.mmap_handle = NULL;
		return ret;
	}
	font_assets.drive_letter = 'F';
	font_assets.mounted = true;
	ESP_LOGI(TAG, "Mounted '%s' to '%c': (%zu files)", mmap_cfg.partition_label,
		 fs_cfg.fs_letter, file_count);
	return ESP_OK;
}

void draw_content(void)
{
	esp_err_t ret;
	nvs_handle_t hndl;
	time_t tgt_time;
	static font_sty_t font_sty;
	static esp_lv_adapter_ft_font_handle_t font_handle = NULL;

	ret = nvs_open(NVS_SETTINGS_NAME, NVS_READONLY, &hndl);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to open NVS. Error %d", ret);
		return;
	}
	ret = nvs_get_u16(hndl, NVS_SET_STY_NAME, (uint16_t *)&font_sty);
	if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to load font style flags");

	ret = nvs_get_i64(hndl, NVS_SET_TGT_NAME, &tgt_time);
	if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to load target date");
	nvs_close(hndl);
	ESP_LOGI(TAG, "Font style flags: Font ID: %d, Font Size ID: %d, FG white: %d, BG white: %d",
		 font_sty.font_id, font_sty.font_size, font_sty.f_fg_clr, font_sty.f_bg_clr);

	lv_color_t fg_clr = font_sty.f_fg_clr ? lv_color_white() : lv_color_black();
	lv_color_t bg_clr = font_sty.f_bg_clr ? lv_color_white() : lv_color_black();

	esp_lv_adapter_ft_font_style_t font_style = ESP_LV_ADAPTER_FT_FONT_STYLE_NORMAL;
	if (font_sty.is_bold && font_sty.is_italic) {
		font_style = ESP_LV_ADAPTER_FT_FONT_STYLE_BOLD_ITALIC;
	} else if (font_sty.is_bold) {
		font_style = ESP_LV_ADAPTER_FT_FONT_STYLE_BOLD;
	} else if (font_sty.is_italic) {
		font_style = ESP_LV_ADAPTER_FT_FONT_STYLE_ITALIC;
	}

	ESP_ERROR_CHECK(esp_lv_adapter_lock(LVGL_LOCK_TIMEOUT_MS));
	lv_obj_clean(lv_scr_act());

	if (font_handle != NULL) {
		esp_lv_adapter_ft_font_deinit(font_handle);
		font_handle = NULL;
	}
	const esp_lv_adapter_ft_font_config_t font_cfg = ESP_LV_ADAPTER_FT_FONT_FILE_CONFIG(
		FONTS[font_sty.font_id], font_sty.font_size, font_style);
	esp_lv_adapter_ft_font_init(&font_cfg, &font_handle);
	const lv_font_t * font = esp_lv_adapter_ft_font_get(font_handle);
	if (font == NULL) { ESP_LOGW(TAG, "Failed to load font"); }

	draw_bg_image();
	draw_date(font, fg_clr, bg_clr);
	draw_rem_days(tgt_time, font, fg_clr, bg_clr);
	draw_battery_state(fg_clr, bg_clr);
	esp_lv_adapter_unlock();
}

esp_err_t save_image_to_flash(void)
{
	const esp_partition_t * p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
		ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, IMAGE_STORAGE_NAME);
	ESP_RETURN_ON_FALSE(p, ESP_ERR_NOT_FOUND, TAG, "Failed to find image storage");

	ESP_LOGI(TAG, "Save image (w/h: %d/%d, size: %zu)", img_dsc.header.w, img_dsc.header.h, img_dsc.data_size);
	
	const size_t save_len = sizeof(img_dsc) + img_dsc.data_size;
	ESP_RETURN_ON_FALSE(save_len < p->size, ESP_ERR_NO_MEM, TAG, "Image size too big (is: %zu, max: %zu)", save_len, p->size);
	const size_t erase_len = (save_len + 4095) & ~4095; // 4KB aligned

	ESP_RETURN_ON_ERROR(esp_partition_erase_range(p, 0, erase_len), TAG, "Failed to erase image storage");
	ESP_RETURN_ON_ERROR(esp_partition_write(p, 0, &img_dsc, sizeof(img_dsc)), TAG, "Failed to save image header");
	ESP_RETURN_ON_ERROR(esp_partition_write(p, sizeof(img_dsc), img_dsc.data, img_dsc.data_size), TAG, "Failed to save image data");
	ESP_LOGI(TAG, "Image saved successfully");
	return ESP_OK;
}

esp_err_t load_image_from_flash(void)
{
	if (img_dsc.data != NULL) { free((void *)img_dsc.data); }

	const esp_partition_t * p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
		ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, IMAGE_STORAGE_NAME);
	ESP_RETURN_ON_FALSE(p, ESP_ERR_NOT_FOUND, TAG, "Failed to find image storage");
	
	ESP_RETURN_ON_ERROR(esp_partition_read(p, 0, &img_dsc, sizeof(img_dsc)), TAG, "Failed to load image header");
	ESP_RETURN_ON_FALSE(img_dsc.header.magic == LV_IMAGE_HEADER_MAGIC, ESP_ERR_INVALID_STATE,
			    TAG, "Loaded image header invalid");
	ESP_LOGI(TAG, "Loading image (w/h: %d/%d, size: %zu)", img_dsc.header.w, img_dsc.header.h, img_dsc.data_size);
	img_dsc.data = heap_caps_malloc(img_dsc.data_size, MALLOC_CAP_SPIRAM);
	ESP_RETURN_ON_FALSE(img_dsc.data, ESP_ERR_NO_MEM, TAG, "Failed to allocate image buffer");
	ESP_RETURN_ON_ERROR(esp_partition_read(p, sizeof(img_dsc), (void *)img_dsc.data, img_dsc.data_size),
			    TAG, "Failed to load image data");
	return ESP_OK;
}

esp_err_t set_image_default(void)
{
	const size_t img_buf_size = EPD4IN26_WIDTH * EPD4IN26_HEIGHT;
	void * img_buf = heap_caps_malloc(img_buf_size, MALLOC_CAP_SPIRAM);
	ESP_RETURN_ON_FALSE(img_buf, ESP_ERR_NO_MEM, TAG, "Failed to allocate image buffer");
	memset(img_buf, 0x00, img_buf_size);

	img_dsc = (lv_img_dsc_t) {
		.header = {
			.magic = LV_IMAGE_HEADER_MAGIC,
			.cf = LV_COLOR_FORMAT_L8,
			.flags = 0,
			.w = EPD4IN26_WIDTH,
			.h = EPD4IN26_HEIGHT,
			.stride = EPD4IN26_WIDTH, // L8 -> 1bpp
		},
		.data = img_buf,
		.data_size = EPD4IN26_WIDTH * EPD4IN26_HEIGHT,
	};
	return ESP_OK;
}