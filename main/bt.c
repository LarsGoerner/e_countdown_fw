/* SPDX-License-Identifier: MIT */
/**
 * @file bt.c
 * @brief Bluetooth LE interface implementations.
 * @author Lars Görner
 * 
 * @date 17.09.2026 - Initial implementation
 */

#include <time.h>
#include <sys/time.h>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/dis/ble_svc_dis.h"
#include "services/bas/ble_svc_bas.h"
#include "services/cts/ble_svc_cts.h"

#include <freertos/message_buffer.h>

#include <epd4in26.h>

#include "bt.h"
#include "lvgl.h"
#include "message.h"
#include "settings.h"

static const char * TAG = "APP_BT";
static struct ble_svc_cts_local_time_info cts_local_info = { .timezone = 0, .dst_offset = TIME_STANDARD };
static struct timeval last_updated;
static uint8_t adjust_reason;

lv_img_dsc_t img_dsc = { 0 }; /**< Container for receiving image */
static uint8_t * img_data_buf = NULL; /**< img_dsc data buffer */
static size_t rec_data_offs = 0; /**< Already received image data bytes */
static bool is_rec = false; /**< Currently receiving image data */
extern MessageBufferHandle_t main_msg_buf;

#if 1
#define BT_DEV_NAME	"Annett E-CNT"
#else
#define BT_DEV_NAME	"Achim E-CNT"
#endif

/* ========================================================================== */
/* == GATT SERVICES ========================================================= */
/* ========================================================================== */

/* == IMAGE SERVICE ================================================ */

/** @brief IMS service UUID */
static const ble_uuid128_t gatt_ims_svc_uuid = BLE_UUID128_INIT(
	0x00, 0x75, 0xbf, 0x32, 0xe3, 0xda, 0xe4, 0xa9,
	0xc8, 0x44, 0x44, 0xda, 0x08, 0x20, 0x43, 0x62);
/** @brief CTRL character UUID */
static const ble_uuid128_t gatt_ims_char_ctrl_uuid = BLE_UUID128_INIT(
	0x01, 0x75, 0xbf, 0x32, 0xe3, 0xda, 0xe4, 0xa9,
	0xc8, 0x44, 0x44, 0xda, 0x08, 0x20, 0x43, 0x62);
/** @brief DATA character UUID */
static const ble_uuid128_t gatt_ims_char_data_uuid = BLE_UUID128_INIT(
	0x02, 0x75, 0xbf, 0x32, 0xe3, 0xda, 0xe4, 0xa9,
	0xc8, 0x44, 0x44, 0xda, 0x08, 0x20, 0x43, 0x62);
/** @brief Test ID character UUID */
static const ble_uuid128_t gatt_ims_char_tid_uuid = BLE_UUID128_INIT(
	0x03, 0x75, 0xbf, 0x32, 0xe3, 0xda, 0xe4, 0xa9,
	0xc8, 0x44, 0x44, 0xda, 0x08, 0x20, 0x43, 0x62);
/** @brief Style flags character UUID */
static const ble_uuid128_t gatt_ims_char_sty_uuid = BLE_UUID128_INIT(
	0x04, 0x75, 0xbf, 0x32, 0xe3, 0xda, 0xe4, 0xa9,
	0xc8, 0x44, 0x44, 0xda, 0x08, 0x20, 0x43, 0x62);
/** @brief Target date character UUID */
static const ble_uuid128_t gatt_ims_char_tgt_uuid = BLE_UUID128_INIT(
	0x05, 0x75, 0xbf, 0x32, 0xe3, 0xda, 0xe4, 0xa9,
	0xc8, 0x44, 0x44, 0xda, 0x08, 0x20, 0x43, 0x62);
/** @brief Display dimensions character UUID */
static const ble_uuid128_t gatt_ims_char_dim_uuid = BLE_UUID128_INIT(
	0x06, 0x75, 0xbf, 0x32, 0xe3, 0xda, 0xe4, 0xa9,
	0xc8, 0x44, 0x44, 0xda, 0x08, 0x20, 0x43, 0x62);

