/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ST_CONSOLE_TEXT_LAYOUT_H_
#define ST_CONSOLE_TEXT_LAYOUT_H_

#include "st_console.h"

static inline struct st_console_rect
st_console_text_viewport(uint32_t columns, uint32_t rows,
	uint32_t host_columns, uint32_t host_rows, bool left_justify)
{
	struct st_console_rect view = {0, 0, columns, rows};

	if (host_columns != 0) {
		if (host_columns < columns)
			view.width = host_columns;
		else if (!left_justify)
			view.x = (host_columns - columns) / 2;
	}
	if (host_rows != 0) {
		if (host_rows < rows)
			view.height = host_rows;
		else
			view.y = (host_rows - rows) / 2;
	}
	return view;
}

#endif
