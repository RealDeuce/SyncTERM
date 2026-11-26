/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller_internal.h"
#include "path.h"
#include "st_time.h"

static const struct st_control_target targets[] = {
    {"Bottom face button",     "BTN_SOUTH",      ST_BUTTON,  304},
    {"Right face button",      "BTN_EAST",       ST_BUTTON,  305},
    {"Top face button",        "BTN_NORTH",      ST_BUTTON,  307},
    {"Left face button",       "BTN_WEST",       ST_BUTTON,  308},
    {"C button",               "BTN_C",          ST_BUTTON,  306},
    {"Z button",               "BTN_Z",          ST_BUTTON,  309},
    {"Left shoulder",          "BTN_TL",         ST_BUTTON,  310},
    {"Right shoulder",         "BTN_TR",         ST_BUTTON,  311},
    {"Left trigger button",    "BTN_TL2",        ST_BUTTON,  312},
    {"Right trigger button",   "BTN_TR2",        ST_BUTTON,  313},
    {"Select / Back",          "BTN_SELECT",     ST_BUTTON,  314},
    {"Start",                  "BTN_START",      ST_BUTTON,  315},
    {"Guide / Mode",           "BTN_MODE",       ST_BUTTON,  316},
    {"Left stick button",      "BTN_THUMBL",     ST_BUTTON,  317},
    {"Right stick button",     "BTN_THUMBR",     ST_BUTTON,  318},
    {"D-pad up",               "BTN_DPAD_UP",    ST_BUTTON,  544},
    {"D-pad down",             "BTN_DPAD_DOWN",  ST_BUTTON,  545},
    {"D-pad left",             "BTN_DPAD_LEFT",  ST_BUTTON,  546},
    {"D-pad right",            "BTN_DPAD_RIGHT", ST_BUTTON,  547},
    {"Joystick trigger",       "BTN_TRIGGER",    ST_BUTTON,  288},
    {"Thumb button",           "BTN_THUMB",      ST_BUTTON,  289},
    {"Second thumb button",    "BTN_THUMB2",     ST_BUTTON,  290},
    {"Top button",             "BTN_TOP",        ST_BUTTON,  291},
    {"Second top button",      "BTN_TOP2",       ST_BUTTON,  292},
    {"Pinky button",           "BTN_PINKIE",     ST_BUTTON,  293},
    {"Base button 1",          "BTN_BASE",       ST_BUTTON,  294},
    {"Base button 2",          "BTN_BASE2",      ST_BUTTON,  295},
    {"Base button 3",          "BTN_BASE3",      ST_BUTTON,  296},
    {"Base button 4",          "BTN_BASE4",      ST_BUTTON,  297},
    {"Base button 5",          "BTN_BASE5",      ST_BUTTON,  298},
    {"Base button 6",          "BTN_BASE6",      ST_BUTTON,  299},
    {"Left stick horizontal",  "ABS_X",          ST_STICK,   0  },
    {"Left stick vertical",    "ABS_Y",          ST_STICK,   1  },
    {"Right stick horizontal", "ABS_RX",         ST_STICK,   3  },
    {"Right stick vertical",   "ABS_RY",         ST_STICK,   4  },
    {"Left trigger",           "ABS_Z",          ST_TRIGGER, 2  },
    {"Right trigger",          "ABS_RZ",         ST_TRIGGER, 5  },
    {"Throttle",               "ABS_THROTTLE",   ST_TRIGGER, 6  },
    {"Rudder",                 "ABS_RUDDER",     ST_STICK,   7  },
    {"Wheel",                  "ABS_WHEEL",      ST_STICK,   8  },
    {"Accelerator",            "ABS_GAS",        ST_TRIGGER, 9  },
    {"Brake",                  "ABS_BRAKE",      ST_TRIGGER, 10 },
    {"Hat 1",                  "ABS_HAT0X/Y",    ST_HAT,     16 },
    {"Hat 2",                  "ABS_HAT1X/Y",    ST_HAT,     18 },
    {"Hat 3",                  "ABS_HAT2X/Y",    ST_HAT,     20 },
    {"Hat 4",                  "ABS_HAT3X/Y",    ST_HAT,     22 },
};
#define TARGET_COUNT (sizeof(targets) / sizeof(targets[0]))

size_t
st_controller_target_count(void)
{
	return TARGET_COUNT;
}

const struct st_control_target *
st_controller_target(size_t index)
{
	return index < TARGET_COUNT ? targets + index : NULL;
}

