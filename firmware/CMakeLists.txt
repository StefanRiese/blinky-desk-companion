# Blinky firmware (ESP-IDF 6.1, ESP32-S3).
#   idf.py set-target esp32s3 && idf.py build flash monitor
# The portable parts (components/ui, …) also build without ESP-IDF: see sim/.
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(deskbuddy)
