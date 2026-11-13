/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_params_persistence.c
 * @brief 参数镜像（PERSIST 表项）的持久化契约。
 *
 * 覆盖：
 *  - 标签与应用 STORAGE 参数共用同一镜像；
 *  - NVM 写失败时写路径返回错误且 RAM 值回滚；
 *  - 重新加载镜像可以恢复值；
 *  - ISDU 写失败产生否定应答。
 */

#include "iolinki/data_storage.h"
#include "iolinki/isdu.h"
#include "iolinki/od.h"
#include "iolinki/params.h"
#include "iolinki/port.h"
#include "iolinki/protocol.h"
#include "iolinki/storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t g_nvm[IOLINK_STORAGE_TOTAL_SIZE];
static int g_fail_write;
static int g_writes;

static int mock_read(void* user, uint32_t offset, uint8_t* data, size_t len)
{
    (void) user;
    if ((offset > sizeof(g_nvm)) || (len > sizeof(g_nvm) - offset)) {
        return -1;
    }
    (void) memcpy(data, &g_nvm[offset], len);
    return 0;
}

static int mock_write(void* user, uint32_t offset, const uint8_t* data, size_t len)
{
    (void) user;
    ++g_writes;
    if (g_fail_write != 0) {
        return -1;
    }
    if ((offset > sizeof(g_nvm)) || (len > sizeof(g_nvm) - offset)) {
        return -1;
    }
    (void) memcpy(&g_nvm[offset], data, len);
    return 0;
}

static const iolink_storage_api_t g_storage = {
    .user = NULL, .read = mock_read, .write = mock_write};

/* 应用 STORAGE 参数：值和持久化位都在表项里。 */
static uint16_t g_vendor_value;
static const iolink_od_entry_t k_app_entries[] = {
    IOLINK_OD_SCALAR_STORAGE(0x0180U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_STORE,
                             IOLINK_OD_STORAGE_DS | IOLINK_OD_STORAGE_PERSIST, 0U, 0xFFFFU, 0U,
                             "PersistedParam", &g_vendor_value),
};
static const iolink_od_app_t g_app = {
    .entries = k_app_entries,
    .count = sizeof(k_app_entries) / sizeof(k_app_entries[0]),
    .user = NULL,
};

/** @brief 与 ISDU 层相同的写后持久化钩子。 */
static iolink_result_t flush_hook(void* user)
{
    return (iolink_ds_save_persistent((iolink_ds_ctx_t*) user) == 0)
               ? iolink_result_ok(0U)
               : iolink_result_error(IOLINK_RESULT_IO);
}

static void bind_device(iolink_params_ctx_t* params, iolink_ds_ctx_t* ds)
{
    iolink_ds_init(ds, &g_storage);
    iolink_ds_bind_sources(ds, &g_app, params);
}

