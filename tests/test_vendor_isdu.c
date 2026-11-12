/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include <assert.h>
#include <string.h>
#include "conformance_test.h"

static uint8_t parameter(void* user, uint16_t index, uint8_t subindex, bool write,
                         const uint8_t* input, size_t length, uint8_t* output, size_t* capacity)
{
    unsigned* calls = user;
    (*calls)++;
    assert(index == 0x0100 && subindex == 0 && !write && length == 0);
    (void) input;
    assert(*capacity >= 2);
    output[0] = 0x80;
    output[1] = 0x34;
    *capacity = 2;
    return 0;
}

static uint8_t too_long(void* user, uint16_t index, uint8_t subindex, bool write,
                        const uint8_t* input, size_t length, uint8_t* output, size_t* capacity)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) write;
    (void) input;
    (void) length;
    (void) output;
    *capacity = 999;
    return 0;
}

static uint8_t count_only(void* user, uint16_t index, uint8_t subindex, bool write,
                          const uint8_t* input, size_t length, uint8_t* output, size_t* capacity)
{
    (void) index;
    (void) subindex;
    (void) write;
    (void) input;
    (void) length;
    (void) output;
    (*(unsigned*) user)++;
    *capacity = 0;
    return 0;
}

typedef struct
{
    unsigned calls;
    uint16_t last_index;
} preferred_spy_t;

static uint8_t preferred_spy(void* user, uint16_t index, uint8_t subindex, bool write,
                             const uint8_t* input, size_t length, uint8_t* output, size_t* capacity)
{
    preferred_spy_t* spy = user;
    spy->calls++;
    spy->last_index = index;
    assert(subindex == 0 && !write && length == 0);
    (void) input;
    assert(*capacity >= 1);
    output[0] = 0x6A;
    *capacity = 1;
    return 0;
}

static uint8_t unavailable(void* user, uint16_t index, uint8_t subindex, bool write,
                           const uint8_t* input, size_t length, uint8_t* output, size_t* capacity)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) write;
    (void) input;
    (void) length;
    (void) output;
    (void) capacity;
    return 0x11U; /* 服务不可用（Table C.1）。 */
}

/* 送入一个针对 `index`、子索引 0 的 16 位索引读（I-Service 0xB，Length 5）。 */
static void read_index(iolink_isdu_ctx_t* ctx, uint16_t index)
{
    uint8_t request[5] = {0xB5U, 0x00U, 0x00U, 0x00U, 0x00U};
    request[1] = (uint8_t) (index >> 8);
    request[2] = (uint8_t) (index & 0xFFU);
    request[3] = 0x00U; /* 子索引 0。 */
    request[4] = (uint8_t) (request[0] ^ request[1] ^ request[2] ^ request[3]);
    for (size_t i = 0; i < sizeof(request); i++) {
        iolink_isdu_od_write(ctx, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i, request + i, 1);
    }
    iolink_isdu_process(ctx);
}

static void assert_negative_service_not_available(const iolink_isdu_ctx_t* ctx)
{
    assert(ctx->response_len == 2 && ctx->response_buf[0] == 0x80);
    assert(ctx->response_buf[1] == 0x11);
}

/* B.2.27：Preferred Indices 0x0040-0x00FE 属于设备专用范围并到达
   应用厂商服务，与 Extended Index 范围完全一样。 */
static void preferred_index_routing(void)
{
    iolink_isdu_ctx_t ctx;
    preferred_spy_t spy = {0U, 0U};
    iolink_isdu_init(&ctx);
    ctx.vendor_service = preferred_spy;
    ctx.vendor_user = &spy;

    read_index(&ctx, 0x0080U);
    assert(spy.calls == 1 && spy.last_index == 0x0080U);
    assert(ctx.response_len == 1 && ctx.response_buf[0] == 0x6A);

    /* 厂商处理器拥有整个设备专用范围：一旦注册了处理器，
       连 0x0040 也会到达它（内置的错误统计对象
       仅在没有注册处理器时才提供服务）。 */
    read_index(&ctx, 0x0040U);
    assert(spy.calls == 2 && spy.last_index == 0x0040U);
    assert(ctx.response_len == 1 && ctx.response_buf[0] == 0x6A);

    /* 0x00FF 为保留值，0x003F 为配置文件保留：两者都
       不得到达厂商服务，主站得到 0x11。 */
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0057");
    read_index(&ctx, 0x00FFU);
    assert(spy.calls == 2);
    assert_negative_service_not_available(&ctx);
    read_index(&ctx, 0x003FU);
    assert(spy.calls == 2);
    assert_negative_service_not_available(&ctx);

    /* 应用拒绝的 Preferred Index 保持 0x11 错误。 */
    ctx.vendor_service = unavailable;
    read_index(&ctx, 0x0080U);
    assert_negative_service_not_available(&ctx);

    /* 无处理器：未知的设备专用索引同样应答 0x11。 */
    ctx.vendor_service = NULL;
    read_index(&ctx, 0x0041U);
    assert_negative_service_not_available(&ctx);
}

