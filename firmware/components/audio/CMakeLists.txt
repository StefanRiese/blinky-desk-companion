idf_component_register(
    SRCS "audio.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_core board_ws146 esp_driver_i2s esp_timer log freertos
)
