/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/*
 * Compile-matrix override: enables the ISR print points for every library
 * translation unit, so the compile matrix builds that branch under -Werror.
 * Runtime output is still governed by each context's sink (silent by default).
 */

#ifndef IOLINK_TESTS_LOG_OVERRIDE_H
#define IOLINK_TESTS_LOG_OVERRIDE_H

#define IOLINK_LOG_ENABLE_ISR 1

#endif /* IOLINK_TESTS_LOG_OVERRIDE_H */
