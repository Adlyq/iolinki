# The full suite exercises the untrimmed stack. Skip it for a trimmed build;
# build the library target (or configure with -DBUILD_TESTING=OFF) instead.
if(IOLINK_MINIMAL_PROFILE OR IOLINK_TINY_PROFILE OR
   NOT IOLINK_FEATURE_DATA_STORAGE OR NOT IOLINK_FEATURE_SYSTEM_COMMAND OR
   NOT IOLINK_FEATURE_LEGACY_PARAMS OR NOT IOLINK_FEATURE_EVENTS OR
   NOT IOLINK_PHY_FEATURE_WAKEUP OR NOT IOLINK_PHY_FEATURE_SIO_CQ OR
   NOT IOLINK_PHY_FEATURE_LEGACY_DIAG)
    message(STATUS "Feature-trimmed build: skipping the unit suite")
    return()
endif()

# Several tests are plain assert()-based. CMake adds -DNDEBUG to Release builds,
# which would compile every assert() away and turn those tests into no-ops (and
# trip -Wunused-but-set-variable). Keep assertions active for the suite.
add_compile_options(-UNDEBUG)

find_package(PkgConfig QUIET)
add_executable(test_switching_sensor test_switching_sensor.c ../examples/switching_sensor/switching_sensor.c)
target_include_directories(test_switching_sensor PRIVATE ../examples/switching_sensor)
add_test(NAME test_switching_sensor COMMAND test_switching_sensor)
add_executable(test_vendor_isdu test_vendor_isdu.c)
target_link_libraries(test_vendor_isdu iolinki)
add_test(NAME test_vendor_isdu COMMAND test_vendor_isdu)
add_executable(test_params_persistence test_params_persistence.c)
target_link_libraries(test_params_persistence iolinki)
add_test(NAME test_params_persistence COMMAND test_params_persistence)
add_executable(test_deadline_rollover test_deadline_rollover.c)
target_link_libraries(test_deadline_rollover iolinki)
add_test(NAME test_deadline_rollover COMMAND test_deadline_rollover)
add_executable(test_device_time_source test_device_time_source.c)
target_link_libraries(test_device_time_source iolinki)
add_test(NAME test_device_time_source COMMAND test_device_time_source)
add_executable(test_device_requires_critical test_device_requires_critical.c)
target_link_libraries(test_device_requires_critical iolinki)
add_test(NAME test_device_requires_critical COMMAND test_device_requires_critical)
add_executable(test_device_requires_storage test_device_requires_storage.c)
target_link_libraries(test_device_requires_storage iolinki)
add_test(NAME test_device_requires_storage COMMAND test_device_requires_storage)
add_executable(test_phy_l6362a test_phy_l6362a.c)
target_link_libraries(test_phy_l6362a iolinki)
add_test(NAME test_phy_l6362a COMMAND test_phy_l6362a)
# Board-level raw-octet PHY example (STM32G0 + L6362A) against a host mock of
# the CubeMX/HAL surface. Compiled in its real ISR-pump FRAME/FRAME mode.
add_executable(test_phy_l6362a_raw
    test_phy_l6362a_raw.c
    ../examples/stm32g0_l6362a_raw/phy_l6362a_raw.c
    mocks/stm32g0_l6362a/mock_stm32g0.c)
target_include_directories(test_phy_l6362a_raw PRIVATE
    mocks/stm32g0_l6362a
    ../examples/stm32g0_l6362a_raw)
target_compile_definitions(test_phy_l6362a_raw PRIVATE
    IOLINK_PHY_FEATURE_ISR_TX_ONLY=1
    IOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME
    IOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME)
# The board example uses C11 _Static_assert for its transport contract.
set_target_properties(test_phy_l6362a_raw PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)
add_test(NAME test_phy_l6362a_raw COMMAND test_phy_l6362a_raw)

# Graded concurrency-primitive contract (no cmocka dependency). The matrix
# builds this executable under every IOLINK_CONCURRENCY profile.
add_executable(test_port_capabilities test_port_capabilities.c)
target_link_libraries(test_port_capabilities iolinki)
add_test(NAME test_port_capabilities COMMAND test_port_capabilities)

# SPSC slot ring used by the ISR handoffs (no cmocka dependency).
add_executable(test_ring test_ring.c)
target_link_libraries(test_ring iolinki)
add_test(NAME test_ring COMMAND test_ring)

# Statistics counter sets (SHARED vs SPLIT), built under both modes.
add_executable(test_stats_split test_stats_split.c)
target_link_libraries(test_stats_split iolinki)
add_test(NAME test_stats_split COMMAND test_stats_split)

# Deterministic cut-point tests for the lock-free ISR handoffs (no cmocka).
add_executable(test_isr_cutpoints test_isr_cutpoints.c)
target_link_libraries(test_isr_cutpoints iolinki)
add_test(NAME test_isr_cutpoints COMMAND test_isr_cutpoints)

if(PkgConfig_FOUND)
    pkg_check_modules(CMOCKA QUIET cmocka)
endif()

# Fallback for systems without pkg-config or where it fails
if(NOT CMOCKA_FOUND)
    find_path(CMOCKA_INCLUDE_DIRS cmocka.h)
    find_library(CMOCKA_LIBRARIES cmocka)
    if(CMOCKA_INCLUDE_DIRS AND CMOCKA_LIBRARIES)
        set(CMOCKA_FOUND TRUE)
    endif()
endif()

