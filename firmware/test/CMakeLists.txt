# Host unit tests for the portable firmware parts. Built from sim/CMakeLists.txt.
add_executable(firmware_tests main.cpp test_round_layout.cpp test_hello.cpp)
target_link_libraries(firmware_tests PRIVATE ui simcore doctest::doctest)
add_test(NAME firmware_tests COMMAND firmware_tests)
