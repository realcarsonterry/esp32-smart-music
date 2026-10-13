cmake_minimum_required(VERSION 3.20)
project(stage7_host_checks LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
enable_testing()
# Host tests exercise the actual firmware audio/wire files with an explicit fake
# I2S boundary. This is neither an ESP-IDF build nor a hardware test.
add_executable(firmware_host_checks host_checks.c ../main/wire.c ../main/audio.c
    ../../core/music_core.c ../../simulator/music_json.c
    ../../simulator/vendor/cjson/cJSON.c)
target_include_directories(firmware_host_checks PRIVATE stubs ../main ../../core
    ../../simulator ../../simulator/vendor/cjson)
if(MSVC)
    target_compile_options(firmware_host_checks PRIVATE /W4 /fp:strict)
else()
    target_compile_options(firmware_host_checks PRIVATE -Wall -Wextra -Werror -fno-fast-math)
    target_link_libraries(firmware_host_checks PRIVATE m)
endif()
add_test(NAME firmware_host_checks COMMAND firmware_host_checks
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/fixtures/valid-one-bar.json")
add_executable(firmware_transport_checks transport_checks.c ../main/transport.c ../main/wire.c)
target_include_directories(firmware_transport_checks PRIVATE stubs ../main ../../core)
if(MSVC)
    target_compile_options(firmware_transport_checks PRIVATE /W4)
else()
    target_compile_options(firmware_transport_checks PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME firmware_transport_checks COMMAND firmware_transport_checks)