typedef struct {
	uint32_t x_offs;
	uint32_t y_offs;
	uint32_t width;
	uint32_t height;
	uint32_t clr_fmt; /** must be of type lv_color_format_t */
} its_ctrl_data_t;
static its_ctrl_data_t its_data = { 0 }; /**< ITS CTRL char data */

static int its_char_ctrl_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt * ctx, void * arg)
{
	if (ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
		uint16_t len = OS_MBUF_PKTLEN(ctx->om);

		if (len == sizeof(its_ctrl_data_t)) {
			os_mbuf_copydata(ctx->om, 0, len, &its_data);
			if (img_data_buf) {
				free(img_data_buf);
				img_data_buf = NULL;
				img_dsc.data = NULL;
			}
			img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
			img_dsc.header.cf = its_data.clr_fmt;
			img_dsc.header.flags = 0;
			img_dsc.header.w = its_data.width;
			img_dsc.header.h = its_data.height;
			img_dsc.header.stride = its_data.width * lv_color_format_get_bpp(its_data.clr_fmt) / 8;
			img_dsc.header.reserved_2 = 0;
			img_dsc.data_size = its_data.width * its_data.height * lv_color_format_get_bpp(its_data.clr_fmt) / 8;
			img_data_buf = heap_caps_malloc(img_dsc.data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
			if (img_data_buf == NULL) {
				ESP_LOGE(TAG, "Failed to allocate image buffer (size %lu)", img_dsc.data_size);
				return BLE_ATT_ERR_INSUFFICIENT_RES;
			}
			img_dsc.data = img_data_buf;
			rec_data_offs = 0;
			is_rec = true;
			ESP_LOGI(TAG, "Ready to receive image. Expecting %lu bytes", img_dsc.data_size);
		} else { ESP_LOGE(TAG, "ITS CTRL command with wrong size. Should: %zu, is: %zu", sizeof(its_ctrl_data_t), len); }
	}
	return 0;
}

static int its_char_data_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt * ctx, void * arg)
{
	if (ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
		if (!is_rec || img_dsc.data == NULL) { return BLE_ATT_ERR_UNLIKELY; }
		
		uint16_t chunk_len = OS_MBUF_PKTLEN(ctx->om);
		if (rec_data_offs + chunk_len > img_dsc.data_size) {
			ESP_LOGE(TAG, "Buffer overflow detected");
			return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
		}

		os_mbuf_copydata(ctx->om, 0, chunk_len, img_data_buf + rec_data_offs);
		rec_data_offs += chunk_len;
		if (rec_data_offs >= img_dsc.data_size) {
			const message_t msg = { .event_id = EV_BG_IMG };
			is_rec = false;
			ESP_LOGI(TAG, "Image transfer completed");
			xMessageBufferSend(main_msg_buf, &msg, sizeof(msg), 0);
		}
	}
	return 0;
}

static int ims_char_tid_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt * ctx, void * arg)
{
	if (ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
		uint8_t test_id;
		uint16_t len = OS_MBUF_PKTLEN(ctx->om);
		if (len == sizeof(test_id)) {
			os_mbuf_copydata(ctx->om, 0, len, &test_id);
			ESP_LOGI(TAG, "Got new test ID %d", test_id);
			const message_t msg = { .event_id = EV_TST, .test_id = test_id };
			xMessageBufferSend(main_msg_buf, &msg, sizeof(msg), 0);
		}
	}
	return 0;
}

static int ims_char_sty_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt * ctx, void * arg)
{
	if (ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
		font_sty_t font_style_flags;
		uint16_t len = OS_MBUF_PKTLEN(ctx->om);
		if (len == sizeof(font_style_flags)) {
			os_mbuf_copydata(ctx->om, 0, len, &font_style_flags);
			nvs_handle_t hndl;
			if (nvs_open(NVS_SETTINGS_NAME, NVS_READWRITE, &hndl) == ESP_OK) {
				nvs_set_u16(hndl, NVS_SET_STY_NAME, *(uint16_t *)&font_style_flags);
				nvs_commit(hndl);
				nvs_close(hndl);
				const message_t msg = { .event_id = EV_STYLE };
				xMessageBufferSend(main_msg_buf, &msg, sizeof(msg), 0);
			}
		}
	} else if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		nvs_handle_t hndl;
		font_sty_t font_style_flags = { 0 };
		if (nvs_open(NVS_SETTINGS_NAME, NVS_READONLY, &hndl) == ESP_OK) {
			nvs_get_u16(hndl, NVS_SET_STY_NAME, (uint16_t *)&font_style_flags);
			nvs_close(hndl);
			os_mbuf_append(ctx->om, &font_style_flags, sizeof(font_style_flags));
		}
	}
	return 0;
}

