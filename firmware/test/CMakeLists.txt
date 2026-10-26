# Host unit tests for the portable firmware parts. Built from sim/CMakeLists.txt.
add_executable(firmware_tests main.cpp test_round_layout.cpp test_hello.cpp test_eye_model.cpp test_app.cpp
                              test_screenshots.cpp test_app_core.cpp test_sensor_math.cpp test_timer_alarm.cpp)
target_link_libraries(firmware_tests PRIVATE ui app_core simcore doctest::doctest)
target_compile_definitions(firmware_tests PRIVATE SCREENSHOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/screenshots")
add_test(NAME firmware_tests COMMAND firmware_tests)
