/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/* REMOVED (upstream original, deleted during the IO-Link conformance audit):
 * the standalone frame encode/decode helpers (src/frame.c) were unused dead code
 * — no production caller — and encoded the pre-Figure-A.14 layout
 * ([PD-in][OD]CKS response, OD always present in the request), contradicting the
 * corrected DLL. Use the DLL implementation instead: dll.c builds the
 * [OD][PD-in]CKS device reply and derives OD presence per frame from the MC R/W
 * bit (Figure A.14). This header is kept as a tombstone only; there is no
 * implementation.
 */

#ifndef IOLINK_FRAME_H
#define IOLINK_FRAME_H

#endif /* IOLINK_FRAME_H */