int
pad_target(unsigned type, unsigned code)
{
	for (size_t i = 0; i < TARGET_COUNT; i++)
		if ((targets[i].kind == ST_BUTTON ? 1u : 3u) == type && targets[i].code == code)
			return (int)i;
	return -1;
}

int
pad_scale(const struct pad_field *f, enum st_control_kind kind)
{
	int64_t low = f->minimum, high = f->maximum;
	int64_t value = f->raw < low ? low : f->raw > high ? high : f->raw;
	if (kind == ST_BUTTON)
		return value != low;
	if (high <= low)
		return 0;
	if (kind == ST_TRIGGER) {
		int64_t pos = f->inverted ? high - value : value - low;
		return (int)((pos * 32767 + (high - low) / 2) / (high - low));
	}
	int64_t mid = low + (high - low + 1) / 2;
	if (value == mid)
		return 0;
	bool    negative = (value < mid) != f->inverted;
	int64_t distance = value < mid ? mid - value : value - mid;
	int64_t span     = value < mid ? mid - low : high - mid;
	int     scaled   = (int)((distance * (negative ? 32768 : 32767) + span / 2) / span);
	return negative ? -scaled : scaled;
}

static int
hat_direction(const struct pad_field *f)
{
	int64_t n = f->raw - f->minimum, count = f->maximum - f->minimum + 1;
	if (n < 0 || n >= count || (count != 4 && count != 8))
		return -1;
	return (int)(count == 4 ? n * 2 : n);
}

void
pad_value(struct st_pad *pad, size_t index, int64_t raw)
{
	struct pad_field *f = &pad->fields[index];
	if (f->known && raw == f->raw)
		return;
	pad->dirty = true;
	bool known = f->known;
	f->raw     = raw;
	f->known   = true;
	if (!known) {
		f->baseline = raw;
		f->armed    = true;
		return;
	}
	if (!f->armed || pad->actuation >= 0)
		return;
	bool active = false;
	switch (f->kind) {
		case ST_BUTTON:
			active = raw != f->minimum;
			break;
		case ST_HAT:
			active = hat_direction(f) >= 0;
			break;
		default:
		{
			int64_t threshold = (f->maximum - f->minimum + 7) / 8;
			int64_t rest      = f->minimum + (f->maximum - f->minimum + 1) / 2;
			if (f->target >= 0 && targets[f->target].kind == ST_TRIGGER)
				rest = f->inverted ? f->maximum : f->minimum;
			/* Releasing an already tilted stick toward center is not an actuation. */
			bool directed = f->target >= 0;
			bool leaving =
			    (raw < rest) != (f->baseline < rest) || llabs(raw - rest) > llabs(f->baseline - rest);
			active = (!directed || leaving) && raw != rest && llabs(raw - f->baseline) >= threshold;
			if (directed && llabs(raw - rest) < llabs(f->baseline - rest))
				f->baseline = raw;
			break;
		}
	}
	if (active) {
		pad->actuation = (int)index;
		(void)st_controller_preview(pad, index, &pad->actuated_value);
	}
}

static void
publish_value(struct st_pad *pad, size_t index, struct st_controller_event *event, bool known, int value,
    st_controller_emit emit, void *ctx, bool *changed)
{
	struct pad_value *last = &pad->reported[index];
	if (known && (!last->known || last->value != value)) {
		event->record = ST_PAD_VALUE;
		event->value  = value;
		emit(ctx, event);
		last->known = true;
		last->value = value;
		*changed    = true;
	}
}

