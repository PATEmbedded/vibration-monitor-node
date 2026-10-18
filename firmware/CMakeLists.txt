cmake_minimum_required(VERSION 3.20)

# Firmware for the STM32F407 (STM32F4 Discovery), FreeRTOS on a Cortex-M4F.
#   cmake -S firmware -B build-fw
#   cmake --build build-fw

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake)
endif()

project(vmn_firmware VERSION 1.0.0 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(VMN_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# Pinned dependencies, downloaded at configure time.
include(FetchContent)
FetchContent_Declare(cmsis_core
    URL https://github.com/ARM-software/CMSIS_6/archive/refs/tags/v6.1.0.tar.gz
    URL_HASH SHA256=d8a044e4b50b7112476d6855a12c729ae2b70b3f77a2a038c23890e9a3515973
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake)
FetchContent_Declare(cmsis_device_f4
    URL https://github.com/STMicroelectronics/cmsis-device-f4/archive/refs/tags/v2.6.10.tar.gz
    URL_HASH SHA256=b0566d2e07f6f38d69f3ba78ffa8bbac6f393c5b0fa31dd9e84f2ab42994fe7b
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake)
FetchContent_Declare(freertos_kernel
    URL https://github.com/FreeRTOS/FreeRTOS-Kernel/archive/refs/tags/V11.1.0.tar.gz
    URL_HASH SHA256=0e21928b3bcc4f9bcaf7333fb1c8c0299d97e2ec9e13e3faa2c5a7ac8a3bc573
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# The kernel's CMake build reads its configuration from this target.
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE src)
set(FREERTOS_PORT GCC_ARM_CM4F CACHE STRING "")
set(FREERTOS_HEAP 4 CACHE STRING "")
FetchContent_MakeAvailable(cmsis_core cmsis_device_f4 freertos_kernel)

set(CMSIS_INCLUDES
    ${cmsis_core_SOURCE_DIR}/CMSIS/Core/Include
    ${cmsis_device_f4_SOURCE_DIR}/Include)

# Vendor start-up code from ST (Apache-2.0), built without our warning flags.
add_library(stm32_startup OBJECT
    ${cmsis_device_f4_SOURCE_DIR}/Source/Templates/system_stm32f4xx.c
    ${cmsis_device_f4_SOURCE_DIR}/Source/Templates/gcc/startup_stm32f407xx.s)
target_include_directories(stm32_startup SYSTEM PRIVATE ${CMSIS_INCLUDES})
target_compile_definitions(stm32_startup PRIVATE STM32F407xx)

add_executable(vmn-node
    src/main.c
    src/board.c
    src/syscalls.c
    ${VMN_ROOT}/app/src/node.c
    ${VMN_ROOT}/core/src/classifier.c
    ${VMN_ROOT}/core/src/crc16.c
    ${VMN_ROOT}/core/src/features.c
    ${VMN_ROOT}/core/src/fft.c
    ${VMN_ROOT}/core/src/modbus_rtu.c
    ${VMN_ROOT}/core/src/signal_sim.c
    $<TARGET_OBJECTS:stm32_startup>)
set_target_properties(vmn-node PROPERTIES
    SUFFIX ".elf"
    LINK_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/stm32f407vg.ld)
target_include_directories(vmn-node PRIVATE src ${VMN_ROOT}/core/include ${VMN_ROOT}/app/include ${VMN_ROOT}/model)
target_include_directories(vmn-node SYSTEM PRIVATE ${CMSIS_INCLUDES})
target_compile_definitions(vmn-node PRIVATE STM32F407xx)
target_compile_options(vmn-node PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wdouble-promotion
                                        -Wstrict-prototypes -Wmissing-prototypes -Werror)
target_link_libraries(vmn-node PRIVATE freertos_kernel m)
target_link_options(vmn-node PRIVATE
    -T${CMAKE_CURRENT_LIST_DIR}/stm32f407vg.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/vmn-node.map
    -Wl,--print-memory-usage)

add_custom_command(TARGET vmn-node POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:vmn-node> ${CMAKE_CURRENT_BINARY_DIR}/vmn-node.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:vmn-node>
    VERBATIM)
