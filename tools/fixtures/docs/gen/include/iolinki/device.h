#ifndef IOLINK_FIXTURE_DEVICE_H
#define IOLINK_FIXTURE_DEVICE_H

/**
 * @brief iolink_fixture_init() 返回的结果代码。
 *
 * 负值表示初始化被拒绝。
 */
typedef enum
{
    IOLINK_DEVICE_INIT_OK = 0,         /**< 初始化成功 */
    IOLINK_DEVICE_INIT_ERR_NULL = -1,  /**< ctx 或 config 为 NULL */
    IOLINK_DEVICE_INIT_ERR_BAD = -7,   /**< PD/OD 长度不兼容 */
} iolink_fixture_init_result_t;

/* 没有 IOLINK_DEVICE_* 取值的枚举不是错误码表。 */
typedef enum
{
    IOLINK_FIXTURE_MODE_A = 1,
} iolink_fixture_mode_t;

#define IOLINK_FIXTURE_MAX 4

#endif /* IOLINK_FIXTURE_DEVICE_H */
