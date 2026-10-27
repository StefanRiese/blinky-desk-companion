# One component for all boards: the board's source file is chosen by the Kconfig option
# (see Kconfig). The requirements are the union of all boards', since ESP-IDF resolves them
# before the configuration is known.
if(CONFIG_BLINKY_BOARD_AMOLED175)
    set(srcs "board_amoled175.c")
else()
    set(srcs "board_ws146.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_ledc esp_driver_spi esp_lcd
)
