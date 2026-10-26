/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

int dynamic_library_fixture_value = 73;

int
dynamic_library_fixture_add(int left, int right)
{
	return left + right;
}
