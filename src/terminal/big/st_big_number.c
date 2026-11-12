/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_big_number.h"
#include "io_error.h"

#include <string.h>

void
st_big_number_begin(struct st_big_number_reader *reader)
{
	if (reader == NULL) return;
	memset(reader,0,sizeof(*reader));
	reader->phase = ST_BIG_NUMBER_DIGITS;
}

void
st_big_number_cancel(struct st_big_number_reader *reader)
{
	if (reader != NULL) memset(reader,0,sizeof(*reader));
}

bool
st_big_number_auxiliary(const struct st_big_number_reader *reader)
{
	return reader != NULL && reader->phase > ST_BIG_NUMBER_DIGITS
	    && reader->phase < ST_BIG_NUMBER_DONE;
}

int32_t
st_big_number_signed(uint16_t word)
{
	return word <= 32767 ? (int32_t)word : (int32_t)word-INT32_C(65536);
}

uint16_t
st_big_number_scale(uint16_t word, const struct st_big_number_context *context)
{
	if (context == NULL || !context->y_coordinate) return word;
	int32_t n = st_big_number_signed(word);
	if (context->scaling == 2 && context->resolution == 2)
		return (uint16_t)(7*n/4);
	if (context->scaling != 3 || context->resolution != 3 || n == 0) return word;
	/* The original binary64 literal is below 12/5. Its software/default
	 * 64-significand product cannot reach the next integer for WORD inputs.
	 * Take the magnitude in a wide type before negating -32768. */
	uint32_t magnitude = (uint32_t)(n < 0 ? -n : n);
	int32_t scaled = (int32_t)((12*magnitude-1)/5);
	return (uint16_t)(n < 0 ? -scaled : scaled);
}

static uint16_t
construct(struct st_big_number_reader *reader)
{
	if (reader->count == 0 || reader->count > 4) return 0;
	uint32_t sum = reader->digits[reader->count-1], weight = 10;
	for (size_t i = reader->count-1; i != 0; --i) {
		/* Weight the slots themselves: continuation reconstructs these
		 * mutated WORDs, not the original decimal digits. */
		reader->digits[i-1] = (uint16_t)((uint32_t)reader->digits[i-1]*weight);
		sum += reader->digits[i-1];
		weight *= 10;
	}
	return (uint16_t)sum;
}

static void
expression(struct st_big_number_reader *reader, uint8_t byte)
{
	if (byte == '+' || byte == '-' || byte == '!') {
		reader->expression = byte == '+' ? 1 : byte == '-' ? 2 : 3;
		reader->phase = ST_BIG_NUMBER_DIGITS;
		return;
	}
	/* These are stored-loop encodings, not conventional signed literals.
	 * Explicit unsigned WORD reduction also covers a sentinel operand. */
	if (reader->expression != 0)
		reader->word = (uint16_t)(-INT32_C(10000)*(int32_t)(reader->expression-1)
		    -(int32_t)reader->word);
	reader->phase = ST_BIG_NUMBER_DONE;
}

static void
ordinary_control(struct st_big_number_reader *reader, uint8_t byte)
{
	switch (byte) {
	case '_': reader->phase = ST_BIG_NUMBER_SKIP_FIRST; return;
	case 'x': reader->word = 0x8001; reader->phase = ST_BIG_NUMBER_AFTER_X; return;
	case 'y': reader->word = 0x8002; reader->phase = ST_BIG_NUMBER_AFTER_Y; return;
	case 'r': reader->word = 0x8003; reader->phase = ST_BIG_NUMBER_AFTER_R; return;
	default: expression(reader,byte); return;
	}
}

static void
accept(struct st_big_number_reader *reader,
    const struct st_big_number_context *context, uint8_t byte)
{
	if (reader->phase == ST_BIG_NUMBER_DIGITS) {
		if (byte >= '0' && byte <= '9') {
			reader->count = (uint16_t)(reader->count+1u);
			if (reader->count >= 1 && reader->count <= 4)
				reader->digits[reader->count-1] = (uint16_t)(byte-'0');
			return;
		}
		reader->word = st_big_number_scale(construct(reader),context);
		ordinary_control(reader,byte);
		return;
	}
	if (reader->phase == ST_BIG_NUMBER_SKIP_FIRST) {
		reader->phase = ST_BIG_NUMBER_SKIP_SECOND;
		return;
	}
	if (reader->phase == ST_BIG_NUMBER_SKIP_SECOND) {
		reader->phase = ST_BIG_NUMBER_DIGITS;
		return;
	}
	/* x/y/r are independent ordered tests. An auxiliary byte can select a
	 * later sentinel or an expression prefix, but never restart an earlier
	 * test (nor underscore or decimal classification). */
	if (reader->phase == ST_BIG_NUMBER_AFTER_X && byte == 'y') {
		reader->word = 0x8002;
		reader->phase = ST_BIG_NUMBER_AFTER_Y;
		return;
	}
	if ((reader->phase == ST_BIG_NUMBER_AFTER_X || reader->phase == ST_BIG_NUMBER_AFTER_Y)
	    && byte == 'r') {
		reader->word = 0x8003;
		reader->phase = ST_BIG_NUMBER_AFTER_R;
		return;
	}
	expression(reader,byte);
}

int
st_big_number_feed(struct st_big_number_reader *reader,
    const struct st_big_number_context *context, const uint8_t *bytes, size_t size,
    bool timeout, size_t *consumed, struct st_big_number_result *result)
{
	if (consumed != NULL) *consumed = 0;
	if (result != NULL) *result = (struct st_big_number_result){0};
	if (reader == NULL || context == NULL || consumed == NULL || result == NULL
	    || (bytes == NULL && size != 0) || reader->phase <= ST_BIG_NUMBER_OFF
	    || reader->phase > ST_BIG_NUMBER_DONE)
		return IO_ERROR_INVALID_ARGUMENT;
	while (reader->phase != ST_BIG_NUMBER_DONE && (*consumed < size || timeout)) {
		bool auxiliary = st_big_number_auxiliary(reader);
		uint8_t byte = *consumed < size ? bytes[(*consumed)++] : 0;
		if (timeout) {
			byte = auxiliary ? ' ' : 0;
			result->bell = !auxiliary || !context->auto_mode;
			timeout = false;
		}
		accept(reader,context,byte);
	}
	result->complete = reader->phase == ST_BIG_NUMBER_DONE;
	result->word = reader->word;
	return 0;
}
