/* Wired gamepad state layout from MS-XUSBI section 3.2.5.1.1. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>

#include "controller_internal.h"

static const struct {
	unsigned    mask, code;
	const char *name;
} buttons[] = {
    {0x0001, 544, "D-pad up"          },
    {0x0002, 545, "D-pad down"        },
    {0x0004, 546, "D-pad left"        },
    {0x0008, 547, "D-pad right"       },
    {0x0010, 315, "Start"             },
    {0x0020, 314, "Back"              },
    {0x0040, 317, "Left stick button" },
    {0x0080, 318, "Right stick button"},
    {0x0100, 310, "Left shoulder"     },
    {0x0200, 311, "Right shoulder"    },
    {0x1000, 304, "A"                 },
    {0x2000, 305, "B"                 },
    {0x4000, 308, "X"                 },
    {0x8000, 307, "Y"                 },
    {0x0400, 316, "Guide"             }
};

static const unsigned axes[] = {0, 1, 3, 4, 2, 5};
#define AXIS_COUNT (sizeof axes / sizeof axes[0])

struct st_pad *
pad_xusb_attach(struct st_controllers *hub, unsigned vendor, unsigned product, const char *name, bool guide)
{
	struct st_pad *pad = calloc(1, sizeof(*pad));
	if (!pad)
		return NULL;
	pad->source            = 3;
	pad->device_class      = 2;
	pad->count             = AXIS_COUNT + sizeof buttons / sizeof buttons[0] - (guide ? 0 : 1);
	pad->descriptor_fields = pad->count;
	snprintf(pad->name, sizeof pad->name, "%s", name && *name ? name : "XUSB gamepad");
	snprintf(pad->profile, sizeof pad->profile, "xusb-%04x-%04x-%zu.map", vendor, product, pad->count);
	pad->fields = calloc(pad->count, sizeof(*pad->fields));
	if (!pad->fields) {
		free(pad);
		return NULL;
	}
	for (size_t i = 0; i < pad->count; ++i) {
		struct pad_field *field = pad->fields + i;
		bool              axis  = i < AXIS_COUNT;
		field->target = axis ? pad_target(3, axes[i]) : pad_target(1, buttons[i - AXIS_COUNT].code);
		const struct st_control_target *target = st_controller_target((size_t)field->target);
		field->kind                            = target->kind;
		field->minimum                         = target->kind == ST_STICK ? -32768 : 0;
		field->maximum                         = target->kind == ST_STICK ? 32767 : axis ? 255 : 1;
		snprintf(field->label, sizeof field->label, "%s",
		    axis ? target->label : buttons[i - AXIS_COUNT].name);
	}
	if (pad_attach(hub, pad))
		return pad;
	free(pad->fields);
	free(pad);
	return NULL;
}

/* XUSB has positive Y upwards. Keep zero at center and reach both evdev endpoints. */
static int
down_positive(int value)
{
	if (value == -32768)
		return 32767;
	if (value == 32767)
		return -32768;
	return -value;
}

void
pad_xusb_state(struct st_pad *pad, unsigned pressed, unsigned left, unsigned right, const int sticks[4],
    st_controller_emit emit, void *context)
{
	pad_value(pad, 0, sticks[0]);
	pad_value(pad, 1, down_positive(sticks[1]));
	pad_value(pad, 2, sticks[2]);
	pad_value(pad, 3, down_positive(sticks[3]));
	pad_value(pad, 4, left);
	pad_value(pad, 5, right);
	for (size_t i = AXIS_COUNT; i < pad->count; ++i)
		pad_value(pad, i, (pressed & buttons[i - AXIS_COUNT].mask) != 0);
	pad_publish(pad, emit, context);
}

void
pad_xusb_report(struct st_pad *pad, const uint8_t *report, size_t size, st_controller_emit emit, void *context)
{
	if (size < 20 || report[0] != 0 || report[1] != 20)
		return;
	int sticks[4];
	for (unsigned i = 0; i < 4; i++) {
		int n     = report[6 + 2 * i] | (report[7 + 2 * i] << 8);
		sticks[i] = n >= 32768 ? n - 65536 : n;
	}
	pad_xusb_state(pad, report[2] | (report[3] << 8), report[4], report[5], sticks, emit, context);
}