static int ims_char_tgt_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt * ctx, void * arg)
{
	if (ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
		time_t date_time;
		uint16_t len = OS_MBUF_PKTLEN(ctx->om);
		if (len == sizeof(date_time)) {
			os_mbuf_copydata(ctx->om, 0, len, &date_time);
			nvs_handle_t hndl;
			if (nvs_open(NVS_SETTINGS_NAME, NVS_READWRITE, &hndl) == ESP_OK) {
				nvs_set_i64(hndl, NVS_SET_TGT_NAME, date_time);
				nvs_commit(hndl);
				nvs_close(hndl);
				const message_t msg = { .event_id = EV_TGT_D };
				xMessageBufferSend(main_msg_buf, &msg, sizeof(msg), 0);
			} else {
				ESP_LOGE(TAG, "Failed to save target date");
				return BLE_ATT_ERR_UNLIKELY;
			}
		}
	} else if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		nvs_handle_t hndl;
		time_t date_time;
		if (nvs_open(NVS_SETTINGS_NAME, NVS_READONLY, &hndl) == ESP_OK) {
			nvs_get_i64(hndl, NVS_SET_TGT_NAME, &date_time);
			nvs_close(hndl);
			os_mbuf_append(ctx->om, &date_time, sizeof(date_time));
			const message_t msg = { .event_id = EV_TGT_D };
			xMessageBufferSend(main_msg_buf, &msg, sizeof(msg), 0);
		} else { return BLE_ATT_ERR_UNLIKELY; }
	}
	return 0;
}

static int ims_char_dim_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt * ctx, void * arg)
{
	if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		const uint16_t dimensions[2] = { os_bswap_16(EPD4IN26_WIDTH), os_bswap_16(EPD4IN26_HEIGHT) };
		os_mbuf_append(ctx->om, dimensions, sizeof(dimensions));
	}
	return 0;
}

/* == GATT SERVER =========================================================== */

static const struct ble_gatt_svc_def gatt_svcs[] = {
	// IMS - Image Service
	{
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = &gatt_ims_svc_uuid.u,
		.characteristics = (struct ble_gatt_chr_def[]) {
			{
				// Control characteristic
				.uuid = &gatt_ims_char_ctrl_uuid.u,
				.access_cb = &its_char_ctrl_cb,
				.flags = BLE_GATT_CHR_F_WRITE,
			},
			{
				// Data characteristic
				.uuid = &gatt_ims_char_data_uuid.u,
				.access_cb = &its_char_data_cb,
				.flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
			},
			{
				// Test ID Characteristic
				.uuid = &gatt_ims_char_tid_uuid.u,
				.access_cb = &ims_char_tid_cb,
				.flags = BLE_GATT_CHR_F_WRITE,
			},
			{
				// Style flags Characteristic
				.uuid = &gatt_ims_char_sty_uuid.u,
				.access_cb = &ims_char_sty_cb,
				.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE
			},
			{
				// Target date Characteristic
				.uuid = &gatt_ims_char_tgt_uuid.u,
				.access_cb = &ims_char_tgt_cb,
				.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE
			},
			{
				// Display dimensions Characteristic
				.uuid = &gatt_ims_char_dim_uuid.u,
				.access_cb = &ims_char_dim_cb,
				.flags = BLE_GATT_CHR_F_READ
			},
			{ 0 }
		},
	},
	{ 0 }
};

static const ble_uuid16_t adv_uuids16[] = {
	BLE_UUID16_INIT(BLE_SVC_DIS_UUID16),
	BLE_UUID16_INIT(BLE_SVC_BAS_UUID16),
	BLE_UUID16_INIT(BLE_SVC_CTS_UUID16),
};

