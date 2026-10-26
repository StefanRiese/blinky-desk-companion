idf_component_register(
    SRCS "serial_link.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_core esp_driver_usb_serial_jtag log freertos
)
