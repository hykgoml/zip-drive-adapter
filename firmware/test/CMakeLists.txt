cmake_minimum_required(VERSION 3.13)
project(zipbridge_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../src)

# Firmware sources under test and test programs. Each task adds to these lists.
set(FW_SRC board.c bus.c leds.c console.c)
set(TESTS fake bus leds console)

add_compile_options(-Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer)
add_link_options(-fsanitize=address,undefined)

list(TRANSFORM FW_SRC PREPEND ${SRC_DIR}/)
add_library(fw_core STATIC ${FW_SRC} fake_board.c fake_io.c)
target_include_directories(fw_core PUBLIC ${SRC_DIR} ${CMAKE_CURRENT_LIST_DIR})

foreach(t ${TESTS})
    add_executable(test_${t} test_${t}.c)
    target_link_libraries(test_${t} fw_core)
    add_test(NAME ${t} COMMAND test_${t})
endforeach()
