/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

extern int dynamic_library_provider_value(void);

int
dynamic_library_consumer_value(void)
{
	return dynamic_library_provider_value();
}
