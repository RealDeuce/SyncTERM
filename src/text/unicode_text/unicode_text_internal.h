/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause */

#ifndef ST_UNICODE_TEXT_INTERNAL_H_
#define ST_UNICODE_TEXT_INTERNAL_H_

#include "st_unicode_text.h"

#include <stdlib.h>

/* Start another normalization pass while retaining all buffer capacities. */
void st_unicode_text_rewind(st_unicode_text_t *text);

/* Called only for empty buffers. A failed shrink can release the allocation. */
static inline void *
trim_empty_buffer(void *values, size_t *capacity, size_t element_size)
{
	const size_t keep = 2048 / element_size;
	if (*capacity <= keep)
		return values;
	void *smaller = realloc(values, keep * element_size);
	if (smaller == NULL)
		free(values);
	*capacity = smaller == NULL ? 0 : keep;
	return smaller;
}

#endif
