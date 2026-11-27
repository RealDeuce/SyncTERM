/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "st_ddofx_script.h"
#include "st_time.h"

enum {
	EVENT_COUNT = 200
};

struct event {
	struct st_ddofx_callback callback;
	uint64_t                 interval_ms, due;
	time_t                   date;
	bool                     calendar, loop, defined, active;
};

struct st_ddofx_events {
	struct event slots[EVENT_COUNT];
	uint64_t     next_due;
	unsigned     cursor;
};

static void
next_deadline(struct st_ddofx_events *events)
{
	events->next_due = UINT64_MAX;
	for (unsigned i = 0; i < EVENT_COUNT; ++i) {
		const struct event *e = &events->slots[i];
		if (e->active && e->due < events->next_due)
			events->next_due = e->due;
	}
}

static bool
calendar_date(const int32_t *parts, time_t *date)
{
	int month = parts[0], day = parts[1], year = parts[2];
	int hour = parts[3], minute = parts[4], second = parts[5];
	if (month < 1 || month > 12 || day < 1 || day > 31 || year < 1900 || year > 9999 || hour < 0 || hour > 23
	    || minute < 0 || minute > 59 || second < 0 || second > 59)
		return false;
	struct tm local = {.tm_year = year - 1900,
	    .tm_mon                 = month - 1,
	    .tm_mday                = day,
	    .tm_hour                = hour,
	    .tm_min                 = minute,
	    .tm_sec                 = second,
	    .tm_isdst               = -1};
	*date           = mktime(&local);
	return *date != (time_t)-1 && local.tm_mon == month - 1 && local.tm_mday == day && local.tm_hour == hour;
}

static void
define_event(struct st_ddofx *ddo, int32_t id, char *args)
{
	int32_t type, interval, parts[6], loop;
	if (!st_ddofx_field_number(ddo, &args, &type) || !st_ddofx_field_number(ddo, &args, &interval))
		return;
	for (unsigned i = 0; i < 6; ++i)
		if (!st_ddofx_field_number(ddo, &args, &parts[i]))
			return;
	if (!st_ddofx_field_number(ddo, &args, &loop))
		return;
	const char *reply = st_ddofx_field(&args);
	if (args == NULL)
		return;
	struct event event = {.defined = true, .loop = loop != 0};
	if (type == 1) {
		event.calendar = true;
		if (!calendar_date(parts, &event.date))
			return;
	}
	else if (type == 0 && interval > 0)
		event.interval_ms = (1000ULL + interval - 1) / interval;
	else if (type == 2 && interval > 0)
		event.interval_ms = (uint64_t)interval * 1000;
	else
		return;

	st_ddofx_callback_define(ddo, &event.callback, reply, args);
	if (ddo->events == NULL) {
		ddo->events = calloc(1, sizeof(*ddo->events));
		if (ddo->events == NULL)
			return;
	}
	ddo->events->slots[id - 1] = event;
	next_deadline(ddo->events);
}

void
st_ddofx_event_command(struct st_ddofx *ddo, const char *verb, char *args)
{
	int32_t id;
	if (!st_ddofx_field_number(ddo, &args, &id))
		return;
	if (id == 0 && !strcmp(verb, "CEVENT")) {
		free(ddo->events);
		ddo->events = NULL;
		return;
	}
	if (id < 1 || id > EVENT_COUNT)
		return;
	if (!strcmp(verb, "CEVENT")) {
		define_event(ddo, id, args);
		return;
	}
	if (args != NULL || ddo->events == NULL)
		return;
	struct event *e = &ddo->events->slots[id - 1];
	if (!e->defined)
		return;
	if (!strcmp(verb, "AEVENT")) {
		uint64_t now;
		if (st_time_monotonic_ms(&now) != 0)
			return;
		e->active = true;
		e->due    = now + e->interval_ms;
	}
	else
		e->active = false;
	next_deadline(ddo->events);
}

static bool
events_active(const struct st_ddofx *ddo)
{
	/* Event macros may change pages, palettes or modes. Dispatch them at
	 * foreground command boundaries once a palette transition finishes. */
	return ddo->events != NULL && !ddo->fade_due && !ddo->background_paused && !ddo->in_event
	       && ddo->events->next_due != UINT64_MAX;
}

uint32_t
st_ddofx_event_wait_ms(const struct st_ddofx *ddo)
{
	if (!events_active(ddo))
		return UINT32_MAX;
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0)
		return UINT32_MAX;
	if (now >= ddo->events->next_due)
		return 0;
	uint64_t delay = ddo->events->next_due - now;
	return delay >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)delay;
}

void
st_ddofx_event_poll(struct st_ddofx *ddo)
{
	if (!events_active(ddo))
		return;
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0 || now < ddo->events->next_due)
		return;

	struct st_ddofx_events *events = ddo->events;
	for (unsigned offset = 0; offset < EVENT_COUNT; ++offset) {
		unsigned      i = (events->cursor + offset) % EVENT_COUNT;
		struct event *e = &events->slots[i];
		if (!e->active || e->due > now)
			continue;
		if (e->calendar && time(NULL) < e->date) {
			/* Calendar events follow changes to the local wall clock. */
			e->due = now + 1000;
			continue;
		}
		e->active      = e->loop && !e->calendar;
		e->due         = now + e->interval_ms;
		events->cursor = (i + 1) % EVENT_COUNT;
		st_ddofx_callback_start(ddo, &e->callback, false);
		break;
	}
	next_deadline(events);
}
