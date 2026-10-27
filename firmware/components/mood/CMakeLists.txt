# MoodEngine: portable (standard C++ and app_core only). Built as an ESP-IDF component for the
# device and as a plain library for the simulator and host tests.
set(MOOD_SOURCES src/mood_engine.cpp)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${MOOD_SOURCES} INCLUDE_DIRS include REQUIRES app_core)
else()
    add_library(mood STATIC ${MOOD_SOURCES})
    target_include_directories(mood PUBLIC include)
    target_compile_features(mood PUBLIC cxx_std_20)
    target_link_libraries(mood PUBLIC app_core)
endif()
