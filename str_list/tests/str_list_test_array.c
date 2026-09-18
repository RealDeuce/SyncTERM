/* Exercise the production array implementation with the same failing
 * allocator as string copies, without exporting test hooks in either API. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdlib.h>
#include "str_list_test_allocator.h"

#define malloc str_list_test_malloc
#define realloc str_list_test_realloc
#define free str_list_test_free
#include "../../array_list/array_list.c"
