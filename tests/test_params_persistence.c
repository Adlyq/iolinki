/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/params.h"
#include "iolinki/port.h"
#include "iolinki/protocol.h"
#include "iolinki/isdu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "conformance_test.h"

static int write_result, writes;
static int mock_read(void* user, uint32_t offset, uint8_t* data, size_t len)
{
    (void) user;
    (void) offset;
    (void) data;
    (void) len;
    return -1;
}
static int mock_write(void* user, uint32_t offset, const uint8_t* data, size_t len)
{
    (void) user;
    (void) offset;
    (void) data;
    (void) len;
    ++writes;
    return write_result;
}
static const iolink_storage_api_t mock_storage = {
    .user = NULL, .read = mock_read, .write = mock_write};
int main(void)
{
    iolink_device_info_ctx_t info;
    iolink_params_ctx_t params;
    iolink_device_info_ctx_init(&info, NULL);
    iolink_params_ctx_init(&params, &info);
    iolink_params_bind_storage(&params, &mock_storage);
    const uint16_t indices[] = {IOLINK_IDX_APPLICATION_TAG, IOLINK_IDX_FUNCTION_TAG,
                                IOLINK_IDX_LOCATION_TAG};
    for (unsigned i = 0; i < 3; ++i) {
        const uint8_t original[] = "original";
        const uint8_t replacement[] = "replacement";
        uint8_t result[32];
        write_result = 0;
        assert(iolink_params_ctx_write(&params, indices[i], 0, original, 8, IOLINK_PERSIST_SYNC)
                   .code == 0);
        int prior_writes = writes;
        write_result = -1;
        /* 旧整型包装把 NVM 失败压成 -1；结果模型返回内部错误
         * IOLINK_RESULT_IO，调用方仍可区分失败原因。 */
        assert(iolink_params_ctx_write(&params, indices[i], 0, replacement, 11, IOLINK_PERSIST_SYNC)
                   .code == IOLINK_RESULT_IO);
        assert(writes == prior_writes + 1);
        const iolink_result_t read_failed =
            iolink_params_ctx_read(&params, indices[i], 0, result, sizeof(result));
        assert(read_failed.code == 0);
        assert(read_failed.len == 8);
        assert(memcmp(result, original, 8) == 0);
        assert(iolink_params_ctx_write(&params, indices[i], 0, replacement, 11, IOLINK_PERSIST_NONE)
                   .code == 0);
        assert(writes == prior_writes + 1);
        const iolink_result_t read_volatile =
            iolink_params_ctx_read(&params, indices[i], 0, result, sizeof(result));
        assert(read_volatile.code == 0);
        assert(read_volatile.len == 11);
        assert(memcmp(result, replacement, 11) == 0);
    }
    /* 当持久化标签存储拒绝写入时，真实字节传输必须产生 Write Response (-)，
     * 而不是旧的 52 52 肯定应答。 */
    const uint8_t requests[3][4] = {
        {0x14U, 0x18U, 0x78U, 0x74U}, /* ApplicationTag "x" */
        {0x14U, 0x19U, 0x78U, 0x75U}, /* FunctionTag "x" */
        {0x14U, 0x1AU, 0x78U, 0x76U}, /* LocationTag "x" */
    };
    const uint8_t expected[] = {0x44U, 0x80U, 0x11U, 0xD5U};
    for (unsigned tag = 0; tag < 3; ++tag) {
        iolink_isdu_ctx_t isdu;
        iolink_isdu_init(&isdu);
        isdu.params_ctx = &params;
        write_result = -1;
        int prior_writes = writes;
        for (unsigned i = 0; i < 4; ++i) {
            iolink_isdu_od_write(&isdu, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i,
                                 &requests[tag][i], 1);
        }
        iolink_isdu_process(&isdu);
        uint8_t response[4];
        for (unsigned i = 0; i < sizeof(response); ++i) {
            iolink_isdu_od_read(&isdu, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i, &response[i],
                                1);
        }
        assert(writes == prior_writes + 1);
        assert(memcmp(response, expected, sizeof(expected)) == 0);
        uint8_t retained[32];
        const iolink_result_t read_retained =
            iolink_params_ctx_read(&params, indices[tag], 0, retained, sizeof(retained));
        assert(read_retained.code == 0);
        assert(read_retained.len == 11);
        assert(memcmp(retained, "replacement", 11) == 0);
    }
    puts("Persistent tag failures preserve RAM/device info; volatile writes still work");
}
