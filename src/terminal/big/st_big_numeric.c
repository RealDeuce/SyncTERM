/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_numeric.h"
#include "st_big_random.h"
#include "st_time.h"
#include "io_error.h"

#include <string.h>

enum { RANDOM_BATCH = 64 };
static int default_now(void *context, uint64_t *now)
{
	(void)context; return st_time_monotonic_ns(now);
}
static uint64_t add_duration(uint64_t start, uint64_t duration)
{
	return start > UINT64_MAX-duration ? UINT64_MAX : start+duration;
}
static uint64_t loop_duration(uint16_t word)
{
	int32_t units = st_big_number_signed(word);
	return units <= 0 ? 0 : ((uint64_t)units*UINT64_C(5000000000)+90)/91;
}
static int synchronous(int status)
{
	return status == IO_ERROR_IN_PROGRESS ? IO_ERROR_PROTOCOL : status;
}
static int sample(struct st_big_numeric *n, uint64_t *now)
{
	int status = synchronous(n->io.now_ns(n->io.context,now));
	if (status != 0) return status;
	if (n->field.sample_valid && *now < n->field.last_sample) return IO_ERROR_OUT_OF_RANGE;
	n->field.last_sample = *now; n->field.sample_valid = true;
	return 0;
}
static int peek(struct st_big_numeric *n, bool *ready)
{
	*ready = false;
	return n->io.peek_key == NULL ? 0 : synchronous(n->io.peek_key(n->io.context,ready));
}
static int loop_delay(void *context, uint16_t duration, bool first)
{
	struct st_big_numeric *n = context;
	uint64_t now;
	int status;
	if (first) {
		status = sample(n,&n->field.delay_start);
		if (status != 0) return status;
		n->field.delay_wait = true;
	}
	status = sample(n,&now);
	if (status != 0) return status;
	bool ready;
	status = peek(n,&ready);
	if (status != 0) return status;
	if (!ready && now < add_duration(n->field.delay_start,loop_duration(duration)))
		return IO_ERROR_IN_PROGRESS;
	n->field.delay_wait = false;
	return 0;
}
static int checkpoint(void *context, bool *cancel)
{
	struct st_big_numeric *n = context;
	*cancel = false;
	if (!n->field.key_pending) {
		bool ready;
		int status = peek(n,&ready);
		if (status != 0 || !ready) return status;
		n->field.key_pending = true;
	}
	uint8_t key;
	int status = n->io.read_key(n->io.context,&key);
	if (status != 0) return status;
	n->field.key_pending = false;
	*cancel = key == 3 || key == 0x18;
	return 0;
}
static int random_word(void *context, uint16_t *word)
{
	struct st_big_numeric *n = context;
	size_t attempts;
	return st_big_random_range(&n->random_state,n->random_from,n->random_to,
	    RANDOM_BATCH,&attempts,word);
}
static int stored(struct st_big_numeric *n, uint16_t *word, bool *supplied)
{
	const struct st_big_loop_sink sink = {n,loop_delay,checkpoint,random_word};
	return st_big_loop_poll(&n->field.stored,&n->loop,&sink,word,supplied);
}
static int fail(struct st_big_numeric *n, int status)
{
	if (status == IO_ERROR_IN_PROGRESS) return status;
	n->field.phase = ST_BIG_NUMERIC_FAILED; n->field.error = status;
	n->field.wire_wait = n->field.delay_wait = false;
	return status;
}
static int arm_wire(struct st_big_numeric *n)
{
	uint64_t now;
	int status = sample(n,&now);
	if (status != 0) return status;
	n->field.wire_deadline = add_duration(now,ST_BIG_NUMERIC_TIMEOUT_NS);
	n->field.wire_wait = true;
	return 0;
}

void
st_big_numeric_cancel(struct st_big_numeric *n)
{
	if (n != NULL) memset(&n->field,0,sizeof(n->field));
}

int
st_big_numeric_init(struct st_big_numeric *n, const struct st_big_numeric_io *io)
{
	if (n == NULL || (io != NULL && ((io->peek_key == NULL) != (io->read_key == NULL))))
		return IO_ERROR_INVALID_ARGUMENT;
	struct st_big_numeric_io selected = io == NULL ? (struct st_big_numeric_io){0} : *io;
	if (selected.now_ns == NULL) selected.now_ns = default_now;
	memset(n,0,sizeof(*n)); n->io = selected;
	n->random_state = 1; n->random_to = 199; n->context.resolution = 2;
	n->initialized = true;
	return 0;
}

bool
st_big_numeric_deadline(const struct st_big_numeric *n, uint64_t *deadline)
{
	if (n == NULL || deadline == NULL || !n->initialized) return false;
	if (n->field.phase == ST_BIG_NUMERIC_WIRE && n->field.wire_wait) {
		*deadline = n->field.wire_deadline; return true;
	}
	if ((n->field.phase == ST_BIG_NUMERIC_SELECT || n->field.phase == ST_BIG_NUMERIC_WIRE_STORED)
	    && n->field.stored.phase == ST_BIG_LOOP_READER_DELAY && n->field.delay_wait) {
		*deadline = add_duration(n->field.delay_start,loop_duration(n->loop.delay)); return true;
	}
	return false;
}

