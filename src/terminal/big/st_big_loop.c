/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_loop.h"
#include "st_big_number.h"
#include "io_error.h"

#include <string.h>

static uint16_t add(uint16_t a, uint16_t b) { return (uint16_t)((uint32_t)a+b); }
static uint16_t subtract(uint16_t a, uint16_t b) { return (uint16_t)((uint32_t)a-b); }
static uint16_t magnitude(uint16_t word)
{
	int32_t n = st_big_number_signed(word);
	return (uint16_t)(n < 0 ? -n : n);
}
static bool varying(uint16_t word)
{
	int32_t n = st_big_number_signed(word);
	return word == 0x8001 || word == 0x8002 || (n > -30000 && n < 0);
}
static bool valid_count(const struct st_big_loop *loop)
{
	return loop->count >= 1 && loop->count <= ST_BIG_LOOP_CAPACITY;
}
static bool valid_index(const struct st_big_loop *loop)
{
	return valid_count(loop) && loop->index < loop->count;
}

void
st_big_loop_init(struct st_big_loop *loop)
{
	if (loop != NULL) memset(loop,0,sizeof(*loop));
}

uint16_t
st_big_loop_setup_step(uint16_t from, uint16_t to, uint16_t step)
{
	if (st_big_number_signed(from) > st_big_number_signed(to)) step = subtract(0,step);
	return step == 0 ? 1 : step;
}

