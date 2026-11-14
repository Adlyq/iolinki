# Transport the suite is built against (defined by the top-level CMakeLists).
# The mock PHY and the expected-frame assertions are transport neutral; only
# the targets that pin a BYTE transport contract (see iolink_test(BYTE_ONLY))
# are dropped for FRAME.
if(NOT IOLINK_TEST_TRANSPORT STREQUAL "BYTE" AND NOT IOLINK_TEST_TRANSPORT STREQUAL "FRAME")
    message(FATAL_ERROR "IOLINK_TEST_TRANSPORT must be BYTE or FRAME")
endif()

# The full suite exercises the untrimmed stack. Skip it for a trimmed build;
# build the library target (or configure with -DBUILD_TESTING=OFF) instead.
if(NOT IOLINK_FEATURE_DATA_STORAGE OR NOT IOLINK_FEATURE_EVENTS)
    message(STATUS "Feature-trimmed build: skipping the unit suite")
    return()
endif()

message(STATUS "Unit suite transport: ${IOLINK_TEST_TRANSPORT}")

# The transport label must match the ISR granularity the sources were actually
# compiled with; a mismatch would run BYTE tests under a "FRAME" row. The probe
# only has to compile.
add_executable(iolink_test_transport_probe transport_probe.c)
target_compile_definitions(iolink_test_transport_probe PRIVATE
    IOLINK_TEST_EXPECT_${IOLINK_TEST_TRANSPORT}=1)

# The suite is built from the same library sources, so the transport comes from
# the compile definitions (IOLINK_ISR_RX_MODE/IOLINK_ISR_TX_MODE) of the build.
# test_capability/test_profile_abi pin the compiled-in profile, so a mismatched
# -DIOLINK_TEST_TRANSPORT fails there instead of silently testing the wrong one.

# Several tests are plain assert()-based. CMake adds -DNDEBUG to Release builds,
# which would compile every assert() away and turn those tests into no-ops (and
# trip -Wunused-but-set-variable). Keep assertions active for the suite.
add_compile_options(-UNDEBUG)

