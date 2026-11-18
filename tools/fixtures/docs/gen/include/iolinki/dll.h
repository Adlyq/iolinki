#ifndef IOLINK_FIXTURE_DLL_H
#define IOLINK_FIXTURE_DLL_H

#include <stdint.h>

/**
 * @brief DLL 统计快照
 */
typedef struct
{
    uint32_t fixture_count; /**< 累计计数 */
    uint32_t fixture_multi; /**< 跨行的
                                 含义 */
} iolink_dll_stats_t;

#endif /* IOLINK_FIXTURE_DLL_H */
