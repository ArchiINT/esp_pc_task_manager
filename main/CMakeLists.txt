idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES pcmon_display pcmon_link pcmon_ui
    PRIV_REQUIRES nvs_flash esp_timer
)
