# Portable UI: standard C++ and LVGL only. Built as an ESP-IDF component for the device,
# and as a plain CMake library for the desktop simulator and host tests (sim/).
set(UI_SOURCES src/app.cpp src/eye_model.cpp src/eyes.cpp src/hello.cpp src/round_layout.cpp)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${UI_SOURCES} INCLUDE_DIRS include)
else()
    add_library(ui STATIC ${UI_SOURCES})
    target_include_directories(ui PUBLIC include)
    target_compile_features(ui PUBLIC cxx_std_20)
    target_link_libraries(ui PUBLIC lvgl)
endif()
