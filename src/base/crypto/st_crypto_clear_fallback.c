/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_crypto.h"

void
st_crypto_clear(void *data, size_t size)
{
	volatile unsigned char *bytes = data;

	while (size-- != 0)
		*bytes++ = 0;
}
