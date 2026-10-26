/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "xp_crypto.h"

void
xp_crypto_clear(void *data, size_t size)
{
	volatile unsigned char *bytes = data;

	while (size-- != 0)
		*bytes++ = 0;
}
