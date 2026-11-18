cmake_minimum_required(VERSION 3.12)
# Version 4.0.0
project(iolinki VERSION 5.0.0 LANGUAGES C)

# No default CMAKE_BUILD_TYPE: the toolchain default applies (GCC = -O0), so
# static helpers stay out of line. Do not judge ISR code size or timing from a
# plain build; `tools/gates.sh build` builds Debug and Release, and the embedded
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

# Statistics counter model: IOLINK_STATS_SPLIT gives the data ISR its own
# counter set (the public stats then sum both); empty keeps the config.h
# default, IOLINK_STATS_SHARED. The macro must be identical for the library and
# the tests, so it is set once for the whole build instead of via CMAKE_C_FLAGS.
# Layout-affecting macros change struct sizes, so the exported target must carry
# them: a consumer built with a different value than the library would allocate
# the wrong iolink_device_ctx_t. Empty keeps the config.h default.
set(IOLINK_STATS_MODE "" CACHE STRING "IOLINK_STATS_SHARED or IOLINK_STATS_SPLIT; empty = config.h default")
set(IOLINK_ISR_RX_MODE "" CACHE STRING "IOLINK_ISR_RX_BYTE or IOLINK_ISR_RX_FRAME; empty = config.h default")
set(IOLINK_ISR_TX_MODE "" CACHE STRING "IOLINK_ISR_TX_BYTE or IOLINK_ISR_TX_FRAME; empty = config.h default")
set(IOLINK_PHY_FEATURE_TX_BUSY "" CACHE STRING "0/1; empty = config.h default")
set_property(CACHE IOLINK_STATS_MODE PROPERTY STRINGS "" IOLINK_STATS_SHARED IOLINK_STATS_SPLIT)
if(IOLINK_STATS_MODE STREQUAL "")
    message(STATUS "Statistics mode: config.h default (IOLINK_STATS_SHARED)")
elseif(IOLINK_STATS_MODE STREQUAL "IOLINK_STATS_SHARED" OR
       IOLINK_STATS_MODE STREQUAL "IOLINK_STATS_SPLIT")
    message(STATUS "Statistics mode: ${IOLINK_STATS_MODE} (IOLINK_STATS_MODE cache variable)")
else()
    message(FATAL_ERROR
            "IOLINK_STATS_MODE must be IOLINK_STATS_SHARED, IOLINK_STATS_SPLIT or empty, got '${IOLINK_STATS_MODE}'")
endif()

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll_frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll_sm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll_reply.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll_api.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll_isr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll_isr_rx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/od.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/isdu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/isdu_cmd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/isdu_ds.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/events.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/diag.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/data_storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/device_info.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/device.c
)
add_library(iolinki STATIC ${IOLINK_SOURCES})
target_include_directories(iolinki PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
# Same spelling for add_subdirectory() and find_package() consumers.
add_library(iolinki::iolinki ALIAS iolinki)

# gcc-only diagnostics that clang does not provide (docs/TESTING.md, group
# `gcc`). They are applied to the library target, not build-wide: the stack
# bounds below are meaningful for src/ while the host examples allocate
# multi-kilobyte contexts on the stack by design.
option(IOLINK_STRICT_GCC "gcc-only strict diagnostics on the iolinki target" OFF)
if(IOLINK_STRICT_GCC AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(iolinki PRIVATE
        -Wsign-conversion
        -Wshadow
        -Wdouble-promotion
        -Wformat=2
        -Wformat-signedness
        -Wnull-dereference
        -Wimplicit-fallthrough=5
        -Wduplicated-branches
        -Wduplicated-cond
        -Wlogical-op
        -Wlogical-not-parentheses
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wmissing-declarations
        -Wold-style-definition
        -Wredundant-decls
        -Wundef
        -Wcast-qual
        -Wbad-function-cast
        -Wwrite-strings
        -Wpointer-arith
        -Wvla
        -Wswitch-default
        -Wswitch-enum
        -Wcast-align
        -Wshift-overflow=2
        -Warray-bounds=2
        -Wstringop-overflow=4
        -Wstack-usage=512
        -Wframe-larger-than=1024)
endif()

# Layout macros are part of the ABI contract, not just behaviour: the exported
# target has to hand them to the consumer. The directory-wide copy keeps the
# test fixtures (separate targets over the same sources) in the same layout.
foreach(_iolink_layout_macro
        IOLINK_STATS_MODE
        IOLINK_ISR_RX_MODE
        IOLINK_ISR_TX_MODE
        IOLINK_PHY_FEATURE_TX_BUSY)
    if(NOT "${${_iolink_layout_macro}}" STREQUAL "")
        add_compile_definitions(${_iolink_layout_macro}=${${_iolink_layout_macro}})
        target_compile_definitions(iolinki PUBLIC ${_iolink_layout_macro}=${${_iolink_layout_macro}})
    endif()
endforeach()

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

# Install/export: consumers use find_package(iolinki CONFIG REQUIRED) and link
# iolinki::iolinki. The public tree includes the internal/ headers because
# device.h embeds the DLL layout (the application allocates the context); they
# are installed so the public headers compile, not as an API.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS iolinki EXPORT iolinkiTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT iolinkiTargets
        NAMESPACE iolinki::
        FILE iolinkiTargets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iolinki)
configure_package_config_file(cmake/iolinkiConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iolinkiConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iolinki)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/iolinkiConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/iolinkiConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/iolinkiConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/iolinki)

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
# ISR transport the suite (and the PTY host examples, which talk through the
# byte-stream iolink_phy_virtual) is exercised on: BYTE or FRAME.
set(IOLINK_TEST_TRANSPORT "BYTE" CACHE STRING "ISR transport the tests exercise: BYTE or FRAME")
set_property(CACHE IOLINK_TEST_TRANSPORT PROPERTY STRINGS BYTE FRAME)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Fuzz harnesses: separate tree, clang + libFuzzer only (tools/fuzz.sh, group
# `fuzz`). The library is rebuilt there with sanitizer coverage, so the option
# must not be combined with a tree the other gates reuse.
option(IOLINK_BUILD_FUZZERS "Build the libFuzzer harnesses (clang only)" OFF)
if(IOLINK_BUILD_FUZZERS)
    add_subdirectory(fuzz)
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
    if(IOLINK_PLATFORM STREQUAL "LINUX" AND IOLINK_TEST_TRANSPORT STREQUAL "BYTE"
       AND IOLINK_FEATURE_DATA_STORAGE AND IOLINK_FEATURE_EVENTS)
        # These drive the host example through its PTY, i.e. the byte-stream
        # iolink_phy_virtual: its get() returns NULL in FRAME builds.
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
