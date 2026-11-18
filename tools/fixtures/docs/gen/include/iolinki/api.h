#ifndef IOLINK_FIXTURE_API_H
#define IOLINK_FIXTURE_API_H

#include <stdint.h>

#define IOLINK_FIXTURE_MAX 4

typedef struct
{
    void (*member_cb)(int value);
} iolink_fixture_ops_t;

typedef void (*iolink_fixture_fn_typedef)(int value);

/**
 * @brief 取一个值。
 * @param ops 操作表。
 * @return 当前值。
 */
int iolink_fixture_get(const iolink_fixture_ops_t* IOLINK_NONNULL ops);

/**
 * @brief 多行声明与可选参数的示例。
 *
 * @param ctx 上下文。
 * @param out 可选的输出缓冲。
 * @return 成功时为 0。
 */
int iolink_fixture_multiline(const iolink_fixture_ops_t* IOLINK_NONNULL ctx,
                             uint8_t* IOLINK_NULLABLE out,
                             uint32_t len);

/* 选择器返回一个函数指针。 */
void (*iolink_fixture_selector(void))(int value);

/**
 * @brief 内联查询（公开头文件中的实现也是 API）。
 * @param ops   操作表（必需）。
 * @param flags 可选标志。
 * @return 当前值。
 */
static inline int iolink_fixture_inline(const iolink_fixture_ops_t* IOLINK_NONNULL ops,
                                        uint32_t IOLINK_NULLABLE flags)
{
    (void) ops;
    return (int) flags;
}

#endif /* IOLINK_FIXTURE_API_H */
