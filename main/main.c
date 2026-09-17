/* SPDX-License-Identifier: MIT */
/**
 * @file main.c
 * @brief Main loop.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"
#include "esp_mmap_assets.h"
#include "mmap_generate_assets_font.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"

#include "bt.h"
#include "display.h"
#include "draw.h"
#include "battery.h"
#include "message.h"

static const char * TAG = "MAIN";

MessageBufferHandle_t main_msg_buf;

/**
 * @brief Initialize hardware an other software components.
 * Then enter an endless loop for display refresh trigger.
 */
void app_main(void)
{
	esp_err_t ret;

	main_msg_buf = xMessageBufferCreate(4 * (sizeof(message_t) + sizeof(size_t)));

	// nvs flash
	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	ret = load_image_from_flash();
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "Failed to load BG image. Error %d", ret);
		ESP_ERROR_CHECK(set_image_default());
	}

	ESP_ERROR_CHECK(bt_init());
	battery_init();
	ESP_ERROR_CHECK(display_init());
	draw_init();

	for (;;) {
		message_t msg;
		time_t now;
		time_t next_midnight;
		struct tm next_midnight_tm;
		
		time(&now);
		localtime_r(&now, &next_midnight_tm);
		next_midnight_tm.tm_mday++;
		next_midnight_tm.tm_hour = 0;
		next_midnight_tm.tm_min = 5;
		next_midnight_tm.tm_sec = 0;
		next_midnight_tm.tm_isdst = -1;
		next_midnight = mktime(&next_midnight_tm);

		if (xMessageBufferReceive(main_msg_buf, &msg, sizeof(msg), pdMS_TO_TICKS(next_midnight * 1000)) != 0) {
			switch (msg.event_id) {
			case EV_TST:
				ESP_LOGI(TAG, "Do test image update (TID %d)", msg.test_id);
				display_on();
				display_gen_test(msg.test_id);
				display_off();
				break;
			case EV_BG_IMG:
				save_image_to_flash();
				__attribute__((fallthrough));
			default:
				display_on();
				draw_content();
				vTaskDelay(pdMS_TO_TICKS(2000));
				display_refresh();
				display_off();
			}
		} else {
			ESP_LOGI(TAG, "Main thread woke up for refresh");
			display_on();
			draw_content();
			vTaskDelay(pdMS_TO_TICKS(2000));
			display_refresh();
			display_off();
		}
	}
}