int
st_big_loop_publish(struct st_big_loop *loop)
{
	if (loop == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (!valid_count(loop)) return IO_ERROR_OUT_OF_RANGE;
	loop->index = loop->varying_total = loop->pass = 0;
	loop->active = loop->no_parameters = 1;
	loop->random_suppression = loop->first_text = 0;
	return 0;
}

int
st_big_loop_scan(struct st_big_loop *loop)
{
	if (loop == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (!valid_count(loop)) return IO_ERROR_OUT_OF_RANGE;
	/* Publish sets no_parameters before the owner's flow operation. Scan
	 * only clears it; it must not erase intervening owner changes. */
	for (size_t i = 0; i < loop->count; ++i)
		if (varying(loop->words[i])) loop->no_parameters = 0;
	return 0;
}

void
st_big_loop_reader_cancel(struct st_big_loop_reader *reader)
{
	if (reader != NULL) memset(reader,0,sizeof(*reader));
}

void
st_big_loop_reader_begin(struct st_big_loop_reader *reader)
{
	if (reader == NULL) return;
	st_big_loop_reader_cancel(reader);
	reader->phase = ST_BIG_LOOP_READER_ENTRY;
}

static uint16_t
calculate(struct st_big_loop *loop)
{
	uint16_t q = loop->words[loop->index], word = q;
	if (!varying(q)) return word;
	loop->varying_count = add(loop->varying_count,1);
	if (loop->pass == 0) loop->saved = loop->current;
	if (st_big_number_signed(loop->varying_count) < 2 && loop->varying_total != 0) {
		loop->current = add(loop->current,loop->step);
		loop->pass = add(loop->pass,1);
		if (loop->pass == 1 && loop->type == '|') loop->current = loop->saved;
		if (loop->pass == 2 && loop->type == '|') loop->pass = 0;
	}
	if (st_big_number_signed(q) < -29999) word = loop->current;
	loop->varying_total = add(loop->varying_total,1);
	if (q == 0x8002) {
		int32_t origin = st_big_number_signed(loop->origin), to = st_big_number_signed(loop->to);
		word = origin == to ? 0 : subtract(origin < to ? loop->to : loop->origin,loop->current);
	}
	/* Each test observes the changed WORD from the previous transform. */
	int32_t n = st_big_number_signed(word);
	if (n < 0 && n > -10000) word = add(magnitude(word),loop->current);
	n = st_big_number_signed(word);
	if (n < -9999 && n > -20000) word = subtract(loop->current,magnitude(add(word,10000)));
	n = st_big_number_signed(word);
	if (n < -19999 && n > -30000) word = subtract(magnitude(add(word,20000)),loop->current);
	return word;
}

static void
boundary(struct st_big_loop *loop)
{
	if (loop->no_parameters == 1 && loop->index == 0) loop->current = add(loop->current,loop->step);
	int32_t step = st_big_number_signed(loop->step);
	int32_t current = st_big_number_signed(loop->current), to = st_big_number_signed(loop->to);
	if (loop->index == loop->count-1
	    && ((step >= 1 && current >= to) || (step < 0 && current <= to))) loop->active = 0;
	if (loop->type != '@' && loop->active == 0 && loop->command == 'W') loop->type = 'Z';
	loop->index = add(loop->index,1);
	if (st_big_number_signed(loop->index) >= st_big_number_signed(loop->count)) loop->index = 0;
}

static int
failed(struct st_big_loop_reader *reader, int status)
{
	reader->phase = ST_BIG_LOOP_READER_FAILED;
	reader->error = status;
	return status;
}

static void
complete(struct st_big_loop_reader *reader, struct st_big_loop *loop)
{
	loop->command_read = true;
	reader->supplied = true;
	reader->phase = ST_BIG_LOOP_READER_DONE;
}

int
st_big_loop_poll(struct st_big_loop_reader *reader, struct st_big_loop *loop,
    const struct st_big_loop_sink *sink, uint16_t *word, bool *supplied)
{
	if (supplied != NULL) *supplied = false;
	if (reader == NULL || loop == NULL || sink == NULL || word == NULL || supplied == NULL
	    || sink->delay == NULL || sink->key == NULL || sink->random == NULL
	    || reader->phase <= ST_BIG_LOOP_READER_OFF || reader->phase > ST_BIG_LOOP_READER_FAILED)
		return IO_ERROR_INVALID_ARGUMENT;
	for (;;) {
		int status;
		switch (reader->phase) {
		case ST_BIG_LOOP_READER_ENTRY:
			if (loop->active == 0) reader->phase = ST_BIG_LOOP_READER_DONE;
			else if (loop->active != 1) complete(reader,loop);
			else {
				if (!valid_index(loop)) return failed(reader,IO_ERROR_OUT_OF_RANGE);
				reader->phase = ST_BIG_LOOP_READER_CALCULATE;
				if (loop->index == 0) {
					loop->varying_count = 0;
					reader->phase = ST_BIG_LOOP_READER_DELAY;
				}
			}
			break;
		case ST_BIG_LOOP_READER_DELAY: {
			bool first = !reader->delay_seen;
			reader->delay_seen = true;
			status = sink->delay(sink->context,loop->delay,first);
			if (status != 0) return status == IO_ERROR_IN_PROGRESS ? status : failed(reader,status);
			reader->phase = ST_BIG_LOOP_READER_CALCULATE;
			break;
		}
		case ST_BIG_LOOP_READER_CALCULATE:
			if (!valid_index(loop)) return failed(reader,IO_ERROR_OUT_OF_RANGE);
			reader->word = calculate(loop);
			reader->phase = ST_BIG_LOOP_READER_KEY;
			break;
		case ST_BIG_LOOP_READER_KEY: {
			bool cancel = false;
			status = sink->key(sink->context,&cancel);
			if (status != 0) return status == IO_ERROR_IN_PROGRESS ? status : failed(reader,status);
			if (cancel) loop->active = 0;
			if (!valid_index(loop)) return failed(reader,IO_ERROR_OUT_OF_RANGE);
			boundary(loop);
			if (reader->word == 0x8003 && loop->random_suppression == 0)
				reader->phase = ST_BIG_LOOP_READER_RANDOM;
			else complete(reader,loop);
			break;
		}
		case ST_BIG_LOOP_READER_RANDOM: {
			uint16_t result;
			status = sink->random(sink->context,&result);
			if (status != 0) return status == IO_ERROR_IN_PROGRESS ? status : failed(reader,status);
			reader->word = result;
			complete(reader,loop);
			break;
		}
		case ST_BIG_LOOP_READER_DONE:
			*supplied = reader->supplied;
			if (*supplied) *word = reader->word;
			return 0;
		case ST_BIG_LOOP_READER_FAILED: return reader->error;
		case ST_BIG_LOOP_READER_OFF: return IO_ERROR_INVALID_ARGUMENT;
		}
	}
}
