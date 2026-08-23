idf_component_register(
    SRCS
        "pcmon_ui.c"
        "ui_theme.c"
        "ui_widgets.c"
        "page_overview.c"
        "page_cpu.c"
        "page_gpu.c"
        "page_history.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES pcmon_link pcmon_display lvgl
    PRIV_REQUIRES esp_driver_gpio
)
