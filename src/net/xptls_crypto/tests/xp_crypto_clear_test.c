/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "xp_crypto.h"

#include <stdint.h>

int
main(void)
{
	uint8_t bytes[32];
	size_t index;

	for (index = 0; index < sizeof(bytes); index++)
		bytes[index] = (uint8_t)(index + 1);
	xp_crypto_clear(bytes, sizeof(bytes));
	for (index = 0; index < sizeof(bytes); index++) {
		if (bytes[index] != 0)
			return 1;
	}
	return 0;
}
