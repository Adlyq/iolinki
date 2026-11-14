# The full suite exercises the untrimmed stack. Skip it for a trimmed build;
# build the library target (or configure with -DBUILD_TESTING=OFF) instead.
if(NOT IOLINK_FEATURE_DATA_STORAGE OR NOT IOLINK_FEATURE_EVENTS)
    message(STATUS "Feature-trimmed build: skipping the unit suite")
    return()
endif()

# Several tests are plain assert()-based. CMake adds -DNDEBUG to Release builds,
# which would compile every assert() away and turn those tests into no-ops (and
# trip -Wunused-but-set-variable). Keep assertions active for the suite.
add_compile_options(-UNDEBUG)

# External-workspace profile: a second build of the sources without the
# embedded scratch, so the -16 rejection and the explicit-workspace path are
# compiled and executed (not only syntax-probed). No cmocka dependency.
add_library(iolink_workspace_fixture STATIC ${IOLINK_SOURCES})
target_include_directories(iolink_workspace_fixture PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(iolink_workspace_fixture PUBLIC IOLINK_WORKSPACE_EMBEDDED=0)
add_executable(test_workspace_external test_workspace_external.c)
target_link_libraries(test_workspace_external iolink_workspace_fixture)
add_test(NAME test_workspace_external COMMAND test_workspace_external)

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

add_executable(test_switching_sensor test_switching_sensor.c
    ../examples/switching_sensor/switching_sensor.c
    ../examples/switching_sensor/switching_sensor_od.c)
target_include_directories(test_switching_sensor PRIVATE ../examples/switching_sensor)
# OD 后端链接协议栈（表项编解码在 src/od.c）。
target_link_libraries(test_switching_sensor iolinki)
add_test(NAME test_switching_sensor COMMAND test_switching_sensor)
add_executable(test_params_persistence test_params_persistence.c)
target_link_libraries(test_params_persistence iolinki)
add_test(NAME test_params_persistence COMMAND test_params_persistence)
add_executable(test_deadline_rollover test_deadline_rollover.c)
target_link_libraries(test_deadline_rollover iolinki)
add_test(NAME test_deadline_rollover COMMAND test_deadline_rollover)
add_executable(test_timer_table test_timer_table.c)
target_link_libraries(test_timer_table iolinki)
add_test(NAME test_timer_table COMMAND test_timer_table)
add_executable(test_dll_statemachine test_dll_statemachine.c)
target_link_libraries(test_dll_statemachine iolinki)
add_test(NAME test_dll_statemachine COMMAND test_dll_statemachine)
add_executable(test_dll_statemachine_gen test_dll_statemachine_gen.c)
target_link_libraries(test_dll_statemachine_gen iolinki)
add_test(NAME test_dll_statemachine_gen COMMAND test_dll_statemachine_gen)
# No cmocka dependency (plain asserts over the plan table and ISR pump).
add_executable(test_reply_plan test_reply_plan.c)
target_link_libraries(test_reply_plan iolinki)
add_test(NAME test_reply_plan COMMAND test_reply_plan)

# Reply-plan overflow: the stock library has 10 slots, so the small-table
# behaviour needs its own build of the sources with the capacity overridden.
add_library(iolink_plan_slots_fixture STATIC ${IOLINK_SOURCES})
target_include_directories(iolink_plan_slots_fixture PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(iolink_plan_slots_fixture PUBLIC IOLINK_REPLY_PLAN_SLOTS=1)
add_executable(test_plan_slots_overflow test_plan_slots_overflow.c)
target_link_libraries(test_plan_slots_overflow iolink_plan_slots_fixture)
add_test(NAME test_plan_slots_overflow COMMAND test_plan_slots_overflow)
# CONSUME 计划帧不进邮箱：ISR 交接帧时间戳，主泵补做 T_CYCLE/T_REN 记账。
add_executable(test_isr_consumed_timing test_isr_consumed_timing.c)
target_link_libraries(test_isr_consumed_timing iolinki)
add_test(NAME test_isr_consumed_timing COMMAND test_isr_consumed_timing)
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

# Context size contract: a layout or buffer default change must update the
# assertions (and docs/MEMORY_GUIDE.md) explicitly.
add_executable(test_ctx_sizes test_ctx_sizes.c)
target_link_libraries(test_ctx_sizes iolinki)
add_test(NAME test_ctx_sizes COMMAND test_ctx_sizes)

# Log sink contract: runtime level filter, forwarding, default silence, ISR switch.
add_executable(test_log test_log.c test_log_isr_on.c)
target_link_libraries(test_log iolinki)
add_test(NAME test_log COMMAND test_log)

# Statistics counter sets (SHARED vs SPLIT), built under both modes.
add_executable(test_stats_split test_stats_split.c)
target_link_libraries(test_stats_split iolinki)
add_test(NAME test_stats_split COMMAND test_stats_split)

# Deterministic cut-point tests for the lock-free ISR handoffs (no cmocka).
add_executable(test_isr_cutpoints test_isr_cutpoints.c)
target_link_libraries(test_isr_cutpoints iolinki)
add_test(NAME test_isr_cutpoints COMMAND test_isr_cutpoints)

# TX FRAME line-state hook (tx_busy): the stock library is built in the default
# BYTE/BYTE layout, so the FRAME behaviour gets its own build of the sources.
add_library(iolink_tx_busy_fixture STATIC ${IOLINK_SOURCES})
target_include_directories(iolink_tx_busy_fixture PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(iolink_tx_busy_fixture PUBLIC
    IOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME
    IOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME
    IOLINK_PHY_FEATURE_TX_BUSY=1)
add_executable(test_tx_busy test_tx_busy.c)
target_link_libraries(test_tx_busy iolink_tx_busy_fixture)
add_test(NAME test_tx_busy COMMAND test_tx_busy)

# PD-input path: the callback and the polling API now coexist in one
# binary, so the test needs no fixture build of the sources.
add_executable(test_app_pd_callback test_app_pd_callback.c)
target_link_libraries(test_app_pd_callback iolinki)
add_test(NAME test_app_pd_callback COMMAND test_app_pd_callback)

# 应用 OD 表自检：非法配置在设备初始化前被拒绝（库内部 API）。
if(IOLINK_FEATURE_VALIDATE)
    add_executable(test_od_validate test_od_validate.c)
    target_link_libraries(test_od_validate iolinki)
    target_include_directories(test_od_validate PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME test_od_validate COMMAND test_od_validate)
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
            ../examples/conformance_device/conformance_device_od.c
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
    # 安全网：表驱动重构前钉死强制索引/Direct Parameter/标签的线上行为。
    add_iolink_test(test_od_characterization test_od_characterization.c)
    # 对象字典：一行表项新增参数 + switching_sensor 迁移 + DS 成员契约。
    add_iolink_test(test_od_app test_od_app.c)
    target_sources(test_od_app PRIVATE
        ../examples/switching_sensor/switching_sensor.c
        ../examples/switching_sensor/switching_sensor_od.c)
    target_include_directories(test_od_app PRIVATE ../examples/switching_sensor)
    # 应用 OD 表参与 DS 集合（设备层接线、暂存/序列化/恢复、整份预校验）。
    add_iolink_test(test_ds_app_od test_ds_app_od.c)
    add_iolink_test(test_events test_events.c)
    # 诊断注册表：Table D.1 内容、电平/脉冲语义、视图一致性与复位。
    add_iolink_test(test_diag test_diag.c)
    add_iolink_test(test_ds test_ds.c)
    add_iolink_test(test_error_model test_error_model.c)
    add_iolink_test(test_error_recovery test_error_recovery.c)
    add_iolink_test(test_timing test_timing.c)
    add_iolink_test(test_integration_full test_integration_full.c)
    add_iolink_test(test_workspace test_workspace.c)
    add_iolink_test(test_application test_application.c)
    # 能力表：构建档位、resolve() 与运行时覆盖规则。
    add_iolink_test(test_capability test_capability.c)
    # 链接期档位断言：正向（同档位可链接）；反向由
    # tools/check_profile_abi.sh 在构建矩阵里验证。
    add_iolink_test(test_profile_abi test_profile_abi.c)
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
    add_executable(test_isdu_buffers test_isdu_buffers.c)
    target_link_libraries(test_isdu_buffers iolinki)
    add_test(NAME test_isdu_buffers COMMAND test_isdu_buffers)
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
    if(IOLINK_PLATFORM STREQUAL "LINUX")
        # New Test Spec cases and known-gap xfails, annotated with SDCI_TC_xxxx IDs.
        # They link the host conformance-device example, so they are host-only.
        add_iolink_test(test_conformance_gaps test_conformance_gaps.c)
        target_sources(test_conformance_gaps PRIVATE
            ../examples/conformance_device/conformance_device.c
            ../examples/conformance_device/conformance_device_od.c
            ../examples/conformance_device/conformance_profiles.c
            ../examples/conformance_device/conformance_storage.c)
        target_include_directories(test_conformance_gaps PRIVATE
            ../examples/conformance_device)
        add_iolink_test(test_conformance_untested test_conformance_untested.c)
        target_sources(test_conformance_untested PRIVATE
            ../examples/conformance_device/conformance_device.c
            ../examples/conformance_device/conformance_device_od.c
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
