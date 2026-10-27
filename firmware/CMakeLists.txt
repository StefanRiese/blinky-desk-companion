# Blinky firmware (ESP-IDF 6.1, ESP32-S3), for one board per build folder:
#   idf.py set-target esp32s3 && idf.py build flash monitor                  ws146 (default)
#   idf.py -B build-amoled175 -D BOARD=amoled175 set-target esp32s3 build    AMOLED-1.75
# The portable parts (components/ui, …) also build without ESP-IDF: see sim/.
cmake_minimum_required(VERSION 3.22)

set(BOARD "ws146" CACHE STRING "Board: ws146 or amoled175")
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}.defaults")
    message(FATAL_ERROR "unknown BOARD '${BOARD}' (boards/: ws146, amoled175)")
endif()
# The board's defaults on top of the common ones; the sdkconfig lives in the build folder,
# so builds for different boards don't mix.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;boards/${BOARD}.defaults")
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deskbuddy)