if(CMOCKA_FOUND)
    message(STATUS "CMocka found: ${CMOCKA_LIBRARIES}")
    include_directories(${CMOCKA_INCLUDE_DIRS})
    # cmocka >= 2.0 marks the legacy type-generic macros (check_expected,
    # expect_value, ...) deprecated. The suite still supports older cmocka, so
    # keep the portable macros and silence the warning in the test targets only.
    add_compile_options(-Wno-deprecated-declarations)

    macro(add_iolink_test name source)
        add_executable(${name} ${source} test_helpers.c)
        target_link_libraries(${name} iolinki ${CMOCKA_LIBRARIES})
        add_test(NAME ${name} COMMAND ${name})
    endmacro()

    # Initial tests
    add_iolink_test(test_init test_init.c)
    add_iolink_test(test_reference_device test_reference_device.c)
    target_sources(test_reference_device PRIVATE ../examples/reference_device/reference_device.c)
    target_include_directories(test_reference_device PRIVATE ../examples/reference_device)
    if(IOLINK_PLATFORM STREQUAL "LINUX")
        add_iolink_test(test_conformance_device test_conformance_device.c)
        target_sources(test_conformance_device PRIVATE
            ../examples/conformance_device/conformance_device.c
            ../examples/conformance_device/conformance_profiles.c
            ../examples/conformance_device/conformance_storage.c)
        target_include_directories(test_conformance_device PRIVATE
            ../examples/conformance_device)
    endif()
    add_iolink_test(test_crc test_crc.c)
    add_iolink_test(test_dll test_dll.c)
    add_iolink_test(test_pd test_pd.c)
    add_iolink_test(test_isdu test_isdu.c)
    add_iolink_test(test_isdu_wire test_isdu_wire.c)
    add_iolink_test(test_events test_events.c)
    add_iolink_test(test_ds test_ds.c)
    add_iolink_test(test_error_recovery test_error_recovery.c)
    add_iolink_test(test_timing test_timing.c)
    add_iolink_test(test_integration_full test_integration_full.c)
    add_iolink_test(test_application test_application.c)
    add_iolink_test(test_m_sequence_types test_m_sequence_types.c)
    add_iolink_test(test_reentrant_device test_reentrant_device.c)

    # Portability Verification
    add_executable(test_locking test_locking.c)
    target_link_libraries(test_locking iolinki ${CMOCKA_LIBRARIES})
    add_test(NAME test_locking COMMAND test_locking)
    add_iolink_test(test_config test_config_verification.c)
    add_iolink_test(test_pd_variable test_pd_variable.c)
    add_iolink_test(test_baudrate test_baudrate.c)
    add_iolink_test(test_phy_diagnostics test_phy_diagnostics.c)
    add_iolink_test(test_phy_tiol112 test_phy_tiol112.c)
    add_iolink_test(test_app_pd test_app_pd.c)
    add_iolink_test(test_pd_lease test_pd_lease.c)
    add_iolink_test(test_sio_fallback test_sio_fallback.c)
    add_iolink_test(test_master_lost test_master_lost.c)

    # Specification-table and identification coverage (IO-Link V1.1.5)
    add_iolink_test(test_protocol_tables test_protocol_tables.c)
    add_iolink_test(test_crc_vectors test_crc_vectors.c)
    add_iolink_test(test_device test_device.c)
    add_iolink_test(test_ident_indices test_ident_indices.c)
    add_iolink_test(test_protocol_constants test_protocol_constants.c)
    add_iolink_test(test_system_commands test_system_commands.c)
    add_iolink_test(test_config_trim test_config_trim.c)
    # Spec-conformance assertions for known stack gaps (expected to fail until fixed)
    add_iolink_test(test_spec_conformance test_spec_conformance.c)
    # New Test Spec cases and known-gap xfails, annotated with SDCI_TC_xxxx IDs.
    add_iolink_test(test_conformance_gaps test_conformance_gaps.c)
    add_iolink_test(test_conformance_untested test_conformance_untested.c)
    if(IOLINK_PLATFORM STREQUAL "LINUX")
        target_sources(test_conformance_gaps PRIVATE
            ../examples/conformance_device/conformance_device.c
            ../examples/conformance_device/conformance_profiles.c
            ../examples/conformance_device/conformance_storage.c)
        target_include_directories(test_conformance_gaps PRIVATE
            ../examples/conformance_device)
        target_sources(test_conformance_untested PRIVATE
            ../examples/conformance_device/conformance_device.c
            ../examples/conformance_device/conformance_profiles.c
            ../examples/conformance_device/conformance_storage.c)
        target_include_directories(test_conformance_untested PRIVATE
            ../examples/conformance_device)
    endif()
    # ISR-pump transport internals
    add_iolink_test(test_dll_isr test_dll_isr.c)
    add_executable(test_isr_atomicity test_isr_atomicity.c)
    target_link_libraries(test_isr_atomicity iolinki ${CMOCKA_LIBRARIES})
    add_test(NAME test_isr_atomicity COMMAND test_isr_atomicity)
    add_executable(test_isr_mailbox_activity test_isr_mailbox_activity.c)
    target_link_libraries(test_isr_mailbox_activity iolinki)
    add_test(NAME test_isr_mailbox_activity COMMAND test_isr_mailbox_activity)
    add_executable(test_isr_stability test_isr_stability.c)
    target_link_libraries(test_isr_stability iolinki)
    add_test(NAME test_isr_stability COMMAND test_isr_stability)
    add_executable(test_isr_isdu_snapshot test_isr_isdu_snapshot.c)
    target_link_libraries(test_isr_isdu_snapshot iolinki)
    add_test(NAME test_isr_isdu_snapshot COMMAND test_isr_isdu_snapshot)
    add_executable(test_isr_observability test_isr_observability.c)
    target_link_libraries(test_isr_observability iolinki)
    add_test(NAME test_isr_observability COMMAND test_isr_observability)
    add_iolink_test(test_isr_window test_isr_window.c)
else()
    message(WARNING "CMocka not found, unit tests will be skipped. Install libcmocka-dev to enable them.")
endif()
