cmake_minimum_required(VERSION 3.13)

# Use ~/pico/pico-sdk when PICO_SDK_PATH isn't set.
if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "$ENV{HOME}/pico/pico-sdk")
endif()
set(PICO_BOARD pico2 CACHE STRING "Board type")

include(pico_sdk_import.cmake)
project(zipbridge_bringup C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(APP_SRC
    src/main.c src/board.c src/gpio_hal_rp2350.c src/io_usb.c
    src/bus.c src/leds.c src/console.c src/exercise.c src/scan.c)

add_executable(zipbridge_bringup ${APP_SRC})
# Strict warnings for our files only (SDK sources are compiled into the target too).
set_source_files_properties(${APP_SRC} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
target_link_libraries(zipbridge_bringup pico_stdlib hardware_sync)
pico_enable_stdio_usb(zipbridge_bringup 1)
pico_enable_stdio_uart(zipbridge_bringup 0)
pico_add_extra_outputs(zipbridge_bringup)
