idf_component_register(SRCS "Eyal_espresso_ESP32_main.c" "lvgl_port.c" "ui_screen.c" "hardware_init.c" "peripherals_manager.c" "system_constants.c" "CommunicationFunctions.c"
                       PRIV_REQUIRES spi_flash driver esp_driver_uart esp_lcd esp_timer esp_lcd_touch esp_lcd_touch_gt911 sdmmc fatfs esp_event esp_netif esp_wifi nvs_flash lwip
                       REQUIRES lvgl
                       INCLUDE_DIRS ""
                       EMBED_TXTFILES "SystemConstants.xml")
