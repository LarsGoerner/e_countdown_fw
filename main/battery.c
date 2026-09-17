/* SPDX-License-Identifier: MIT */
/**
 * @file battery.c
 * @brief Battery measurement task implementations.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#include <stdint.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"

#include "max1704x.h"
#include "services/bas/ble_svc_bas.h"

#include "battery.h"
#include "message.h"

extern MessageBufferHandle_t main_msg_buf; /**< Reference to external main message buffer */
static const char * TAG = "BATTERY"; /**< Logging tag string */
static SemaphoreHandle_t bat_upd_sem; /**< Battery update semaphore */

/**
 * @name Hardware defines
 * @{
 */

/** @brief I2C bus number. */
#define FG_I2C		I2C_NUM_0
/** @brief I2C CLK pin number. */
#define FG_SCL_NUM	GPIO_NUM_4
/** @brief I2C data pin number */
#define FG_SDA_NUM	GPIO_NUM_5
/** @brief Alert interrupt pin number */
#define FG_ALRT_NUM	GPIO_NUM_6

/** @} */

static uint8_t battery_lvl_perc = 0; /**< Internal battery level value */

/**
 * @brief Current battery status.
 * @note Used by others externally.
 * 
 * @sa bat_state_t
 */
bat_state_t cur_bat_sta = BAT_STA_EMPTY;

/**
 * @brief Battery charging indicator.
 * @note Used by others externally.
 */
bool battery_charging = false;

/**
 * @brief Alert pin interrupt handler.
 * 
 * @param[in] arg Unused
 */
static void IRAM_ATTR fg_alrt_isr_handler(void * arg)
{
	ESP_UNUSED(arg);
	BaseType_t prio_task_woken = pdFALSE;
	xSemaphoreGiveFromISR(bat_upd_sem, &prio_task_woken);
	portYIELD_FROM_ISR(prio_task_woken);
}

/**
 * @brief Battery measure task
 * @details Periodically reads fuel gauge and updates the BAS BLE service
 * 
 * @param[in] args Unused
 */
static void bat_meas_task(void * args)
{
	ESP_UNUSED(args);
	max1704x_t fg_dev = { 0 };
	max1704x_config_t cfg = { 0 };
	max1704x_status_t sta = { 0 };
	uint16_t fg_ver = 0;

	bat_upd_sem = xSemaphoreCreateBinary();

	fg_dev.model = MAX17048_9;

	ESP_ERROR_CHECK(i2cdev_init());
	fg_dev.i2c_dev.cfg.sda_pullup_en = true;
	fg_dev.i2c_dev.cfg.scl_pullup_en = true;
	ESP_ERROR_CHECK(max1704x_init_desc(&fg_dev, FG_I2C, FG_SDA_NUM, FG_SCL_NUM));
#if 0
	// ONLY USE ON BATTERY SWAP
	ESP_ERROR_CHECK(max1704x_quickstart(&fg_dev));
#endif
	ESP_ERROR_CHECK(max1704x_get_version(&fg_dev, &fg_ver));
	ESP_LOGI(TAG, "FG version: %d", fg_ver);

	// setup alert for soc change
	ESP_ERROR_CHECK(max1704x_get_config(&fg_dev));
	cfg = fg_dev.config;
	cfg.soc_change_alert = true;
	ESP_ERROR_CHECK(max1704x_set_config(&fg_dev, &cfg));

	ESP_ERROR_CHECK(max1704x_get_status(&fg_dev));
	sta = fg_dev.status;
	sta.soc_change = false;
	sta.soc_low = false;
	ESP_ERROR_CHECK(max1704x_set_status(&fg_dev, &sta));

	ESP_ERROR_CHECK(max1704x_get_config(&fg_dev));
	cfg = fg_dev.config;
	cfg.alert_status = false;
	ESP_ERROR_CHECK(max1704x_set_config(&fg_dev, &cfg));

	const gpio_config_t io_cfg = {
		.intr_type = GPIO_INTR_NEGEDGE,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.mode = GPIO_MODE_INPUT,
		.pin_bit_mask = (1ULL << FG_ALRT_NUM)
	};
	gpio_config(&io_cfg);
	gpio_install_isr_service(0);
	gpio_isr_handler_add(FG_ALRT_NUM, fg_alrt_isr_handler, NULL);

	xSemaphoreGive(bat_upd_sem);

	for (;;) {
		bat_state_t new_bat_sta;
		float soc;
		float crate;
		bool now_charging;
		bool refresh_needed = false;

		xSemaphoreTake(bat_upd_sem, portMAX_DELAY);

		ESP_ERROR_CHECK(max1704x_get_status(&fg_dev));
		ESP_LOGD(TAG, "FG: %d %d %d %d %d %d %d", 
			 fg_dev.status.reset_indicator,
			 fg_dev.status.soc_change,
			 fg_dev.status.soc_low,
			 fg_dev.status.voltage_high,
			 fg_dev.status.voltage_low,
			 fg_dev.status.voltage_reset,
			 fg_dev.status.vreset_alert);
		sta = fg_dev.status;
		sta.reset_indicator = false;
		sta.soc_change = false;
		sta.soc_low = false;
		ESP_ERROR_CHECK(max1704x_set_status(&fg_dev, &sta));

		ESP_ERROR_CHECK_WITHOUT_ABORT(max1704x_get_config(&fg_dev));
		cfg = fg_dev.config;
		cfg.alert_status = false;
		ESP_ERROR_CHECK_WITHOUT_ABORT(max1704x_set_config(&fg_dev, &cfg));

		ESP_ERROR_CHECK_WITHOUT_ABORT(max1704x_get_soc(&fg_dev, &soc));
		ESP_ERROR_CHECK_WITHOUT_ABORT(max1704x_get_crate(&fg_dev, &crate));
		battery_lvl_perc = lroundf(soc);
		now_charging = crate > 0.0f;

		ESP_LOGI(TAG, "Battery: SoC= %.3f, crate=%.3f", soc, crate);

		if (battery_lvl_perc > 80) { new_bat_sta = BAT_STA_FULL; }
		else if (battery_lvl_perc > 60) { new_bat_sta = BAT_STA_3; }
		else if (battery_lvl_perc > 40) { new_bat_sta = BAT_STA_2; }
		else if (battery_lvl_perc > 15) { new_bat_sta = BAT_STA_1; }
		else { new_bat_sta = BAT_STA_EMPTY; }

		if (now_charging != battery_charging) {
			battery_charging = now_charging;
			refresh_needed = true;
		}

		if (cur_bat_sta != new_bat_sta) {
			battery_charging = now_charging;
			cur_bat_sta = new_bat_sta;
			refresh_needed = true;
		}

		ble_svc_bas_battery_level_set(battery_lvl_perc);

		if (refresh_needed) {
			const message_t main_msg = { .event_id = EV_BAT };
			xMessageBufferSend(main_msg_buf, (const void *)&main_msg, sizeof(message_t), 0);
		}
	}
}

/* ========================================================================== */
/* == INTERFACE ============================================================= */
/* ========================================================================== */

/**
 * @brief Initialize battery thread
 */
void battery_init(void)
{
	xTaskCreate(bat_meas_task, "BatMeas", 2048, NULL, 1, NULL);
}