int main(void)
{
    iolink_device_info_ctx_t info;
    iolink_params_ctx_t params;
    iolink_ds_ctx_t ds;
    const uint8_t original[] = "original";
    const uint8_t replacement[] = "replacement";
    uint8_t result[32];

    (void) memset(g_nvm, 0xFF, sizeof(g_nvm));
    iolink_device_info_ctx_init(&info, NULL);
    iolink_params_ctx_init(&params, &info);
    bind_device(&params, &ds);
    /* 擦除的 NVM（0xFF）视为无镜像：默认值保持不变。 */
    uint8_t baseline[32] = {0U};
    const iolink_result_t before =
        iolink_params_ctx_read(&params, IOLINK_IDX_APPLICATION_TAG, 0U, baseline, sizeof(baseline));
    assert(before.code == 0);
    assert(iolink_ds_load_persistent(&ds) == -1);
    assert(iolink_params_ctx_read(&params, IOLINK_IDX_APPLICATION_TAG, 0U, result, sizeof(result))
               .len == before.len);
    assert(memcmp(result, baseline, before.len) == 0);

    const iolink_od_entry_t* tag = iolink_od_find(NULL, IOLINK_IDX_APPLICATION_TAG, 0U);
    assert(tag != NULL);

    iolink_od_ctx_t od = {.app = &g_app, .params = &params, .flush = flush_hook, .flush_user = &ds};

    /* 成功写入：值生效且镜像落盘。 */
    g_fail_write = 0;
    assert(iolink_od_write_value(&od, tag, 0U, original, sizeof(original) - 1U).code == 0);
    /* 镜像确实写到偏移 0，且是本栈的 magic（不钉内部写次数）。 */
    assert(g_writes >= 1);
    assert(g_nvm[0] == (uint8_t) (IOLINK_STORAGE_MAGIC >> 24));
    assert(g_nvm[3] == (uint8_t) IOLINK_STORAGE_MAGIC);

    /* NVM 失败：返回 IO，RAM 回滚到写前值。 */
    g_fail_write = 1;
    assert(iolink_od_write_value(&od, tag, 0U, replacement, sizeof(replacement) - 1U).code ==
           IOLINK_RESULT_IO);
    const iolink_result_t retained =
        iolink_params_ctx_read(&params, IOLINK_IDX_APPLICATION_TAG, 0U, result, sizeof(result));
    assert(retained.code == 0);
    assert(retained.len == sizeof(original) - 1U);
    assert(memcmp(result, original, sizeof(original) - 1U) == 0);
    g_fail_write = 0;

    /* 应用 STORAGE 参数：成功写后可以从镜像恢复。 */
    const uint8_t wire[2] = {0x12U, 0x34U};
    assert(iolink_od_write_value(&od, &k_app_entries[0], 0U, wire, sizeof(wire)).code == 0);
    assert(g_vendor_value == 0x1234U);

    /* 模拟重启：新上下文、同一 NVM。 */
    iolink_params_ctx_t params2;
    iolink_ds_ctx_t ds2;
    iolink_device_info_ctx_t info2;
    iolink_device_info_ctx_init(&info2, NULL);
    iolink_params_ctx_init(&params2, &info2);
    g_vendor_value = 0U;
    iolink_ds_init(&ds2, &g_storage);
    iolink_ds_bind_sources(&ds2, &g_app, &params2);
    assert(iolink_ds_load_persistent(&ds2) == 0);
    assert(g_vendor_value == 0x1234U);

    const iolink_result_t restored =
        iolink_params_ctx_read(&params2, IOLINK_IDX_APPLICATION_TAG, 0U, result, sizeof(result));
    assert(restored.code == 0);
    assert(restored.len == sizeof(original) - 1U);
    assert(memcmp(result, original, sizeof(original) - 1U) == 0);

    /* ISDU 写路径：flush 失败必须产生否定应答，且标签保持不变。 */
    iolink_isdu_ctx_t isdu;
    iolink_isdu_init(&isdu);
    isdu.params_ctx = &params2;
    isdu.ds_ctx = &ds2;
    g_fail_write = 1;
    const uint8_t request[] = {0x14U, 0x18U, 0x78U, 0x74U}; /* ApplicationTag "x" */
    for (size_t i = 0; i < sizeof(request); ++i) {
        iolink_isdu_od_write(&isdu, (i == 0U) ? IOLINK_FLOWCTRL_START : (uint8_t) i, &request[i],
                             1U);
    }
    iolink_isdu_process(&isdu);
    uint8_t response[4] = {0U};
    for (size_t i = 0; i < sizeof(response); ++i) {
        iolink_isdu_od_read(&isdu, (i == 0U) ? IOLINK_FLOWCTRL_START : (uint8_t) i, &response[i],
                            1U);
    }
    const uint8_t expected[] = {0x44U, 0x80U, 0x11U, 0xD5U};
    assert(memcmp(response, expected, sizeof(expected)) == 0);
    const iolink_result_t still_original =
        iolink_params_ctx_read(&params2, IOLINK_IDX_APPLICATION_TAG, 0U, result, sizeof(result));
    assert(still_original.code == 0);
    assert(still_original.len == sizeof(original) - 1U);
    assert(memcmp(result, original, sizeof(original) - 1U) == 0);

    puts("Parameter image: shared tag/app persistence, rollback and reload work");
    return 0;
}
