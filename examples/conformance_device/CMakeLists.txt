add_library(conformance_device_app STATIC
    conformance_device.c
    conformance_profiles.c
    conformance_storage.c)
target_include_directories(conformance_device_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(conformance_device_app PUBLIC iolinki)

add_executable(conformance_device main.c)
target_link_libraries(conformance_device PRIVATE conformance_device_app iolinki)
