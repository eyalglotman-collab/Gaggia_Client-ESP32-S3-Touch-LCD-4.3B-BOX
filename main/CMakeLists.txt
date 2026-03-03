idf_component_register(SRCS "Eyal_espresso_ESP32_main.c" "lvgl_port.c" "ui_screen.c" "hardware_init.c"
                       PRIV_REQUIRES spi_flash driver esp_lcd esp_timer esp_lcd_touch esp_lcd_touch_gt911
                       REQUIRES lvgl
                       INCLUDE_DIRS "")