static void truncated_requests_never_execute(void)
{
    const uint8_t requests[][4] = {
        {0x34, 0x01, 0x35, 0x00},                           /* 缺少子索引的 16 位写。 */
        {0xB4, 0x01, 0xB5, 0x00},                           /* 缺少子索引的 16 位读。 */
        {0x33, 0x01, 0x32, 0x00},                           /* 缺少低索引字节/子索引的 16 位写。 */
        {0xB3, 0x01, 0xB2, 0x00}, {0x23, 0x01, 0x22, 0x00}, /* 缺少子索引的 8 位写。 */
        {0xA3, 0x01, 0xA2, 0x00}, {0x12, 0x12, 0x00, 0x00}, /* 缺少索引的 8 位写。 */
        {0x92, 0x92, 0x00, 0x00}, {0x32, 0x32, 0x00, 0x00}, /* 两个索引字节都缺失的 16 位写。 */
        {0xB2, 0xB2, 0x00, 0x00},
    };
    for (size_t vector = 0; vector < sizeof(requests) / sizeof(requests[0]); vector++) {
        iolink_isdu_ctx_t ctx;
        unsigned calls = 0;
        iolink_isdu_init(&ctx);
        ctx.vendor_service = count_only;
        ctx.vendor_user = &calls;
        size_t length = requests[vector][0] & 0x0FU;
        for (size_t i = 0; i < length; i++) {
            iolink_isdu_od_write(&ctx, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i,
                                 requests[vector] + i, 1);
        }
        iolink_isdu_process(&ctx);
        assert(calls == 0);
        assert(ctx.response_len == 2 && ctx.response_buf[0] == 0x80);
        assert(ctx.response_negative);
    }
}

int main(void)
{
    truncated_requests_never_execute();
    preferred_index_routing();
    iolink_isdu_ctx_t ctx;
    unsigned calls = 0;
    iolink_isdu_init(&ctx);
    ctx.vendor_service = parameter;
    ctx.vendor_user = &calls;
    /* 16 位索引读，子索引为零，独立按 XOR 计算校验和。 */
    const uint8_t request[] = {0xB5, 0x01, 0x00, 0x00, 0xB4};
    for (size_t i = 0; i < sizeof(request); i++) {
        iolink_isdu_od_write(&ctx, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i, request + i, 1);
    }
    iolink_isdu_process(&ctx);
    assert(calls == 1);
    assert(ctx.response_len == 2 && ctx.response_buf[0] == 0x80 && ctx.response_buf[1] == 0x34);
    uint8_t octet;
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &octet, 1);
    assert(octet == 0xD4); /* 0x80xx 是合法数据，不是错误标记。 */
    /* CHKPDU 不匹配由 test_isdu_wire.c 中的 SDCI_TC_0065 覆盖。 */
    ctx.header.index = 0x0010;
    ctx.state = ISDU_STATE_SERVICE_EXECUTE;
    iolink_isdu_process(&ctx);
    assert(calls == 1); /* 强制索引不可被覆盖。 */
    ctx.header.index = 0x0100;
    ctx.vendor_service = too_long;
    ctx.state = ISDU_STATE_SERVICE_EXECUTE;
    iolink_isdu_process(&ctx);
    assert(ctx.response_len == 2 && ctx.response_buf[0] == 0x80);
    assert(ctx.response_negative);
    /* 缺少回调时保留原有的未知索引错误。 */
    ctx.vendor_service = NULL;
    ctx.state = ISDU_STATE_SERVICE_EXECUTE;
    iolink_isdu_process(&ctx);
    assert(ctx.response_len == 2 && ctx.response_buf[0] == 0x80);
    iolink_isdu_ctx_t other;
    unsigned other_calls = 0;
    iolink_isdu_init(&other);
    other.vendor_service = parameter;
    other.vendor_user = &other_calls;
    for (size_t i = 0; i < sizeof(request); i++) {
        iolink_isdu_od_write(&other, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i, request + i, 1);
    }
    iolink_isdu_process(&other);
    assert(other_calls == 1 && calls == 1); /* 应用所有权是按设备划分的。 */
    return 0;
}