static const ble_uuid128_t adv_uuids128[] = { gatt_ims_svc_uuid };

/* == GENERAL BLUETOOTH ===================================================== */

static void start_advertise(void);

static int gap_event(struct ble_gap_event * ev, void * arg)
{
	struct ble_gap_conn_desc desc;
	int ret;

	switch(ev->type) {
	case BLE_GAP_EVENT_CONNECT:
		ESP_LOGI(TAG, "Connection %s (status=%d)",
			 ev->connect.status == 0 ? "established" : "failed",
			 ev->connect.status);
		if (ev->connect.status != 0) { start_advertise(); }
		else {
			ble_gap_set_prefered_le_phy(ev->connect.conn_handle, BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK, 0);
			ble_gap_set_data_len(ev->connect.conn_handle, 0xFB, 0x400);
		}
		break;
	case BLE_GAP_EVENT_DISCONNECT:
		ESP_LOGI(TAG, "Disconnected (reason=%d)", ev->disconnect.reason);
		start_advertise();
		break;
	case BLE_GAP_EVENT_ENC_CHANGE:
		ESP_LOGI(TAG, "Encryption change event (status=%d)", ev->enc_change.status);
		break;
	case BLE_GAP_EVENT_NOTIFY_TX:
		ESP_LOGI(TAG, "Subscribe event: conn_handle=%d, attr_handle=%d, reason=%d, prevn=%d, curn=%d, previ=%d, curi=%d",
			 ev->subscribe.conn_handle, ev->subscribe.attr_handle, ev->subscribe.reason, ev->subscribe.prev_notify,
			 ev->subscribe.cur_notify, ev->subscribe.prev_indicate, ev->subscribe.cur_indicate);
		break;
	case BLE_GAP_EVENT_MTU:
		ESP_LOGI(TAG, "MTU update event: conn_handle=%d, cid=%d, mtu=%d", ev->mtu.conn_handle, ev->mtu.channel_id, ev->mtu.value);
		break;
	case BLE_GAP_EVENT_REPEAT_PAIRING:
		/* already have a bond, but peer is attempting to establish
		 * a new secure link -> throw away old bond an accept new one
		 */

		// delete old bond
		ret = ble_gap_conn_find(ev->repeat_pairing.conn_handle, &desc);
		assert(ret == 0);
		ble_store_util_delete_peer(&desc.peer_id_addr);
		// indicate thet host should continue with pairing operation
		return BLE_GAP_REPEAT_PAIRING_RETRY;
	case BLE_GAP_EVENT_PASSKEY_ACTION:
		ESP_LOGI(TAG, "Passkey event started");
		struct ble_sm_io pkey = { 0 };
		int key = 0;

		if (ev->passkey.params.action == BLE_SM_IOACT_DISP) {
			pkey.passkey = 123456; // @todo Replace this with randomly generated key
			ESP_LOGI(TAG, "Passkey: %d", pkey.passkey);
			ret = ble_sm_inject_io(ev->passkey.conn_handle, &pkey);
			ESP_LOGD(TAG, "ble_sm_inject_io result: %d", ret);
		}
		break;
	case BLE_GAP_EVENT_AUTHORIZE:
		ESP_LOGI(TAG, "Authorize event: conn_handle=%d, attr_handle=%d, is_read=%d",
			 ev->authorize.conn_handle, ev->authorize.attr_handle, ev->authorize.is_read);
		// default behaviour for this event is to reject
		ev->authorize.out_response = BLE_GAP_AUTHORIZE_REJECT;
		break;
	default:
		break;
	}
	return 0;
}

