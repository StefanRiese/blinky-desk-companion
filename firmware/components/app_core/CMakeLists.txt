# Portable core: DataStore, settings registry, units, protocol and HostLink. Standard C++ only; built as an ESP-IDF
# component for the device and as a plain library for the simulator and host tests.
set(CORE_SOURCES src/alarm_clock.cpp src/countdown.cpp src/data_store.cpp src/host_link.cpp src/protocol.cpp src/sensor_math.cpp src/settings.cpp src/time_zone.cpp src/units.cpp)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${CORE_SOURCES} src/settings_nvs.cpp INCLUDE_DIRS include REQUIRES nvs_flash)
else()
    add_library(app_core STATIC ${CORE_SOURCES})
    target_include_directories(app_core PUBLIC include)
    target_compile_features(app_core PUBLIC cxx_std_20)
    target_link_libraries(app_core PUBLIC ArduinoJson)
endif()
