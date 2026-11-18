/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_spec_constants.c
 * @brief `tests/spec_constants.csv` 与实现常量的双向一致性。
 *
 * 表格是规范条款到代码符号的映射，它只有在两个方向都被强制时才有意义：
 * CSV 里的每一行必须能在下面的 C 表里找到同名符号，C 表里的每一项也必须
 * 出现在 CSV 里，且两边的数值必须相等。新增或删除任一侧都会让本测试失败。
 *
 * CSV 路径由 `IOLINK_SPEC_CONSTANTS_CSV` 编译定义传入（见
 * `tests/CMakeLists.txt`），因此测试不会依赖进程的工作目录。
 */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iolinki/config.h"
#include "iolinki/device_info.h"
#include "iolinki/protocol.h"

#ifndef IOLINK_SPEC_CONSTANTS_CSV
#error "IOLINK_SPEC_CONSTANTS_CSV must name tests/spec_constants.csv"
#endif

#define CSV_FIELD_COUNT 4U
#define CSV_LINE_MAX 1024U

typedef struct
{
    const char* symbol;
    long long value;
} spec_entry_t;

#define SPEC_ENTRY(symbol_) {#symbol_, (long long) (symbol_)}

static const spec_entry_t k_spec[] = {
    SPEC_ENTRY(IOLINK_T_WU_US),
    SPEC_ENTRY(IOLINK_T_REN_US),
    SPEC_ENTRY(IOLINK_T_DSIO_MS),
    SPEC_ENTRY(IOLINK_EVENT_MIN_APPEAR_MS),
    SPEC_ENTRY(IOLINK_EVENT_MIN_REPEAT_MS),
    SPEC_ENTRY(IOLINK_M_SEQ_HEADER_LEN),
    SPEC_ENTRY(IOLINK_M_SEQ_TYPE0_LEN),
    SPEC_ENTRY(IOLINK_M_SEQ_MIN_LEN),
    SPEC_ENTRY(IOLINK_MC_RW_MASK),
    SPEC_ENTRY(IOLINK_MC_COMM_CHANNEL_MASK),
    SPEC_ENTRY(IOLINK_MC_ADDR_MASK),
    SPEC_ENTRY(IOLINK_MC_CHANNEL_PROCESS),
    SPEC_ENTRY(IOLINK_MC_CHANNEL_PAGE),
    SPEC_ENTRY(IOLINK_MC_CHANNEL_DIAGNOSIS),
    SPEC_ENTRY(IOLINK_MC_CHANNEL_ISDU),
    SPEC_ENTRY(IOLINK_MSEQ_TYPE_MASK),
    SPEC_ENTRY(IOLINK_MSEQ_TYPE_0),
    SPEC_ENTRY(IOLINK_MSEQ_TYPE_1),
    SPEC_ENTRY(IOLINK_MSEQ_TYPE_2),
    SPEC_ENTRY(IOLINK_MSEQ_TYPE_RESERVED),
    SPEC_ENTRY(IOLINK_MSEQ_CHECKSUM_MASK),
    SPEC_ENTRY(IOLINK_EVENT_BIT_STATUS),
    SPEC_ENTRY(IOLINK_FLOWCTRL_START),
    SPEC_ENTRY(IOLINK_FLOWCTRL_IDLE),
    SPEC_ENTRY(IOLINK_FLOWCTRL_IDLE2),
    SPEC_ENTRY(IOLINK_FLOWCTRL_ABORT),
    SPEC_ENTRY(IOLINK_FLOWCTRL_COUNT_MASK),
    SPEC_ENTRY(IOLINK_CMD_FALLBACK),
    SPEC_ENTRY(IOLINK_CMD_MASTER_IDENT),
    SPEC_ENTRY(IOLINK_CMD_DEVICE_IDENT),
    SPEC_ENTRY(IOLINK_CMD_DEVICE_STARTUP),
    SPEC_ENTRY(IOLINK_CMD_PROCESS_DATA_OPERATE),
    SPEC_ENTRY(IOLINK_CMD_DEVICE_OPERATE),
    SPEC_ENTRY(IOLINK_CMD_DEVICE_PREOPERATE),
    SPEC_ENTRY(IOLINK_ISDU_SERVICE_READ),
    SPEC_ENTRY(IOLINK_ISDU_SERVICE_WRITE),
    SPEC_ENTRY(IOLINK_IDX_DIRECT_PARAMETERS_1),
    SPEC_ENTRY(IOLINK_IDX_DIRECT_PARAMETERS_2),
    SPEC_ENTRY(IOLINK_IDX_SYSTEM_COMMAND),
    SPEC_ENTRY(IOLINK_IDX_DATA_STORAGE),
    SPEC_ENTRY(IOLINK_IDX_DEVICE_ACCESS_LOCKS),
    SPEC_ENTRY(IOLINK_IDX_PROFILE_CHARACTERISTIC),
    SPEC_ENTRY(IOLINK_IDX_VENDOR_NAME),
    SPEC_ENTRY(IOLINK_IDX_VENDOR_TEXT),
    SPEC_ENTRY(IOLINK_IDX_PRODUCT_NAME),
    SPEC_ENTRY(IOLINK_IDX_PRODUCT_ID),
    SPEC_ENTRY(IOLINK_IDX_PRODUCT_TEXT),
    SPEC_ENTRY(IOLINK_IDX_SERIAL_NUMBER),
    SPEC_ENTRY(IOLINK_IDX_HARDWARE_REVISION),
    SPEC_ENTRY(IOLINK_IDX_FIRMWARE_REVISION),
    SPEC_ENTRY(IOLINK_IDX_APPLICATION_TAG),
    SPEC_ENTRY(IOLINK_IDX_FUNCTION_TAG),
    SPEC_ENTRY(IOLINK_IDX_LOCATION_TAG),
    SPEC_ENTRY(IOLINK_IDX_PRODUCT_URI),
    SPEC_ENTRY(IOLINK_IDX_ERROR_COUNT),
    SPEC_ENTRY(IOLINK_IDX_DEVICE_STATUS),
    SPEC_ENTRY(IOLINK_IDX_DETAILED_DEVICE_STATUS),
    SPEC_ENTRY(IOLINK_IDX_PROCESS_DATA_INPUT),
    SPEC_ENTRY(IOLINK_IDX_PROCESS_DATA_OUTPUT),
    SPEC_ENTRY(IOLINK_IDX_OFFSET_TIME),
    SPEC_ENTRY(IOLINK_IDX_ERROR_STATS),
    SPEC_ENTRY(IOLINK_CMD_PARAM_UPLOAD_START),
    SPEC_ENTRY(IOLINK_CMD_PARAM_UPLOAD_END),
    SPEC_ENTRY(IOLINK_CMD_PARAM_DOWNLOAD_START),
    SPEC_ENTRY(IOLINK_CMD_PARAM_DOWNLOAD_END),
    SPEC_ENTRY(IOLINK_CMD_PARAM_DOWNLOAD_STORE),
    SPEC_ENTRY(IOLINK_CMD_PARAM_BREAK),
    SPEC_ENTRY(IOLINK_CMD_DEVICE_RESET),
    SPEC_ENTRY(IOLINK_CMD_APPLICATION_RESET),
    SPEC_ENTRY(IOLINK_CMD_RESTORE_FACTORY_SETTINGS),
    SPEC_ENTRY(IOLINK_CMD_BACK_TO_BOX),
    SPEC_ENTRY(IOLINK_LOCK_PARAM),
    SPEC_ENTRY(IOLINK_LOCK_DS),
    SPEC_ENTRY(IOLINK_LOCK_LOCAL_PARAM),
    SPEC_ENTRY(IOLINK_LOCK_LOCAL_UI),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_IDX_NOT_AVAIL),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_SERVICE_TEMP_NOT_AVAIL),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_LOCAL_CTRL),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_NOT_ACCESSIBLE),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_WRITE_PROTECTED),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_PAR_VAL_GT_LIM),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_PAR_VAL_LT_LIM),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_PARAM_SET_INVALID),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_PARAM_INCONSISTENT),
    SPEC_ENTRY(IOLINK_ISDU_ERROR_APP_DEV_NOT_READY),
    SPEC_ENTRY(IOLINK_DEVICE_STATUS_OK),
    SPEC_ENTRY(IOLINK_DEVICE_STATUS_MAINTENANCE),
    SPEC_ENTRY(IOLINK_DEVICE_STATUS_OUT_OF_SPEC),
    SPEC_ENTRY(IOLINK_DEVICE_STATUS_FUNCTIONAL_CHECK),
    SPEC_ENTRY(IOLINK_DEVICE_STATUS_FAILURE),
    SPEC_ENTRY(IOLINK_REVISION_ID_1_0),
    SPEC_ENTRY(IOLINK_REVISION_ID_1_1),
};

