/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "xp_crypto.h"

#include <string.h>

void
xp_crypto_clear(void *data, size_t size)
{
	if (size != 0)
		memset_explicit(data, 0, size);
}
