idf_component_register(
    SRCS "audio.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_core board esp_timer log freertos
)
