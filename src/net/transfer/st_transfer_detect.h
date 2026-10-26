/* Incremental, CRC-validated ZMODEM startup detector. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_TRANSFER_DETECT_H
#define ST_TRANSFER_DETECT_H
#include <stdint.h>
#include <stddef.h>
struct st_transfer_detector { uint8_t bytes[32]; size_t size; };
/* Zero: output is ordinary terminal data. One: ZRQINIT (download). Two:
 * ZRINIT (upload). For either event output is the retained protocol prefix.
 * output must hold 33 bytes; matching prefixes produce no ordinary output. */
int st_transfer_detect(struct st_transfer_detector *detector, uint8_t byte,
	uint8_t *output, size_t *output_size);
#endif
