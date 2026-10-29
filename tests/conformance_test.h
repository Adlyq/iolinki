/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Test-only helpers for tagging unit assertions with IO-Link Test Spec IDs.
 */
#ifndef IOLINK_CONFORMANCE_TEST_H_
#define IOLINK_CONFORMANCE_TEST_H_

#include <stdio.h>

#define IOLINK_CONFORMANCE_CASE(id)                                                               \
    do {                                                                                          \
        (void) fprintf(stderr, "\n[conformance] case=%s test=%s:%d\n", (id), __FILE__, __LINE__); \
        (void) fflush(stderr);                                                                    \
    } while (0)

#endif /* IOLINK_CONFORMANCE_TEST_H_ */
