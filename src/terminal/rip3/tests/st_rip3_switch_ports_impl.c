/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
/* Reproduce the adopted native probe's protected-but-undefined record only in
 * this fixture. Ordinary public port operations cannot create that state. */
#include "../st_rip3_ports.c"

int
st_rip3_switch_test_stale(struct st_rip3_ports *ports, unsigned number, bool protect)
{
	if (ports == NULL || number == 0 || number >= 36
	    || ports->entries[number].info.defined)
		return IO_ERROR_INVALID_ARGUMENT;
	ports->entries[number].info.protected = protect;
	return 0;
}
