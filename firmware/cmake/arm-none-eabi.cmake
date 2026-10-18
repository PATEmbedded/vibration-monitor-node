# Toolchain file for the Arm GNU toolchain (arm-none-eabi-gcc).
# Set ARM_TOOLCHAIN_DIR to its bin directory if it is not on PATH.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(_prefix "arm-none-eabi-")
if(DEFINED ENV{ARM_TOOLCHAIN_DIR})
    file(TO_CMAKE_PATH "$ENV{ARM_TOOLCHAIN_DIR}" _dir)
    set(_prefix "${_dir}/arm-none-eabi-")
endif()
set(_suffix "")
if(CMAKE_HOST_WIN32)
    set(_suffix ".exe")
endif()

set(CMAKE_C_COMPILER "${_prefix}gcc${_suffix}")
set(CMAKE_ASM_COMPILER "${_prefix}gcc${_suffix}")
set(CMAKE_OBJCOPY "${_prefix}objcopy${_suffix}" CACHE FILEPATH "")
set(CMAKE_SIZE "${_prefix}size${_suffix}" CACHE FILEPATH "")

set(VMN_CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${VMN_CPU_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "${VMN_CPU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${VMN_CPU_FLAGS} -Wl,--gc-sections -Wl,--no-warn-rwx-segments --specs=nano.specs")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
