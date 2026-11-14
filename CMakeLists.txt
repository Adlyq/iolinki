cmake_minimum_required(VERSION 3.10)
# Version 3.1.0
project(iolinki VERSION 3.1.0 LANGUAGES C)

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

# Feature trimming: a trimmed image is the structural configuration
# (Data Storage and events off) expressed directly with the individual options
# below. All default to the full stack.
option(IOLINK_FEATURE_DATA_STORAGE "Compile Data Storage" ON)
option(IOLINK_FEATURE_SYSTEM_COMMAND "Enable SystemCommand (runtime bit; the handler is always resident)" ON)
option(IOLINK_FEATURE_EVENTS "Compile the diagnostic event engine" ON)
option(IOLINK_FEATURE_VALIDATE "Run the init-time configuration self-checks" ON)
# Absolute paths so a subdirectory can build the same sources under a different
# ISR granularity (see tests/CMakeLists.txt: TX FRAME fixture).
set(IOLINK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/phy_generic.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/phy_tiol112.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/phy_l6362a.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/phy_virtual.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/crc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/capability.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/profile.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll_isr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/od.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/isdu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/events.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/diag.c
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
    IOLINK_FEATURE_VALIDATE)
    if(NOT ${_iolink_feature})
        target_compile_definitions(iolinki PUBLIC ${_iolink_feature}=0)
    endif()
endforeach()

# Platform Selection
set(IOLINK_PLATFORM "LINUX" CACHE STRING "Target platform: LINUX, BAREMETAL")

if(IOLINK_PLATFORM STREQUAL "LINUX")
    message(STATUS "Building for Linux host")
elseif(IOLINK_PLATFORM STREQUAL "BAREMETAL")
    message(STATUS "Building for Bare Metal (Stub)")
endif()

# Examples
add_subdirectory(examples/simple_device)
if(IOLINK_PLATFORM STREQUAL "LINUX")
    add_subdirectory(examples/host_demo)
elseif(IOLINK_PLATFORM STREQUAL "BAREMETAL")
    add_subdirectory(examples/bare_metal_app)
endif()

# Testing
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

if(IOLINK_PLATFORM STREQUAL "LINUX")
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
    if(IOLINK_PLATFORM STREQUAL "LINUX")
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
