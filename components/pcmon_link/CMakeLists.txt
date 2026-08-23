# Both transport implementations are always compiled; each one guards its body
# with the matching CONFIG_PCMON_LINK_* symbol.  Keeping the file list and the
# requirements static avoids surprises during CMake's early expansion pass,
# where CONFIG_* values are not yet available.
idf_component_register(
    SRCS
        "pcmon_link.c"
        "link_usb.c"
        "link_ble.c"
    INCLUDE_DIRS "include"
    REQUIRES pcmon_proto
    PRIV_REQUIRES esp_driver_usb_serial_jtag esp_timer bt nvs_flash
)