# One test target: executable, link line and ctest registration in a single
# place. LIBS defaults to iolinki. CMOCKA adds test_helpers.c and links the
# cmocka library; the assert()-only tests stay without that dependency, and
# NO_HELPERS keeps it off the cmocka tests that do not use the helper unit.
# BYTE_ONLY marks a target whose assertions pin a BYTE transport contract; it
# is not built when the suite runs on the FRAME transport.
function(iolink_test)
    cmake_parse_arguments(T "CMOCKA;NO_HELPERS;NO_IOLINKI;BYTE_ONLY" "NAME;C_STANDARD"
        "SOURCES;DEFS;INCLUDES;LIBS" ${ARGN})
    if(T_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "iolink_test(${T_NAME}): unknown arguments: ${T_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT T_NAME OR NOT T_SOURCES)
        message(FATAL_ERROR "iolink_test: NAME and SOURCES are required")
    endif()
    if(T_BYTE_ONLY AND NOT IOLINK_TEST_TRANSPORT STREQUAL "BYTE")
        set_property(GLOBAL APPEND PROPERTY IOLINK_TEST_BYTE_ONLY_SKIPPED ${T_NAME})
        return()
    endif()

    set(sources ${T_SOURCES})
    if(T_CMOCKA AND NOT T_NO_HELPERS)
        list(APPEND sources test_helpers.c)
    endif()
    add_executable(${T_NAME} ${sources})

    if(T_DEFS)
        target_compile_definitions(${T_NAME} PRIVATE ${T_DEFS})
    endif()
    if(T_INCLUDES)
        target_include_directories(${T_NAME} PRIVATE ${T_INCLUDES})
    endif()
    # The board example uses C11 _Static_assert for its transport contract.
    if(T_C_STANDARD)
        set_target_properties(${T_NAME} PROPERTIES
            C_STANDARD ${T_C_STANDARD}
            C_STANDARD_REQUIRED ON)
    endif()

    if(T_LIBS)
        set(libs ${T_LIBS})
    elseif(T_NO_IOLINKI)
        set(libs)
    else()
        set(libs iolinki)
    endif()
    if(T_CMOCKA)
        list(APPEND libs ${CMOCKA_LIBRARIES})
    endif()
    target_link_libraries(${T_NAME} ${libs})

    add_test(NAME ${T_NAME} COMMAND ${T_NAME})
endfunction()

# External-workspace profile: a second build of the sources without the
# embedded scratch, so the -16 rejection and the explicit-workspace path are
# compiled and executed (not only syntax-probed). No cmocka dependency.
add_library(iolink_workspace_fixture STATIC ${IOLINK_SOURCES})
target_include_directories(iolink_workspace_fixture PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(iolink_workspace_fixture PUBLIC IOLINK_WORKSPACE_EMBEDDED=0)
iolink_test(NAME test_workspace_external
    SOURCES test_workspace_external.c
    LIBS iolink_workspace_fixture)

find_package(PkgConfig QUIET)
# 参数事实源是 C 对象字典表；校验 IODD 导出使用表项而不是 JSON 变量。
find_package(Python3 QUIET)
if(Python3_FOUND)
    add_test(NAME od_iodd_export
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_od_iodd_export.py)
    # The generated state-machine table, Mermaid and CSV must match the
    # C source of truth; --check fails on drift (also run in CI).
    add_test(NAME dll_statemachine_gen_check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gen_dll_statemachine.py --check)
endif()

# OD 后端链接协议栈（表项编解码在 src/od.c）。
iolink_test(NAME test_switching_sensor
    SOURCES test_switching_sensor.c
        ../examples/switching_sensor/switching_sensor.c
        ../examples/switching_sensor/switching_sensor_od.c
    INCLUDES ../examples/switching_sensor)
iolink_test(NAME test_deadline_rollover SOURCES test_deadline_rollover.c)
iolink_test(NAME test_timer_table SOURCES test_timer_table.c)
iolink_test(NAME test_dll_statemachine SOURCES test_dll_statemachine.c)
iolink_test(NAME test_dll_statemachine_gen SOURCES test_dll_statemachine_gen.c)
# No cmocka dependency (plain asserts over the plan table and ISR pump).
iolink_test(NAME test_reply_plan SOURCES test_reply_plan.c)

# Reply-plan overflow: the stock library has 10 slots, so the small-table
# behaviour needs its own build of the sources with the capacity overridden.
add_library(iolink_plan_slots_fixture STATIC ${IOLINK_SOURCES})
target_include_directories(iolink_plan_slots_fixture PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(iolink_plan_slots_fixture PUBLIC IOLINK_REPLY_PLAN_SLOTS=1)
iolink_test(NAME test_plan_slots_overflow
    SOURCES test_plan_slots_overflow.c
    LIBS iolink_plan_slots_fixture)
# CONSUME 计划帧不进邮箱：ISR 交接帧时间戳，主泵补做 T_CYCLE/T_REN 记账。
iolink_test(NAME test_isr_consumed_timing SOURCES test_isr_consumed_timing.c)
iolink_test(NAME test_device_time_source SOURCES test_device_time_source.c)
iolink_test(NAME test_device_requires_critical SOURCES test_device_requires_critical.c)
iolink_test(NAME test_phy_l6362a SOURCES test_phy_l6362a.c)
# Board-level raw-octet PHY example (STM32G0 + L6362A) against a host mock of
# the CubeMX/HAL surface. Compiled in its real ISR-pump FRAME/FRAME mode.
iolink_test(NAME test_phy_l6362a_raw
    SOURCES test_phy_l6362a_raw.c
        ../examples/stm32g0_l6362a_raw/phy_l6362a_raw.c
        mocks/stm32g0_l6362a/mock_stm32g0.c
    INCLUDES mocks/stm32g0_l6362a
        ../examples/stm32g0_l6362a_raw
    DEFS IOLINK_PHY_FEATURE_ISR_TX_ONLY=1
        IOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME
        IOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME
    NO_IOLINKI
    C_STANDARD 11)

# Graded concurrency-primitive contract (no cmocka dependency). The matrix
# builds this executable under every IOLINK_CONCURRENCY profile.
iolink_test(NAME test_port_capabilities SOURCES test_port_capabilities.c)

# SPSC slot ring used by the ISR handoffs (no cmocka dependency).
iolink_test(NAME test_ring SOURCES test_ring.c)

# Context size contract: a layout or buffer default change must update the
# assertions (and docs/MEMORY_GUIDE.md) explicitly. The exact sizes are a BYTE
# transport contract: IOLINK_PD_IN_BUFFERS is 3 on FRAME/FRAME without tx_busy,
# so the pinned numbers do not describe that layout.
iolink_test(NAME test_ctx_sizes SOURCES test_ctx_sizes.c)

# Log sink contract: runtime level filter, forwarding, default silence, ISR switch.
iolink_test(NAME test_log SOURCES test_log.c test_log_isr_on.c)

# Statistics counter sets (SHARED vs SPLIT), built under both modes.
iolink_test(NAME test_stats_split SOURCES test_stats_split.c)

# Deterministic cut-point tests for the lock-free ISR handoffs (no cmocka).
iolink_test(NAME test_isr_cutpoints SOURCES test_isr_cutpoints.c)

# TX FRAME line-state hook (tx_busy): the stock library is built in the default
# BYTE/BYTE layout, so the FRAME behaviour gets its own build of the sources.
add_library(iolink_tx_busy_fixture STATIC ${IOLINK_SOURCES})
target_include_directories(iolink_tx_busy_fixture PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(iolink_tx_busy_fixture PUBLIC
    IOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME
    IOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME
    IOLINK_PHY_FEATURE_TX_BUSY=1)
iolink_test(NAME test_tx_busy SOURCES test_tx_busy.c LIBS iolink_tx_busy_fixture)

# PD-input path: the callback and the polling API now coexist in one
# binary, so the test needs no fixture build of the sources.
iolink_test(NAME test_app_pd_callback SOURCES test_app_pd_callback.c)

# 应用 OD 表自检：非法配置在设备初始化前被拒绝（库内部 API）。
if(IOLINK_FEATURE_VALIDATE)
    iolink_test(NAME test_od_validate SOURCES test_od_validate.c
        INCLUDES ${CMAKE_SOURCE_DIR}/src)
endif()


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

    # Initial tests
    iolink_test(NAME test_init SOURCES test_init.c CMOCKA)
    iolink_test(NAME test_reference_device
        SOURCES test_reference_device.c ../examples/reference_device/reference_device.c
        INCLUDES ../examples/reference_device
        CMOCKA)
    if(IOLINK_PLATFORM STREQUAL "LINUX")
        # Host conformance example, shared by three test binaries: build it once
        # as a library instead of recompiling its sources per target.
        add_library(conformance_device_example STATIC
            ../examples/conformance_device/conformance_device.c
            ../examples/conformance_device/conformance_device_od.c
            ../examples/conformance_device/conformance_profiles.c)
        target_include_directories(conformance_device_example PUBLIC
            ../examples/conformance_device)
        target_link_libraries(conformance_device_example PUBLIC iolinki)
        iolink_test(NAME test_conformance_device
            SOURCES test_conformance_device.c
            LIBS conformance_device_example
            CMOCKA)
    endif()
    iolink_test(NAME test_crc SOURCES test_crc.c CMOCKA)
    iolink_test(NAME test_dll SOURCES test_dll.c CMOCKA)
    iolink_test(NAME test_pd SOURCES test_pd.c CMOCKA)
    iolink_test(NAME test_isdu SOURCES test_isdu.c CMOCKA)
    iolink_test(NAME test_isdu_wire SOURCES test_isdu_wire.c CMOCKA)
    # 安全网：表驱动重构前钉死强制索引/Direct Parameter/标签的线上行为。
    iolink_test(NAME test_od_characterization SOURCES test_od_characterization.c CMOCKA)
    # 对象字典：一行表项新增参数 + switching_sensor 迁移 + DS 成员契约。
    iolink_test(NAME test_od_app
        SOURCES test_od_app.c
            ../examples/switching_sensor/switching_sensor.c
            ../examples/switching_sensor/switching_sensor_od.c
        INCLUDES ../examples/switching_sensor
        CMOCKA)
    # 应用 OD 表参与 DS 集合（设备层接线、暂存/序列化/恢复、整份预校验）。
    iolink_test(NAME test_ds_app_od SOURCES test_ds_app_od.c CMOCKA)
    iolink_test(NAME test_events SOURCES test_events.c CMOCKA)
    # 诊断注册表：Table D.1 内容、电平/脉冲语义、视图一致性与复位。
    iolink_test(NAME test_diag SOURCES test_diag.c CMOCKA)
    iolink_test(NAME test_ds SOURCES test_ds.c CMOCKA)
    iolink_test(NAME test_error_model SOURCES test_error_model.c CMOCKA)
    iolink_test(NAME test_error_recovery SOURCES test_error_recovery.c CMOCKA)
    iolink_test(NAME test_timing SOURCES test_timing.c CMOCKA)
    iolink_test(NAME test_integration_full SOURCES test_integration_full.c CMOCKA)
    iolink_test(NAME test_workspace SOURCES test_workspace.c CMOCKA)
    iolink_test(NAME test_application SOURCES test_application.c CMOCKA)
    # 能力表：构建档位、resolve() 与运行时覆盖规则。
    iolink_test(NAME test_capability SOURCES test_capability.c CMOCKA)
    # 链接期档位断言：正向（同档位可链接）；反向由
    # tools/check_profile_abi.sh 在构建矩阵里验证。
    iolink_test(NAME test_profile_abi SOURCES test_profile_abi.c CMOCKA)
    iolink_test(NAME test_m_sequence_types SOURCES test_m_sequence_types.c CMOCKA)
    iolink_test(NAME test_reentrant_device SOURCES test_reentrant_device.c CMOCKA)

    # Portability Verification
    iolink_test(NAME test_locking SOURCES test_locking.c CMOCKA NO_HELPERS)
    iolink_test(NAME test_config SOURCES test_config_verification.c CMOCKA)
    iolink_test(NAME test_pd_variable SOURCES test_pd_variable.c CMOCKA)
    iolink_test(NAME test_baudrate SOURCES test_baudrate.c CMOCKA)
    iolink_test(NAME test_phy_diagnostics SOURCES test_phy_diagnostics.c CMOCKA)
    iolink_test(NAME test_phy_tiol112 SOURCES test_phy_tiol112.c CMOCKA)
    iolink_test(NAME test_app_pd SOURCES test_app_pd.c CMOCKA)
    iolink_test(NAME test_pd_lease SOURCES test_pd_lease.c CMOCKA)
set_tests_properties(test_pd_lease PROPERTIES SKIP_RETURN_CODE 77)
    iolink_test(NAME test_isdu_buffers SOURCES test_isdu_buffers.c)
    iolink_test(NAME test_sio_fallback SOURCES test_sio_fallback.c CMOCKA)
    iolink_test(NAME test_master_lost SOURCES test_master_lost.c CMOCKA)

    # Specification-table and identification coverage (IO-Link V1.1.5)
    iolink_test(NAME test_protocol_tables SOURCES test_protocol_tables.c CMOCKA)
    iolink_test(NAME test_crc_vectors SOURCES test_crc_vectors.c CMOCKA)
    iolink_test(NAME test_device SOURCES test_device.c CMOCKA)
    iolink_test(NAME test_ident_indices SOURCES test_ident_indices.c CMOCKA)
    iolink_test(NAME test_protocol_constants SOURCES test_protocol_constants.c CMOCKA)
    iolink_test(NAME test_system_commands SOURCES test_system_commands.c CMOCKA)
    iolink_test(NAME test_config_trim SOURCES test_config_trim.c CMOCKA)
    # Spec-conformance assertions for known stack gaps (expected to fail until fixed)
    iolink_test(NAME test_spec_conformance SOURCES test_spec_conformance.c CMOCKA)
    if(IOLINK_PLATFORM STREQUAL "LINUX")
        # New Test Spec cases and known-gap xfails, annotated with SDCI_TC_xxxx IDs.
        # They link the host conformance-device example, so they are host-only.
        iolink_test(NAME test_conformance_gaps
            SOURCES test_conformance_gaps.c
            LIBS conformance_device_example
            CMOCKA)
        iolink_test(NAME test_conformance_untested
            SOURCES test_conformance_untested.c
            LIBS conformance_device_example
            CMOCKA)
    endif()
    # ISR-pump transport internals
    iolink_test(NAME test_dll_isr SOURCES test_dll_isr.c CMOCKA)
    iolink_test(NAME test_isr_atomicity SOURCES test_isr_atomicity.c CMOCKA NO_HELPERS)
    iolink_test(NAME test_isr_mailbox_activity SOURCES test_isr_mailbox_activity.c)
    iolink_test(NAME test_isr_stability SOURCES test_isr_stability.c)
    iolink_test(NAME test_isr_isdu_snapshot SOURCES test_isr_isdu_snapshot.c)
    iolink_test(NAME test_isr_observability SOURCES test_isr_observability.c)
    iolink_test(NAME test_isr_window SOURCES test_isr_window.c CMOCKA)
else()
    message(WARNING "CMocka not found, unit tests will be skipped. Install libcmocka-dev to enable them.")
endif()

get_property(skipped_targets GLOBAL PROPERTY IOLINK_TEST_BYTE_ONLY_SKIPPED)
if(skipped_targets)
    list(LENGTH skipped_targets skipped_count)
    message(STATUS
        "${IOLINK_TEST_TRANSPORT} transport: ${skipped_count} BYTE-only target(s) not built: "
        "${skipped_targets}")
endif()