void
pad_publish(struct st_pad *pad, st_controller_emit emit, void *ctx)
{
	if (!emit)
		return;
	struct st_controller_event event = {.device = pad->id};
	if (pad->announced && (pad->removed || pad->remapped)) {
		event.record = ST_PAD_REMOVE;
		emit(ctx, &event);
		pad->announced = 0;
	}
	if (pad->removed || (pad->announced && !pad->dirty))
		return;
	pad->dirty   = false;
	bool initial = !pad->announced, changed = initial;
	if (initial) {
		memset(pad->reported, 0, 2 * TARGET_COUNT * sizeof(*pad->reported));
		event.record       = ST_PAD_ADD;
		event.device_class = pad->device_class;
		event.source       = pad->source;
		emit(ctx, &event);
		pad->announced = pad->id;
		pad->remapped  = false;
	}
	/* Emit every capability before the first known value. */
	for (int pass = initial ? 0 : 1; pass <= 1; ++pass) {
		for (size_t i = 0; i < pad->count; ++i) {
			struct pad_field *f = pad->fields + i;
			if (f->target < 0)
				continue;
			const struct st_control_target *target = &targets[f->target];
			event.type                             = target->kind == ST_BUTTON ? 1 : 3;
			event.minimum = target->kind == ST_STICK ? -32768 : target->kind == ST_HAT ? -1 : 0;
			event.maximum = target->kind == ST_BUTTON || target->kind == ST_HAT ? 1 : 32767;
			int              direction = target->kind == ST_HAT ? hat_direction(f) : -1;
			static const int hat[8][2] = {
			    {0,  -1},
                            {1,  -1},
                            {1,  0 },
                            {1,  1 },
                            {0,  1 },
                            {-1, 1 },
                            {-1, 0 },
                            {-1, -1}
                        };
			for (unsigned j = 0; j < (target->kind == ST_HAT ? 2u : 1u); ++j) {
				event.code = target->code + j;
				if (!pass) {
					event.record = ST_PAD_CAP;
					emit(ctx, &event);
				}
				else {
					int value = target->kind != ST_HAT ? pad_scale(f, target->kind)
					            : direction < 0        ? 0
					                                   : hat[direction][j];
					publish_value(pad, 2 * (size_t)f->target + j, &event, f->known, value,
					    emit, ctx, &changed);
				}
			}
		}
	}
	if (changed) {
		event.record = ST_PAD_FRAME;
		emit(ctx, &event);
	}
}

bool
pad_attach(struct st_controllers *hub, struct st_pad *pad)
{
	if (!hub->next_id)
		return false;
	pad->reported = calloc(2 * TARGET_COUNT, sizeof(*pad->reported));
	if (!pad->reported)
		return false;
	pad->id        = hub->next_id++;
	pad->actuation = -1;
	if (pad->source == 1)
		pad_load_mapping(hub, pad);
	struct st_pad **tail = &hub->pads;
	while (*tail)
		tail = &(*tail)->next;
	*tail = pad;
	return true;
}

static void
free_pad(struct st_pad *pad)
{
	free(pad->reported);
	free(pad->fields);
	free(pad);
}

struct st_controllers *
pad_create(const char *settings, st_port_next ports, const struct pad_driver *driver)
{
	struct st_controllers *hub = calloc(1, sizeof(*hub));
	if (!hub)
		return NULL;
	if (settings) {
		size_t size   = strlen(settings) + 2;
		char  *parent = malloc(size);
		if (!parent || path_parent(parent, size, settings) < 0
		    || path_join_alloc(&hub->directory, *parent ? parent : ".", "controllers") != 0) {
			free(parent);
			free(hub);
			return NULL;
		}
		free(parent);
	}
	hub->next_id = 1;
	hub->ports   = ports;
	hub->driver  = driver && driver->init(hub) ? driver : NULL;
	return hub;
}

struct st_controllers *
st_controllers_create(const char *settings, st_port_next ports)
{
	static const struct pad_driver native = {pad_native_init, pad_native_poll, pad_native_close};
	return pad_create(settings, ports, ports ? NULL : &native);
}

void
st_controllers_destroy(struct st_controllers *hub)
{
	if (!hub)
		return;
	if (hub->driver)
		hub->driver->close(hub);
	while (hub->pads) {
		struct st_pad *next = hub->pads->next;
		free_pad(hub->pads);
		hub->pads = next;
	}
	free(hub->directory);
	free(hub);
}

bool
st_controllers_available(const struct st_controllers *hub)
{
	return hub && (hub->ports || hub->driver);
}

uint32_t
st_controllers_wait_ms(const struct st_controllers *hub)
{
	if (!st_controllers_available(hub))
		return UINT32_MAX;
	if (hub->pads || hub->ports || !hub->next_scan)
		return 10;
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0 || hub->next_scan <= now)
		return 0;
	return (uint32_t)(hub->next_scan - now);
}