int
st_big_numeric_field(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter)
{
	struct st_big_numeric *n = context;
	(void)view;
	if (n == NULL || !n->initialized || parameter == NULL
	    || parameter->kind < ST_GRAPHICS_NUMBER || parameter->kind > ST_GRAPHICS_BYTES)
		return IO_ERROR_INVALID_ARGUMENT;
	st_big_numeric_cancel(n);
	if (parameter->kind != ST_GRAPHICS_NUMBER) return 0;
	n->field.phase = ST_BIG_NUMERIC_SELECT;
	n->field.y_coordinate = parameter->axis == ST_GRAPHICS_Y;
	st_big_number_begin(&n->field.number);
	st_big_loop_reader_begin(&n->field.stored);
	return 0;
}

int
st_big_numeric_number(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter, int *value, bool *supplied)
{
	struct st_big_numeric *n = context;
	(void)view;
	if (supplied != NULL) *supplied = false;
	if (n == NULL || !n->initialized || parameter == NULL || parameter->kind != ST_GRAPHICS_NUMBER
	    || value == NULL || supplied == NULL || n->field.phase <= ST_BIG_NUMERIC_OFF
	    || n->field.phase > ST_BIG_NUMERIC_FAILED)
		return IO_ERROR_INVALID_ARGUMENT;
	if (n->field.phase == ST_BIG_NUMERIC_FAILED) return n->field.error;
	if (n->field.phase == ST_BIG_NUMERIC_SELECT) {
		uint16_t word; bool source;
		int status = stored(n,&word,&source);
		if (status != 0) return fail(n,status);
		if (source) {
			n->field.word = word; n->field.phase = ST_BIG_NUMERIC_DONE_SOURCE;
		} else {
			n->field.phase = ST_BIG_NUMERIC_WIRE;
			status = arm_wire(n);
			if (status != 0) return fail(n,status);
		}
	}
	if (n->field.phase == ST_BIG_NUMERIC_DONE_SOURCE) {
		*supplied = true; *value = (int)st_big_number_signed(n->field.word);
	}
	return 0;
}

static int
wire_tail(struct st_big_numeric *n)
{
	if (n->field.phase == ST_BIG_NUMERIC_WIRE_STORED) {
		uint16_t word; bool supplied;
		int status = stored(n,&word,&supplied);
		if (status != 0) return status;
		if (!supplied) return IO_ERROR_PROTOCOL;
		n->field.word = word;
	} else if (n->field.phase == ST_BIG_NUMERIC_WIRE_RANDOM) {
		int status = random_word(n,&n->field.word);
		if (status != 0) return status;
	}
	n->loop.command_read = true;
	n->field.phase = ST_BIG_NUMERIC_DONE_WIRE;
	return 0;
}

int
st_big_numeric_read_number(void *context, const struct st_graphics_command_view *view,
    const struct st_graphics_parameter *parameter, const uint8_t *bytes, size_t size,
    size_t *accepted, int *value, bool *complete)
{
	struct st_big_numeric *n = context;
	(void)view;
	if (accepted != NULL) *accepted = 0;
	if (complete != NULL) *complete = false;
	if (n == NULL || !n->initialized || parameter == NULL || parameter->kind != ST_GRAPHICS_NUMBER
	    || accepted == NULL || value == NULL || complete == NULL || (bytes == NULL && size != 0)
	    || n->field.phase <= ST_BIG_NUMERIC_OFF || n->field.phase > ST_BIG_NUMERIC_FAILED
	    || n->field.phase == ST_BIG_NUMERIC_SELECT
	    || n->field.phase == ST_BIG_NUMERIC_DONE_SOURCE)
		return IO_ERROR_INVALID_ARGUMENT;
	if (n->field.phase == ST_BIG_NUMERIC_FAILED) return n->field.error;
	while (n->field.phase == ST_BIG_NUMERIC_WIRE) {
		uint64_t now;
		int status = sample(n,&now);
		if (status != 0) return fail(n,status);
		bool expired = now >= n->field.wire_deadline;
		bool available = *accepted < size;
		if (!available && !expired) return 0;
		/* The original notification precedes construction and its scale
		 * gates. Its owner effects are visible to the following calculation.
		 * The leaf reader's bell result is therefore not emitted again. */
		bool auxiliary = st_big_number_auxiliary(&n->field.number);
		if (expired && (!auxiliary || !n->context.auto_mode) && n->io.bell != NULL)
			n->io.bell(n->io.context);
		struct st_big_number_context current = n->context;
		current.y_coordinate = n->field.y_coordinate;
		struct st_big_number_result result;
		size_t used;
		status = st_big_number_feed(&n->field.number,&current,available ? bytes+*accepted : NULL,
		    available ? 1 : 0,expired,&used,&result);
		*accepted += used;
		if (status != 0) return fail(n,status);
		if (result.complete) {
			n->field.wire_wait = false; n->field.word = result.word;
			/* _value checks exact loop1 again at the common tail. An owner
			 * callback may have changed it after ordinary source selection. */
			if (n->loop.active == 1) {
				st_big_loop_reader_begin(&n->field.stored);
				n->field.phase = ST_BIG_NUMERIC_WIRE_STORED;
			} else if (result.word == 0x8003 && n->loop.random_suppression == 0)
				n->field.phase = ST_BIG_NUMERIC_WIRE_RANDOM;
			break;
		}
		status = arm_wire(n);
		if (status != 0) return fail(n,status);
		if (*accepted == size) return 0;
	}
	if (n->field.phase != ST_BIG_NUMERIC_DONE_WIRE) {
		int status = wire_tail(n);
		if (status != 0) return fail(n,status);
	}
	*complete = true; *value = (int)st_big_number_signed(n->field.word);
	return 0;
}
