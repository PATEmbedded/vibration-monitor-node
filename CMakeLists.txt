cmake_minimum_required(VERSION 3.20)
project(vibration_monitor_node VERSION 1.0.0 LANGUAGES C)

# Host build: the portable core, its unit tests, the command-line tool and the fuzz target.
# The firmware has its own project in firmware/ with the ARM toolchain.

option(VMN_BUILD_TESTS "Build the unit tests" ON)
option(VMN_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(VMN_FUZZ "Build the libFuzzer target (Clang only)" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(VMN_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wdouble-promotion -Wcast-qual
                 -Wstrict-prototypes -Wmissing-prototypes -Werror)

if(VMN_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()
if(VMN_FUZZ)
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(vmn_core STATIC
    core/src/classifier.c
    core/src/crc16.c
    core/src/features.c
    core/src/fft.c
    core/src/modbus_rtu.c
    core/src/signal_sim.c
)
target_include_directories(vmn_core PUBLIC core/include PRIVATE model)
target_compile_options(vmn_core PRIVATE ${VMN_WARNINGS})
if(UNIX)
    target_link_libraries(vmn_core PUBLIC m)
endif()

add_library(vmn_app STATIC app/src/node.c)
target_include_directories(vmn_app PUBLIC app/include)
target_link_libraries(vmn_app PUBLIC vmn_core)
target_compile_options(vmn_app PRIVATE ${VMN_WARNINGS})

add_executable(vmn_cli tools/cli/vmn_cli.c)
target_link_libraries(vmn_cli PRIVATE vmn_core)
target_compile_options(vmn_cli PRIVATE ${VMN_WARNINGS})

if(VMN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test/host)
endif()

if(VMN_FUZZ)
    add_executable(fuzz_modbus fuzz/fuzz_modbus.c)
    target_link_libraries(fuzz_modbus PRIVATE vmn_core)
    target_link_options(fuzz_modbus PRIVATE -fsanitize=fuzzer)
endif()