static void
port_sample(struct st_controllers *hub, const struct st_port_sample *sample, st_controller_emit emit, void *ctx)
{
	/* RetroPad physical positions use Nintendo's B/Y/A/X arrangement. */
	static const unsigned buttons[] = {304, 308, 314, 315, 544, 545, 546, 547, 305, 307, 310, 311, 312, 313,
	    317, 318};
	static const unsigned axes[]    = {0, 1, 3, 4, 2, 5};
	struct st_pad        *pad       = hub->pads;
	while (pad && (pad->removed || pad->port != sample->port))
		pad = pad->next;
	if (pad && (!sample->present || sample->generation != pad->generation)) {
		pad->removed = true;
		pad_publish(pad, emit, ctx);
		pad = NULL;
	}
	if (!sample->present)
		return;
	if (!pad) {
		pad = calloc(1, sizeof(*pad));
		if (!pad)
			return;
		pad->source       = 2;
		pad->device_class = 2;
		pad->port         = sample->port;
		pad->generation   = sample->generation;
		pad->count        = sample->analog ? 22 : 16;
		pad->fields       = calloc(pad->count, sizeof(*pad->fields));
		if (!pad->fields) {
			free_pad(pad);
			return;
		}
		snprintf(pad->name, sizeof pad->name, "RetroPad port %u", sample->port + 1);
		for (size_t i = 0; i < pad->count; i++) {
			struct pad_field *f = &pad->fields[i];
			f->target           = i < 16 ? pad_target(1, buttons[i]) : pad_target(3, axes[i - 16]);
			f->kind             = targets[f->target].kind;
			f->minimum          = f->kind == ST_STICK ? -32768 : 0;
			f->maximum          = f->kind == ST_BUTTON ? 1 : 32767;
		}
		if (!pad_attach(hub, pad)) {
			free_pad(pad);
			return;
		}
	}
	for (size_t i = 0; i < pad->count; ++i)
		pad_value(pad, i,
		    i < 16   ? ((sample->buttons >> i) & 1)
		    : i < 20 ? sample->sticks[i - 16]
		             : sample->triggers[i - 20]);
	pad_publish(pad, emit, ctx);
}

void
st_controllers_poll(struct st_controllers *hub, st_controller_emit emit, void *ctx)
{
	if (!hub)
		return;
	if (hub->driver)
		hub->driver->poll(hub, emit, ctx);
	if (hub->ports) {
		struct st_port_sample sample;
		while (hub->ports(&sample))
			port_sample(hub, &sample, emit, ctx);
	}
	for (struct st_pad **p = &hub->pads; *p;) {
		struct st_pad *pad = *p;
		pad_publish(pad, emit, ctx);
		if (pad->removed && !pad->announced) {
			*p = pad->next;
			free_pad(pad);
		}
		else
			p = &pad->next;
	}
}

void
st_controllers_forget_listener(struct st_controllers *hub)
{
	if (hub)
		for (struct st_pad *p = hub->pads; p; p = p->next)
			p->announced = 0;
}

struct st_pad *
st_controller_next(struct st_pad *pad)
{
	do {
		pad = pad->next;
	} while (pad && pad->removed);
	return pad;
}

struct st_pad *
st_controller_first(struct st_controllers *hub)
{
	struct st_pad *pad = hub ? hub->pads : NULL;
	return pad && pad->removed ? st_controller_next(pad) : pad;
}

uint32_t
st_controller_id(const struct st_pad *p)
{
	return p->id;
}

const char *
st_controller_name(const struct st_pad *p)
{
	return p->name;
}

size_t
st_controller_fields(const struct st_pad *p)
{
	return p->source == 1 ? p->count : 0;
}

const char *
st_controller_field(const struct st_pad *p, size_t i)
{
	return p->fields[i].label;
}

int
st_controller_binding(const struct st_pad *p, size_t i, bool *inv)
{
	*inv = p->fields[i].inverted;
	return p->fields[i].target;
}

bool
st_controller_compatible(const struct st_pad *p, size_t i, int target)
{
	if (target == -1)
		return true;
	if (target < 0 || (size_t)target >= TARGET_COUNT || i >= p->count)
		return false;
	enum st_control_kind kind = p->fields[i].kind;
	return kind == targets[target].kind
	       || ((kind == ST_STICK || kind == ST_TRIGGER)
	           && (targets[target].kind == ST_STICK || targets[target].kind == ST_TRIGGER));
}

bool
st_controller_preview(const struct st_pad *p, size_t i, int *v)
{
	const struct pad_field *f = p->fields + i;
	if (!f->known)
		return false;
	*v = f->kind == ST_HAT ? hat_direction(f)
	                       : pad_scale(f, f->target >= 0 ? targets[f->target].kind : f->kind);
	return true;
}

void
st_controller_arm(struct st_pad *p)
{
	p->actuation = -1;
	for (size_t i = 0; i < p->count; i++) {
		p->fields[i].baseline = p->fields[i].raw;
		p->fields[i].armed    = p->fields[i].known;
	}
}

int
st_controller_actuation(const struct st_pad *p, int *value)
{
	*value = p->actuated_value;
	return p->actuation;
}
