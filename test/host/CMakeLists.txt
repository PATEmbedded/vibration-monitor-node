include(FetchContent)
FetchContent_Declare(unity
    URL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.6.1.tar.gz
    URL_HASH SHA256=b41a66d45a6b99758fb3202ace6178177014d52fc524bf1f72687d93e9867292
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(unity)

foreach(name crc16 modbus fft features classifier signal_sim node)
    add_executable(test_${name} test_${name}.c)
    target_link_libraries(test_${name} PRIVATE vmn_app unity::framework)
    target_compile_options(test_${name} PRIVATE ${VMN_WARNINGS} -Wno-missing-prototypes)
    add_test(NAME ${name} COMMAND test_${name})
endforeach()
