# Host unit tests for the portable firmware parts. Built from sim/CMakeLists.txt.
add_executable(firmware_tests main.cpp test_round_layout.cpp test_info.cpp test_eye_model.cpp test_app.cpp
                              test_screenshots.cpp test_app_core.cpp test_sensor_math.cpp test_timer_alarm.cpp
                              test_host_link.cpp test_sources.cpp test_mood.cpp test_ota.cpp)
target_link_libraries(firmware_tests PRIVATE ui mood app_core simcore doctest::doctest)
target_compile_definitions(firmware_tests PRIVATE SCREENSHOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/screenshots"
                                                  PROTOCOL_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/examples")
add_test(NAME firmware_tests COMMAND firmware_tests)
