idf_component_register(
    SRCS "pcmon_display.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_ledc esp_timer
)
