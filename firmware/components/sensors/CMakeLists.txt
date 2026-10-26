idf_component_register(
    SRCS "sensors.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_core esp_driver_i2c
)