static void start_advertise(void)
{
	struct ble_gap_adv_params advp = { 0 };
	struct ble_hs_adv_fields adv_fields = { 0 };
	struct ble_hs_adv_fields rsp_fields = { 0 };
	const char * name = NULL;
	int ret;

	adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
	adv_fields.tx_pwr_lvl_is_present = 1;
	adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
	adv_fields.uuids128 = adv_uuids128;
	adv_fields.num_uuids128 = ARRAY_SIZE(adv_uuids128);
	adv_fields.uuids128_is_complete = true;

	ret = ble_gap_adv_set_fields(&adv_fields);
	if (ret != 0) { ESP_LOGE(TAG, "Failed to set adv data (error %d)", ret); }

	name = ble_svc_gap_device_name();
	rsp_fields.name = (uint8_t *)name;
	rsp_fields.name_len = strlen(name);
	rsp_fields.name_is_complete = 1;
	rsp_fields.uuids16 = adv_uuids16;
	rsp_fields.num_uuids16 = ARRAY_SIZE(adv_uuids16);
	rsp_fields.uuids16_is_complete = true;

	ret = ble_gap_adv_rsp_set_fields(&rsp_fields);
	if (ret != 0) { ESP_LOGE(TAG, "Failed to set rsp data (error %d)", ret); }

	advp.conn_mode = BLE_GAP_CONN_MODE_UND;
	advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
	ret = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &advp, gap_event, NULL);
	if (ret != 0) { ESP_LOGE(TAG, "Failed to start advertising (error %d)", ret); }
}

static void on_sync(void)
{
	int ret;

	ret = ble_hs_util_ensure_addr(0);
	if (ret != 0) {
		ESP_LOGE(TAG, "Failed to determine address type");
		return;
	}

	uint8_t addr_val[6] = { 0 };
	ret = ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr_val, NULL);
	ESP_LOGD(TAG, "Device Address: %02x%02x%02x%02x%02x%02x", addr_val[0], addr_val[1],
		 addr_val[2], addr_val[3], addr_val[4], addr_val[5]);

	start_advertise();
}

static void on_reset(int reason)
{
	ESP_LOGE(TAG, "Resetting state (reason=%d)", reason);
}

