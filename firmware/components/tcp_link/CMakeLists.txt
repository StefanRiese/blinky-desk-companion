idf_component_register(
    SRCS "tcp_link.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_core net lwip log freertos esp_timer
)
