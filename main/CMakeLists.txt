idf_component_register(
	SRCS
		"battery.c"
		"display.c"
		"draw.c"
		"bt.c"
		"main.c"
	INCLUDE_DIRS
		"."
	REQUIRES
		epd4in26
		bt
		nvs_flash
		esp_mmap_assets
		esp_lv_fs
		esp_lvgl_adapter
		lvgl
)

spiffs_create_partition_assets(
	"assets_font"
	"${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
	FLASH_IN_PROJECT
	MMAP_FILE_SUPPORT_FORMAT ".ttf"
)