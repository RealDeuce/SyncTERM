/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include <string.h>
#include <strings.h>

#include "st_crypto.h"

void
st_crypto_clear(void *data, size_t size)
{
	if (size != 0)
		explicit_bzero(data, size);
}
