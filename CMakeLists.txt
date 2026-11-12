cmake_minimum_required(VERSION 3.10)
# Version 2.2.0
project(iolinki VERSION 2.2.0 LANGUAGES C)

# No default CMAKE_BUILD_TYPE: the toolchain default applies (GCC = -O0), so
# static helpers stay out of line. Do not judge ISR code size or timing from a
# plain build; check_quality.sh builds Debug and Release, and the embedded
# examples use -Os.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Code Coverage Support
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    endif()
endif()

# Include directories
include_directories(include)

# Feature trimming. The profiles are bundles: when selected they override the
# individual feature options below. Use the individual options for fine-grained
# control without a profile. All default to the full stack.
option(IOLINK_MINIMAL_PROFILE "Compile the trimmed feature profile (keeps events)" OFF)
option(IOLINK_TINY_PROFILE "Compile the tiny feature profile (MINIMAL + no events)" OFF)
option(IOLINK_FEATURE_DATA_STORAGE "Compile Data Storage" ON)
option(IOLINK_FEATURE_SYSTEM_COMMAND "Compile SystemCommand handling" ON)
option(IOLINK_FEATURE_EVENTS "Compile the diagnostic event engine" ON)
option(IOLINK_PHY_FEATURE_WAKEUP "Compile PHY wake-up detection" ON)
option(IOLINK_PHY_FEATURE_SIO_CQ "Compile PHY manual C/Q control" ON)
option(IOLINK_APP_PD_INPUT_CALLBACK "Use the on_pd_input callback instead of the polling PD API" OFF)

if(IOLINK_MINIMAL_PROFILE OR IOLINK_TINY_PROFILE)
    set(IOLINK_FEATURE_DATA_STORAGE OFF)
    set(IOLINK_FEATURE_SYSTEM_COMMAND OFF)
    set(IOLINK_PHY_FEATURE_WAKEUP OFF)
    set(IOLINK_PHY_FEATURE_SIO_CQ OFF)
endif()
if(IOLINK_TINY_PROFILE)
    set(IOLINK_FEATURE_EVENTS OFF)
endif()

# Absolute paths so a subdirectory can build the same sources under a different
# ISR granularity (see tests/CMakeLists.txt: TX FRAME fixture).
set(IOLINK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/phy_generic.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/phy_tiol112.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/phy_l6362a.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/phy_virtual.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/crc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll_isr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/isdu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/events.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/params.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/data_storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/device_info.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/device.c
)
add_library(iolinki STATIC ${IOLINK_SOURCES})

# Only trimmed features are defined; the rest keep the config.h defaults.
foreach(_iolink_feature
    IOLINK_FEATURE_DATA_STORAGE
    IOLINK_FEATURE_SYSTEM_COMMAND
    IOLINK_FEATURE_EVENTS
    IOLINK_PHY_FEATURE_WAKEUP
    IOLINK_PHY_FEATURE_SIO_CQ)
    if(NOT ${_iolink_feature})
        target_compile_definitions(iolinki PUBLIC ${_iolink_feature}=0)
    endif()
endforeach()

if(IOLINK_APP_PD_INPUT_CALLBACK)
    # PUBLIC so an application that consumes iolinki via add_subdirectory()
    # compiles the same profile: iolink_app_callbacks_t's layout depends on it.
    target_compile_definitions(iolinki PUBLIC IOLINK_APP_PD_INPUT_CALLBACK=1)
endif()

# Platform Selection
set(IOLINK_PLATFORM "LINUX" CACHE STRING "Target platform: LINUX, BAREMETAL")

if(IOLINK_PLATFORM STREQUAL "LINUX")
    message(STATUS "Building for Linux host")
elseif(IOLINK_PLATFORM STREQUAL "BAREMETAL")
    message(STATUS "Building for Bare Metal (Stub)")
endif()

# Examples
if(NOT IOLINK_APP_PD_INPUT_CALLBACK)
    add_subdirectory(examples/simple_device)
    if(IOLINK_PLATFORM STREQUAL "LINUX")
        add_subdirectory(examples/host_demo)
    elseif(IOLINK_PLATFORM STREQUAL "BAREMETAL")
        add_subdirectory(examples/bare_metal_app)
    endif()
endif()

# Testing
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

if(IOLINK_PLATFORM STREQUAL "LINUX" AND NOT IOLINK_APP_PD_INPUT_CALLBACK)
    add_subdirectory(examples/reference_device)
    add_subdirectory(examples/switching_sensor)
    add_subdirectory(examples/conformance_device)
endif()

# Documentation
option(IOLINK_ENABLE_DOCS "Enable Doxygen documentation target" OFF)
if(IOLINK_ENABLE_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        add_custom_target(doc
            COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
    else()
        message(WARNING "Doxygen not found; documentation target disabled.")
    endif()
endif()

# IODD Generation
find_package(Python3)
if(Python3_FOUND)
    add_custom_target(iodd
        COMMAND ${Python3_EXECUTABLE} tools/iodd_gen.py examples/simple_device/simple_device.json
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating IODD XML for simple_device"
        VERBATIM
    )
endif()

if(BUILD_TESTING AND Python3_FOUND)
    add_test(NAME release_test_stats COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/test_release_test_stats.py)
    add_test(NAME iar_project_paths COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/test_check_iar_project.py)
    add_test(NAME iodd_cli COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/test_iodd_cli.py)
    add_test(NAME conformance_traceability
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_conformance_traceability.py)
    if(IOLINK_PLATFORM STREQUAL "LINUX" AND NOT IOLINK_APP_PD_INPUT_CALLBACK)
        add_test(NAME conformance_device_smoke
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/test_conformance_device_smoke.py
                         $<TARGET_FILE:conformance_device>)
        add_test(NAME conformance_device_master
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/conformance_device_master.py
                         $<TARGET_FILE:conformance_device>)
        add_test(NAME conformance_device_iodd_oracle
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/test_conformance_iodd_oracle.py
                         $<TARGET_FILE:conformance_device>)
    endif()
endif()
