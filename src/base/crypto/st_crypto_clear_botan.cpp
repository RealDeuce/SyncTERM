/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "st_crypto.h"

#include <botan/mem_ops.h>

extern "C" void
st_crypto_clear(void *data, size_t size)
{
	if (size != 0)
		Botan::secure_scrub_memory(data, size);
}