#define SPEC_COUNT (sizeof(k_spec) / sizeof(k_spec[0]))

/** @brief 去掉首尾空白（就地修改并返回新起点）。 */
static char* trim(char* text)
{
    while ((*text != '\0') && (isspace((unsigned char) *text) != 0)) {
        text++;
    }
    char* end = text + strlen(text);
    while ((end > text) && (isspace((unsigned char) end[-1]) != 0)) {
        end--;
        *end = '\0';
    }
    return text;
}

/**
 * @brief 把一行拆成至多 @ref CSV_FIELD_COUNT 个字段。
 *
 * 支持 RFC 4180 子集：双引号包裹、`""` 表示一个字面双引号。结果就地写在
 * @p line 上（写入位置永不超过读取位置），字段之间以 NUL 分隔。
 *
 * @return 字段数；引号未闭合或字段过多时返回 0。
 */
static size_t split_csv(char* line, char* fields[CSV_FIELD_COUNT])
{
    size_t count = 0U;
    char* write = line;
    char* field = line;
    bool in_quotes = false;

    for (char* read = line;; read++) {
        const char c = *read;
        if (in_quotes) {
            if (c == '"') {
                if (read[1] == '"') {
                    *write++ = '"';
                    read++;
                }
                else {
                    in_quotes = false;
                }
            }
            else if (c == '\0') {
                return 0U;
            }
            else {
                *write++ = c;
            }
        }
        else if (c == '"') {
            in_quotes = true;
        }
        else if ((c == ',') || (c == '\0')) {
            if (count >= CSV_FIELD_COUNT) {
                return 0U;
            }
            *write++ = '\0';
            fields[count] = field;
            count++;
            field = write;
            if (c == '\0') {
                break;
            }
        }
        else {
            *write++ = c;
        }
    }
    return count;
}

