/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_od_validate.c
 * @brief 应用对象字典表初始化期自检（od_validate_app）的单元测试。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "od_internal.h"

/* --- 合法的应用值与后端 ------------------------------------------------ */

static uint16_t g_scalar;
static uint8_t g_buf[16];
static uint8_t g_len;

static iolink_result_t h_read(void* user, const iolink_od_entry_t* entry, uint8_t* out, size_t cap)
{
    (void) user;
    (void) entry;
    if (cap < 1U) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    out[0] = 0U;
    return iolink_result_ok(1U);
}

static iolink_result_t h_write(void* user, const iolink_od_entry_t* entry, uint8_t subindex,
                               const uint8_t* data, size_t len)
{
    (void) user;
    (void) entry;
    (void) subindex;
    (void) data;
    (void) len;
    return iolink_result_ok(0U);
}

/* validate 与 on_change 共用同一签名。 */
static iolink_result_t h_hook(void* user, const iolink_od_entry_t* entry, const uint8_t* data,
                              size_t len)
{
    (void) user;
    (void) entry;
    (void) data;
    (void) len;
    return iolink_result_ok(0U);
}

enum
{
    IDX_SCALAR = 0,
    IDX_BYTES = 1,
    IDX_HANDLER = 2,
    VALID_COUNT = 3
};

static const iolink_od_entry_t k_valid_table[] = {
    IOLINK_OD_SCALAR_STORAGE(0x0100U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_STORE,
                             IOLINK_OD_STORAGE_VOLATILE, 0U, 0xFFFFU, 0U, "ValidScalar", &g_scalar),
    IOLINK_OD_BYTES_STORAGE(0x0101U, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                            IOLINK_OD_STORAGE_VOLATILE, 16U, "ValidBytes", g_buf, &g_len),
    IOLINK_OD_SCALAR_HANDLER(0x0102U, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RW,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, 0U, 0xFFU, 0U,
                             "ValidHandler", h_read, h_write),
};

/** @brief 单表项应用表。 */
static int validate_one(const iolink_od_entry_t* entry)
{
    iolink_od_app_t app;

    app.entries = entry;
    app.count = 1U;
    app.user = NULL;
    return od_validate_app(&app);
}

static void test_null_and_empty(void** state)
{
    (void) state;
    iolink_od_app_t app;

    assert_int_equal(od_validate_app(NULL), OD_VALIDATE_OK);

    app.entries = NULL;
    app.count = 0U;
    app.user = NULL;
    assert_int_equal(od_validate_app(&app), OD_VALIDATE_OK);
}

static void test_table_malformed(void** state)
{
    (void) state;
    iolink_od_app_t app;

    app.entries = NULL;
    app.count = 1U;
    app.user = NULL;
    assert_int_equal(od_validate_app(&app), OD_VALIDATE_ERR_TABLE);
}

static void test_valid_table(void** state)
{
    (void) state;
    iolink_od_app_t app;

    for (size_t i = 0U; i < (size_t) VALID_COUNT; i++) {
        assert_int_equal(validate_one(&k_valid_table[i]), OD_VALIDATE_OK);
    }

    app.entries = k_valid_table;
    app.count = (size_t) VALID_COUNT;
    app.user = NULL;
    assert_int_equal(od_validate_app(&app), OD_VALIDATE_OK);
}

static void test_handler_none(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_SCALAR];

    entry.handler = (uint8_t) IOLINK_OD_HANDLER_SYSTEM_COMMAND;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_HANDLER_NONE);
}

static void test_datatype(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_SCALAR];

    entry.datatype = (uint8_t) IOLINK_OD_DT_COUNT;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_DATATYPE);
}

static void test_access(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_SCALAR];

    entry.access = 0U;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_ACCESS);
}

static void test_semantics(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_SCALAR];

    entry.write_semantics = 99U;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_SEMANTICS);
}

static void test_storage_bits(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_SCALAR];

    entry.storage = 0x04U;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_STORAGE_BITS);
}

static void test_trigger_storage(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_SCALAR];

    entry.write_semantics = (uint8_t) IOLINK_OD_WRITE_TRIGGER;
    entry.storage = (uint8_t) IOLINK_OD_STORAGE_DS;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_TRIGGER_STORAGE);
}

static void test_bytes_unbounded(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_BYTES];

    entry.block_len = 0U;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_BYTES_UNBOUNDED);
}

static void test_ro_hooks(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_SCALAR];

    entry.access = (uint8_t) IOLINK_OD_ACCESS_RO;
    entry.on_change = h_hook;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_RO_HOOK);

    entry.on_change = NULL;
    entry.validate = h_hook;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_RO_HOOK);
}

static void test_range(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_SCALAR];

    entry.range_min = 10U;
    entry.range_max = 5U;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_RANGE);

    entry.range_min = 0U;
    entry.range_max = 10U;
    entry.scalar_default = 11U;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_DEFAULT_RANGE);
}

static void test_backend(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_SCALAR];

    entry.backend = 2U;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_BACKEND);
}

static void test_storage_null(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_SCALAR];

    entry.u.storage.value = NULL;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_STORAGE_NULL);

    entry = k_valid_table[IDX_BYTES];
    entry.u.storage.str_len = NULL;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_STORAGE_NULL);
}

static void test_handler_functions(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_HANDLER];

    entry.u.handler.read = NULL;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_HANDLER_READ);

    entry = k_valid_table[IDX_HANDLER];
    entry.u.handler.write = NULL;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_HANDLER_WRITE);
}

static void test_duplicate(void** state)
{
    (void) state;
    iolink_od_entry_t entries[2];
    iolink_od_app_t app;

    entries[0] = k_valid_table[IDX_SCALAR];
    entries[1] = k_valid_table[IDX_SCALAR];
    app.entries = entries;
    app.count = 2U;
    app.user = NULL;
    assert_int_equal(od_validate_app(&app), OD_VALIDATE_ERR_DUPLICATE);
}

static void test_std_index(void** state)
{
    (void) state;
    iolink_od_entry_t entry = k_valid_table[IDX_SCALAR];

    entry.index = 0x0024U;
    assert_int_equal(validate_one(&entry), OD_VALIDATE_ERR_STD_INDEX);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_null_and_empty),    cmocka_unit_test(test_table_malformed),
        cmocka_unit_test(test_valid_table),       cmocka_unit_test(test_handler_none),
        cmocka_unit_test(test_datatype),          cmocka_unit_test(test_access),
        cmocka_unit_test(test_semantics),         cmocka_unit_test(test_storage_bits),
        cmocka_unit_test(test_trigger_storage),   cmocka_unit_test(test_bytes_unbounded),
        cmocka_unit_test(test_ro_hooks),          cmocka_unit_test(test_range),
        cmocka_unit_test(test_backend),           cmocka_unit_test(test_storage_null),
        cmocka_unit_test(test_handler_functions), cmocka_unit_test(test_duplicate),
        cmocka_unit_test(test_std_index),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
