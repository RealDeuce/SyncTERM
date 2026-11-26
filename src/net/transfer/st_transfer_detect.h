/* Incremental ZMODEM startup detector. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_TRANSFER_DETECT_H
#define ST_TRANSFER_DETECT_H
#include <stdint.h>

struct st_transfer_detector { unsigned matched; };

/* Observe one byte. Return 1 for ZRQINIT, 2 for ZRINIT, or 0 otherwise.
 * The terminal receives every observed byte as it arrives. */
int st_transfer_detect(struct st_transfer_detector *detector, uint8_t byte);
#endif
