/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include <assert.h>
#include <string.h>

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
                             const uint8_t* input, size_t length, uint8_t* output,
                             size_t* capacity)
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
    return 0x11U; /* Service not available (Table C.1). */
}

/* Feed one 16-bit indexed read (I-Service 0xB, Length 5) of `index`, subindex 0. */
static void read_index(iolink_isdu_ctx_t* ctx, uint16_t index)
{
    uint8_t request[5] = {0xB5U, 0x00U, 0x00U, 0x00U, 0x00U};
    request[1] = (uint8_t) (index >> 8);
    request[2] = (uint8_t) (index & 0xFFU);
    request[3] = 0x00U; /* Subindex 0. */
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

/* B.2.27: Preferred Indices 0x0040-0x00FE are device-specific and reach the
   application vendor service, exactly like the Extended Index range. */
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

    /* The vendor handler owns the whole device-specific range: with one
       registered, even 0x0040 reaches it (the built-in error statistics object
       serves only when no handler is registered). */
    read_index(&ctx, 0x0040U);
    assert(spy.calls == 2 && spy.last_index == 0x0040U);
    assert(ctx.response_len == 1 && ctx.response_buf[0] == 0x6A);

    /* 0x00FF is reserved and 0x003F is reserved for profiles: neither range
       may reach the vendor service, and the master gets 0x11. */
    read_index(&ctx, 0x00FFU);
    assert(spy.calls == 2);
    assert_negative_service_not_available(&ctx);
    read_index(&ctx, 0x003FU);
    assert(spy.calls == 2);
    assert_negative_service_not_available(&ctx);

    /* A Preferred Index the application rejects keeps the 0x11 error. */
    ctx.vendor_service = unavailable;
    read_index(&ctx, 0x0080U);
    assert_negative_service_not_available(&ctx);

    /* No handler: unknown device-specific indices answer 0x11 too. */
    ctx.vendor_service = NULL;
    read_index(&ctx, 0x0041U);
    assert_negative_service_not_available(&ctx);
}

static void truncated_requests_never_execute(void)
{
    const uint8_t requests[][4] = {
        {0x34, 0x01, 0x35, 0x00}, /* 16-bit write with missing subindex. */
        {0xB4, 0x01, 0xB5, 0x00}, /* 16-bit read with missing subindex. */
        {0x33, 0x01, 0x32, 0x00}, /* 16-bit write missing low index/subindex. */
        {0xB3, 0x01, 0xB2, 0x00},
        {0x23, 0x01, 0x22, 0x00}, /* 8-bit write missing subindex. */
        {0xA3, 0x01, 0xA2, 0x00},
        {0x12, 0x12, 0x00, 0x00}, /* 8-bit write missing index. */
        {0x92, 0x92, 0x00, 0x00},
        {0x32, 0x32, 0x00, 0x00}, /* 16-bit write missing both index bytes. */
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
        assert(!ctx.vendor_positive);
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
    /* 16-bit indexed read, subindex zero, independently XOR-checksummed. */
    const uint8_t request[] = {0xB5, 0x01, 0x00, 0x00, 0xB4};
    for (size_t i = 0; i < sizeof(request); i++) {
        iolink_isdu_od_write(&ctx, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i, request + i, 1);
    }
    iolink_isdu_process(&ctx);
    assert(calls == 1);
    assert(ctx.response_len == 2 && ctx.response_buf[0] == 0x80 && ctx.response_buf[1] == 0x34);
    uint8_t octet;
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &octet, 1);
    assert(octet == 0xD4); /* 0x80xx is legitimate data, not an error marker. */
    const uint8_t malformed[] = {0xB5, 0x01, 0x00, 0x00, 0x00};
    for (size_t i = 0; i < sizeof(malformed); i++) {
        iolink_isdu_od_write(&ctx, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i, malformed + i, 1);
    }
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &octet, 1);
    assert(octet == 0xC4); /* Bad CHKPDU after positive vendor data stays negative. */
    ctx.header.index = 0x0010;
    ctx.state = ISDU_STATE_SERVICE_EXECUTE;
    iolink_isdu_process(&ctx);
    assert(calls == 1); /* Mandatory indices cannot be overridden. */
    ctx.header.index = 0x0100;
    ctx.vendor_service = too_long;
    ctx.state = ISDU_STATE_SERVICE_EXECUTE;
    iolink_isdu_process(&ctx);
    assert(ctx.response_len == 2 && ctx.response_buf[0] == 0x80);
    assert(!ctx.vendor_positive);
    /* A missing callback retains the original unknown-index error. */
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
    assert(other_calls == 1 && calls == 1); /* Application ownership is per device. */
    return 0;
}