/** @brief 解析 C 风格整数：十进制或 `0x` 十六进制，允许 `u`/`l` 后缀。 */
static bool parse_expected(const char* text, long long* value)
{
    char buffer[32];
    const size_t length = strlen(text);
    if ((length == 0U) || (length >= sizeof(buffer))) {
        return false;
    }
    (void) memcpy(buffer, text, length + 1U);

    size_t end = length;
    while ((end > 0U) && ((buffer[end - 1U] == 'u') || (buffer[end - 1U] == 'U') ||
                          (buffer[end - 1U] == 'l') || (buffer[end - 1U] == 'L'))) {
        end--;
    }
    buffer[end] = '\0';

    errno = 0;
    char* stop = NULL;
    const long long parsed = strtoll(buffer, &stop, 0);
    if ((errno != 0) || (stop == buffer) || (*stop != '\0')) {
        return false;
    }
    *value = parsed;
    return true;
}

static int find_entry(const char* symbol)
{
    for (size_t i = 0U; i < SPEC_COUNT; i++) {
        if (strcmp(k_spec[i].symbol, symbol) == 0) {
            return (int) i;
        }
    }
    return -1;
}

int main(void)
{
    FILE* file = fopen(IOLINK_SPEC_CONSTANTS_CSV, "r");
    if (file == NULL) {
        (void) fprintf(stderr, "FAIL: cannot open %s\n", IOLINK_SPEC_CONSTANTS_CSV);
        return 1;
    }

    bool ok = true;
    bool have_header = false;
    size_t rows = 0U;
    unsigned long line_no = 0UL;
    bool csv_seen[SPEC_COUNT] = {false};
    char line[CSV_LINE_MAX];

    while (fgets(line, (int) sizeof(line), file) != NULL) {
        line_no++;
        const size_t length = strlen(line);
        if ((length > 0U) && (line[length - 1U] != '\n') && (feof(file) == 0)) {
            (void) fprintf(stderr, "FAIL: %s:%lu: line longer than %u bytes\n",
                           IOLINK_SPEC_CONSTANTS_CSV, line_no, (unsigned) sizeof(line));
            ok = false;
            break;
        }
        if ((length > 0U) && (line[length - 1U] == '\n')) {
            line[length - 1U] = '\0';
        }

        char* text = trim(line);
        if ((*text == '\0') || (*text == '#')) {
            continue;
        }

        char* fields[CSV_FIELD_COUNT];
        const size_t count = split_csv(text, fields);
        if (count != CSV_FIELD_COUNT) {
            (void) fprintf(stderr, "FAIL: %s:%lu: expected %u CSV fields, got %u\n",
                           IOLINK_SPEC_CONSTANTS_CSV, line_no, (unsigned) CSV_FIELD_COUNT,
                           (unsigned) count);
            ok = false;
            continue;
        }
        for (size_t i = 0U; i < CSV_FIELD_COUNT; i++) {
            fields[i] = trim(fields[i]);
        }

        if (!have_header) {
            if ((strcmp(fields[0], "clause") != 0) || (strcmp(fields[1], "symbol") != 0) ||
                (strcmp(fields[2], "expected") != 0) || (strcmp(fields[3], "note") != 0)) {
                (void) fprintf(stderr,
                               "FAIL: %s:%lu: header must be "
                               "clause,symbol,expected,note\n",
                               IOLINK_SPEC_CONSTANTS_CSV, line_no);
                ok = false;
            }
            have_header = true;
            continue;
        }

        const char* clause = fields[0];
        const char* symbol = fields[1];
        const char* expected_text = fields[2];
        const char* note = fields[3];
        if ((*clause == '\0') || (*symbol == '\0') || (*expected_text == '\0') || (*note == '\0')) {
            (void) fprintf(stderr, "FAIL: %s:%lu: empty CSV field\n", IOLINK_SPEC_CONSTANTS_CSV,
                           line_no);
            ok = false;
            continue;
        }

        long long expected = 0;
        if (!parse_expected(expected_text, &expected)) {
            (void) fprintf(stderr, "FAIL: %s:%lu: %s has invalid expected value '%s'\n",
                           IOLINK_SPEC_CONSTANTS_CSV, line_no, symbol, expected_text);
            ok = false;
            continue;
        }

        const int index = find_entry(symbol);
        if (index < 0) {
            (void) fprintf(stderr, "FAIL: %s:%lu: %s (%s) is in the CSV but not in the C table\n",
                           IOLINK_SPEC_CONSTANTS_CSV, line_no, symbol, clause);
            ok = false;
            continue;
        }
        if (csv_seen[index]) {
            (void) fprintf(stderr, "FAIL: %s:%lu: duplicate symbol %s\n", IOLINK_SPEC_CONSTANTS_CSV,
                           line_no, symbol);
            ok = false;
            continue;
        }
        csv_seen[index] = true;
        rows++;

        if (k_spec[index].value != expected) {
            (void) fprintf(stderr,
                           "FAIL: %s: %s expected %lld but the implementation has %lld "
                           "(clause %s; %s)\n",
                           IOLINK_SPEC_CONSTANTS_CSV, symbol, expected, k_spec[index].value, clause,
                           note);
            ok = false;
        }
    }

    (void) fclose(file);

    if (!have_header) {
        (void) fprintf(stderr, "FAIL: %s has no CSV header\n", IOLINK_SPEC_CONSTANTS_CSV);
        ok = false;
    }
    if (rows == 0U) {
        (void) fprintf(stderr, "FAIL: %s has no data rows\n", IOLINK_SPEC_CONSTANTS_CSV);
        ok = false;
    }
    for (size_t i = 0U; i < SPEC_COUNT; i++) {
        if (!csv_seen[i]) {
            (void) fprintf(stderr, "FAIL: C table symbol %s is missing from %s\n", k_spec[i].symbol,
                           IOLINK_SPEC_CONSTANTS_CSV);
            ok = false;
        }
        for (size_t j = i + 1U; j < SPEC_COUNT; j++) {
            if (strcmp(k_spec[i].symbol, k_spec[j].symbol) == 0) {
                (void) fprintf(stderr, "FAIL: duplicate C table symbol %s\n", k_spec[i].symbol);
                ok = false;
            }
        }
    }

    if (!ok) {
        return 1;
    }
    (void) printf("PASS: %zu spec constants match %s\n", rows, IOLINK_SPEC_CONSTANTS_CSV);
    return 0;
}
