add_library(reference_device_app STATIC reference_device.c)
target_include_directories(reference_device_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reference_device_app PUBLIC iolinki)

# The host demo drives the streaming transport; exclude it from ISR builds.
if(NOT IOLINK_ISR_PUMP)
    add_executable(reference_device_demo host_main.c)
    target_link_libraries(reference_device_demo reference_device_app)
    if(BUILD_TESTING)
        add_test(NAME reference_device_demo COMMAND reference_device_demo)
    endif()
endif()
