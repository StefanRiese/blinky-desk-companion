idf_component_register(
    SRCS "app_main.cpp"
    REQUIRES board ui app_core sensors serial_link net tcp_link audio esp_app_format esp_hw_support esp_timer app_update
)