static void gatt_svr_register(struct ble_gatt_register_ctxt * ctxt, void * arg)
{
	char buf[BLE_UUID_STR_LEN];

	switch (ctxt->op) {
	case BLE_GATT_REGISTER_OP_SVC:
		ESP_LOGD(TAG, "Register service %s with handle %d",
			 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
		break;
	case BLE_GATT_REGISTER_OP_CHR:
		ESP_LOGD(TAG, "Register characteristic %s with def_handle %d and val_handle %d",
			 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
			 ctxt->chr.def_handle, ctxt->chr.val_handle);
		break;
	case BLE_GATT_REGISTER_OP_DSC:
		ESP_LOGI(TAG, "Register descriptor %s with dsc_handle %d",
			 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.dsc_def);
		break;
	default:
		assert(0);
		break;
	}
}

void nimble_host_task(void * p)
{
	ESP_LOGI(TAG, "BLE host task started");
	// this only returns when nimble_port_stop() is called
	nimble_port_run();
	nimble_port_freertos_deinit();
}

static int fetch_current_time(struct ble_svc_cts_curr_time * ct)
{
	time_t now;
	struct tm timeinfo;
	struct timeval tv_now;

	time(&now);
	localtime_r(&now, &timeinfo);
	gettimeofday(&tv_now, NULL);
	if (ct != NULL) {
		ct->et_256.d_d_t.d_t.year = timeinfo.tm_year + 1900;
		ct->et_256.d_d_t.d_t.month = timeinfo.tm_mon + 1;
		ct->et_256.d_d_t.d_t.day = timeinfo.tm_mday;
		ct->et_256.d_d_t.d_t.hours = timeinfo.tm_hour;
		ct->et_256.d_d_t.d_t.minutes = timeinfo.tm_min;
		ct->et_256.d_d_t.d_t.seconds = timeinfo.tm_sec;
		ct->et_256.d_d_t.day_of_week = timeinfo.tm_wday + 1;
		ct->et_256.fractions_256 = (((uint64_t)tv_now.tv_usec * 256L) / 1000000L);
		ct->adjust_reason = adjust_reason;
	}
	return 0;
}

static int set_current_time(struct ble_svc_cts_curr_time ct)
{
	time_t now;
	struct tm timeinfo;
	struct timeval tv_now;
	const message_t msg = { .event_id = EV_CUR_DT };

	timeinfo.tm_year = ct.et_256.d_d_t.d_t.year - 1900;
	timeinfo.tm_mon = ct.et_256.d_d_t.d_t.month - 1;
	timeinfo.tm_mday = ct.et_256.d_d_t.d_t.day;
	timeinfo.tm_hour = ct.et_256.d_d_t.d_t.hours;
	timeinfo.tm_min = ct.et_256.d_d_t.d_t.minutes;
	timeinfo.tm_sec = ct.et_256.d_d_t.d_t.seconds;
	timeinfo.tm_wday = ct.et_256.d_d_t.day_of_week - 1;
	now = mktime(&timeinfo);
	tv_now.tv_sec = now;
	settimeofday(&tv_now, NULL);
	gettimeofday(&last_updated, NULL);
	adjust_reason = ct.adjust_reason;
	xMessageBufferSend(main_msg_buf, &msg, sizeof(msg), 0);
	return 0;
}

static int fetch_local_time_info(struct ble_svc_cts_local_time_info * info)
{
	if (info != NULL) {
		memcpy(info, &cts_local_info, sizeof(cts_local_info));
	}
	return 0;
}

static int set_local_time_info(struct ble_svc_cts_local_time_info info)
{
	const message_t msg = { .event_id = EV_CUR_DT };
	cts_local_info.timezone = info.timezone;
	cts_local_info.dst_offset = info.dst_offset;
	gettimeofday(&last_updated, NULL);
	xMessageBufferSend(main_msg_buf, &msg, sizeof(msg), 0);
	return 0;
}

static int fetch_reference_time_info(struct ble_svc_cts_reference_time_info * info)
{
	struct timeval tv_now;
	uint64_t days_since_update;
	uint64_t hours_since_update;

	gettimeofday(&tv_now, NULL);
	tv_now.tv_sec -= last_updated.tv_sec;

	days_since_update = (tv_now.tv_sec / 86400L);
	info->time_source = TIME_SOURCE_MANUAL;
	info->time_accuracy = 0;
	info->days_since_update = days_since_update < 255 ? days_since_update : 255;
	if (days_since_update > 254) { info->hours_since_update = 255; }
	else {
		hours_since_update = (tv_now.tv_sec % 86400L) / 3600L;
		info->hours_since_update = hours_since_update;
	}
	adjust_reason = (CHANGE_OF_DST_MASK | CHANGE_OF_TIME_ZONE_MASK);
	return 0;
}

/* ========================================================================== */
/* == INTERFACE FUNCTIONS =================================================== */
/* ========================================================================== */

esp_err_t bt_init(void)
{
	ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "Failed to start NimBLE");

	ble_hs_cfg.sync_cb = &on_sync;
	ble_hs_cfg.reset_cb = &on_reset;
	ble_hs_cfg.gatts_register_cb = &gatt_svr_register;
	ble_hs_cfg.store_status_cb = &ble_store_util_status_rr;

	// init GAP & GATT
	ble_svc_gap_init();
	ble_svc_gap_device_name_set(BT_DEV_NAME);
	ble_svc_gatt_init();

	// init DIS
	ble_svc_dis_init();
	ble_svc_dis_manufacturer_name_set("LaGo");
	ble_svc_dis_model_number_set("E-Countdown");
	ble_svc_dis_hardware_revision_set("4.0.1");
	ble_svc_dis_firmware_revision_set("1.0.0");

	// init BAS
	ble_svc_bas_init();
	ble_svc_bas_battery_level_set(0);

	// init CTS
	struct ble_svc_cts_cfg cts_cfg = {
		.fetch_time_cb = fetch_current_time,
		.local_time_info_cb = fetch_local_time_info,
		.ref_time_info_cb = fetch_reference_time_info,
		.set_local_time_info_cb = set_local_time_info,
		.set_time_cb = set_current_time
	};
	ble_svc_cts_init(cts_cfg);

	// add ITS
	if (ble_gatts_count_cfg(gatt_svcs) != 0) { return ESP_ERR_INVALID_ARG; }
	if (ble_gatts_add_svcs(gatt_svcs) != 0) { return ESP_ERR_NO_MEM; }
	ble_att_set_preferred_mtu(BLE_ATT_MTU_MAX);

	nimble_port_freertos_init(nimble_host_task);

	return ESP_OK;
}