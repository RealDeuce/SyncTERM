/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
static int calls;
int dependency(void);
int first_main(void)
{
	return CASE_VALUE == 1 && ++calls == 1 && dependency() == 7 ? 0 : 1;
}
