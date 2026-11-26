/* Windows XUSB class driver input. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <windows.h>
#include <stdio.h>

#include <xinput.h>

#include "controller_internal.h"

void
pad_xinput_poll(struct st_controllers *hub, bool discover, st_controller_emit emit, void *context)
{
	for (DWORD slot = 0; slot < XUSER_MAX_COUNT; slot++) {
		struct st_pad *pad = hub->pads;
		while (pad && (pad->source != 3 || pad->removed || pad->port != slot))
			pad = pad->next;
		if (!pad && !discover)
			continue;
		XINPUT_STATE state;
		if (XInputGetState(slot, &state) != ERROR_SUCCESS) {
			if (pad)
				pad->removed = true;
			continue;
		}
		if (!pad) {
			char name[48];
			snprintf(name, sizeof name, "XInput gamepad %u", (unsigned)slot + 1);
			/* The public XInput API supplies neither USB IDs nor the Guide button. */
			pad = pad_xusb_attach(hub, 0, 0, name, false);
			if (!pad)
				continue;
			pad->port = (unsigned)slot;
		}
		const XINPUT_GAMEPAD *g        = &state.Gamepad;
		int                   sticks[] = {g->sThumbLX, g->sThumbLY, g->sThumbRX, g->sThumbRY};
		pad_xusb_state(pad, g->wButtons, g->bLeftTrigger, g->bRightTrigger, sticks, emit, context);
	}
}
