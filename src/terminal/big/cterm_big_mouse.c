/* Native BIG mouse observation, ownership and ordered pointer samples. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm.h"
#include "cterm_big_internal.h"
#include "cterm_render.h"

bool
cterm_big_pointer_active(const struct cterm_big *big)
{
	return big != NULL && big->collect.active && big->collect.pointer;
}

static void
enqueue(struct cterm_big *big, int32_t x, int32_t y, bool allowed)
{
	if (!cterm_big_pointer_active(big))
		return;
	const struct cterm_big_event event = {
	    .mouse       = true,
	    .data.sample = {.x = x,
			    .y             = y,
			    .buttons       = big->mouse.buttons,
			    .generation    = big->collect.generation,
			    .allowed       = allowed}
        };
	(void)cterm_big_input_push(big, &event);
}

bool
cterm_big_mouse(struct cterm_big *big, const struct st_console_event *event)
{
	int32_t x = event->data.mouse.pixel_x, y = event->data.mouse.pixel_y;
	big->mouse.buttons = event->data.mouse.buttons;
	if (x >= 0 && y >= 0) {
		big->mouse.known   = true;
		big->mouse.known_x = x;
		big->mouse.known_y = y;
	}
	bool owned   = cterm_big_pointer_active(big);
	bool allowed = event->data.mouse.action != ST_CONSOLE_MOUSE_WHEEL;
	enqueue(big, x, y, allowed);
	if (owned && !big->feeding)
		cterm_render_record_error(big->terminal, cterm_big_poll(big));
	return owned && allowed;
}

void
cterm_big_mouse_sync(struct cterm_big *big, uint32_t buttons)
{
	big->mouse.buttons = buttons;
	enqueue(big, -1, -1, false);
}
