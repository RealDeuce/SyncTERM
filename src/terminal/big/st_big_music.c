/* Original N15/67 WORD arithmetic, callback and protocol-reached N4 writes. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "io_error.h"
#include "st_big_music.h"

int
st_big_music_buffer_read(const struct st_big_music_buffer *buffer, uint16_t offset, bool word, uint16_t *value)
{
	if (buffer == NULL || value == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	size_t size = word ? 2 : 1;
	if (offset > ST_BIG_MUSIC_CAPACITY - size)
		return IO_ERROR_OUT_OF_RANGE;
	uint16_t result = buffer->bytes[offset];
	if (word)
		result |= (uint16_t)((uint16_t)buffer->bytes[(size_t)offset + 1] << 8);
	*value = result;
	return 0;
}

int
st_big_music_buffer_store(struct st_big_music_buffer *buffer, uint16_t offset, uint8_t value)
{
	if (buffer == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (offset >= ST_BIG_MUSIC_CAPACITY)
		return IO_ERROR_OUT_OF_RANGE;
	buffer->bytes[offset] = value;
	return 0;
}

static int64_t
signed_word_pair(uint32_t value)
{
	return value < UINT32_C(0x80000000) ? (int64_t)value : (int64_t)value - INT64_C(4294967296);
}

uint16_t
st_big_music_selector(uint16_t word)
{
	return word <= 5 ? word : 2;
}

uint16_t
st_big_music_pair(uint16_t whole, uint16_t remainder, bool length)
{
	uint16_t value = (uint16_t)((uint32_t)whole * 1000 + remainder);
	return length && value == 65535 ? 65534 : value;
}

uint32_t
st_big_music_end(uint16_t length, bool slice, uint16_t to)
{
	uint32_t limit = (uint16_t)(length - 1);
	uint32_t end   = (uint32_t)(to / 3) * 3 + 1;
	return !slice || end > limit ? limit : end;
}

uint32_t
st_big_music_start(uint32_t end, bool slice, uint16_t from)
{
	if (!slice)
		return 2;
	uint32_t position = (uint32_t)(from / 3) * 3 + 2, cutoff = end - UINT32_C(3);
	return signed_word_pair(position) > signed_word_pair(cutoff) ? cutoff : position;
}

uint32_t
st_big_music_position(const struct st_big_music *m)
{
	return m == NULL ? 0 : (uint32_t)m->position_low | ((uint32_t)m->position_high << 16);
}

void
st_big_music_callback_begin(struct st_big_music_callback *r)
{
	if (r != NULL)
		*r = (struct st_big_music_callback) {.phase = ST_BIG_MUSIC_CALLBACK_ENTRY};
}

static int
failed(struct st_big_music_callback *r, int status)
{
	if (status != IO_ERROR_IN_PROGRESS) {
		r->error = status;
		r->phase = ST_BIG_MUSIC_CALLBACK_FAILED;
	}
	return status;
}

int
st_big_music_callback_poll(struct st_big_music_callback *r, struct st_big_music *m,
    const struct st_big_music_sink *sink, size_t *budget, uint16_t *returned, bool *complete)
{
	if (r == NULL || m == NULL || budget == NULL || returned == NULL || complete == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*complete = false;
	for (;;) {
		if (r->phase == ST_BIG_MUSIC_CALLBACK_FAILED)
			return r->error;
		if (r->phase == ST_BIG_MUSIC_CALLBACK_OFF)
			return IO_ERROR_INVALID_ARGUMENT;
		if (r->phase == ST_BIG_MUSIC_CALLBACK_DONE) {
			*returned = r->returned_delay;
			*complete = true;
			return 0;
		}
		if (*budget == 0)
			return IO_ERROR_IN_PROGRESS;
		--*budget;
		int      status;
		uint16_t value;
		switch (r->phase) {
			case ST_BIG_MUSIC_CALLBACK_ENTRY:
				r->phase =
				    m->running == 1 ? ST_BIG_MUSIC_CALLBACK_POINTER : ST_BIG_MUSIC_CALLBACK_TAIL;
				break;
			case ST_BIG_MUSIC_CALLBACK_POINTER:
				m->value_offset = (uint16_t)(m->position_low + 1);
				r->phase        = ST_BIG_MUSIC_CALLBACK_ADDRESS;
				break;
			case ST_BIG_MUSIC_CALLBACK_ADDRESS:
				if (!r->read_latched) {
					r->read_offset  = m->position_low;
					r->read_latched = true;
				}
				if (sink == NULL || sink->read == NULL)
					return failed(r, IO_ERROR_PROTOCOL);
				status = sink->read(sink->context, r->read_offset, false, &value);
				if (status != 0)
					return failed(r, status);
				m->address      = (uint8_t)value;
				r->read_latched = false;
				r->phase        = ST_BIG_MUSIC_CALLBACK_DATA;
				break;
			case ST_BIG_MUSIC_CALLBACK_DATA:
				if (!r->read_latched) {
					r->read_offset  = m->value_offset;
					r->read_latched = true;
				}
				if (sink == NULL || sink->read == NULL)
					return failed(r, IO_ERROR_PROTOCOL);
				status = sink->read(sink->context, r->read_offset, true, &value);
				if (status != 0)
					return failed(r, status);
				m->data         = value;
				r->read_latched = false;
				r->phase        = ST_BIG_MUSIC_CALLBACK_DELAY;
				break;
			case ST_BIG_MUSIC_CALLBACK_DELAY:
				if (m->address == 255)
					m->delay = m->data;
				r->phase = ST_BIG_MUSIC_CALLBACK_REGISTER;
				break;
			case ST_BIG_MUSIC_CALLBACK_REGISTER:
				r->output_latched = false;
				r->phase =
				    m->address != 255 ? ST_BIG_MUSIC_CALLBACK_OUTPUT : ST_BIG_MUSIC_CALLBACK_LOW;
				break;
			case ST_BIG_MUSIC_CALLBACK_OUTPUT:
				if (!r->output_latched) {
					r->output_address = m->address;
					r->output_data    = m->data;
					r->output_latched = true;
				}
				if (sink == NULL || sink->write == NULL)
					return failed(r, IO_ERROR_PROTOCOL);
				status = sink->write(sink->context, r->output_address, r->output_data);
				if (status != 0)
					return failed(r, status);
				r->phase = ST_BIG_MUSIC_CALLBACK_LOW;
				break;
			case ST_BIG_MUSIC_CALLBACK_LOW:
			{
				uint32_t sum    = (uint32_t)m->position_low + 3;
				m->position_low = (uint16_t)sum;
				r->carry        = sum > 65535;
				r->phase        = ST_BIG_MUSIC_CALLBACK_HIGH;
				break;
			}
			case ST_BIG_MUSIC_CALLBACK_HIGH:
				m->position_high = (uint16_t)(m->position_high + (r->carry ? 1 : 0));
				r->phase         = ST_BIG_MUSIC_CALLBACK_END;
				break;
			case ST_BIG_MUSIC_CALLBACK_END:
			{
				uint32_t end = (uint32_t)m->end_low | ((uint32_t)m->end_high << 16);
				if (signed_word_pair(end - UINT32_C(3))
				    <= signed_word_pair(st_big_music_position(m)))
					m->running = 0;
				r->phase = ST_BIG_MUSIC_CALLBACK_NEXT;
				break;
			}
			case ST_BIG_MUSIC_CALLBACK_NEXT:
				/* Original nonff back edge bypasses ENTRY, even after END cleared
			 * running. Allocated storage, not loaded length, limits reads. */
				r->phase =
				    m->address != 255 ? ST_BIG_MUSIC_CALLBACK_POINTER : ST_BIG_MUSIC_CALLBACK_TAIL;
				break;
			case ST_BIG_MUSIC_CALLBACK_TAIL:
				if (m->running == 0)
					m->delay = 1;
				r->returned_delay = m->delay;
				*returned         = r->returned_delay;
				r->phase          = ST_BIG_MUSIC_CALLBACK_DONE;
				*complete         = true;
				return 0;
			default:
				return failed(r, IO_ERROR_PROTOCOL);
		}
	}
}

static void
duration(struct st_big_music_schedule *s, uint16_t divider, uint16_t count)
{
	/* Convert only a newly requested duration. The remainder belongs to the
	 * local sample grid, not a simulated foreign timer/countdown. Maximum
	 * product65536*65535*44100 fits comfortably in uint64_t. */
	uint64_t scaled = (uint64_t)(divider == 0 ? 65536u : divider) * count * ST_BIG_MUSIC_RATE + s->fraction;
	s->remaining    = scaled / UINT32_C(1193181);
	s->fraction     = (uint32_t)(scaled % UINT32_C(1193181));
}

int
st_big_music_schedule_arm(struct st_big_music_schedule *s, const struct st_big_music *m, uint16_t count)
{
	if (s == NULL || m == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*s = (struct st_big_music_schedule) {.armed = true};
	duration(s, m->divider, count);
	return 0;
}

int
st_big_music_schedule_rate(struct st_big_music_schedule *s, struct st_big_music *m, uint16_t divider)
{
	if (s == NULL || m == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (s->armed && s->remaining != 0 && !s->pending) {
		/* Preserve elapsed local playback and the remaining fraction of the
		 * requested duration. Both divider values are converted periods;
		 * there is no foreign countdown to advance. The maximum product
		 * 65536*65535*44100*65536 is below UINT64_MAX. */
		uint32_t period = divider == 0 ? 65536u : divider;
		if (s->remaining > (UINT64_MAX - s->fraction) / UINT32_C(1193181))
			return IO_ERROR_OUT_OF_RANGE;
		uint64_t scaled = s->remaining * UINT32_C(1193181) + s->fraction;
		if (scaled > UINT64_MAX / period)
			return IO_ERROR_OUT_OF_RANGE;
		scaled       = scaled * period / (m->divider == 0 ? 65536u : m->divider);
		s->remaining = scaled / UINT32_C(1193181);
		s->fraction  = (uint32_t)(scaled % UINT32_C(1193181));
	}
	m->divider = divider;
	return 0;
}

int
st_big_music_schedule_poll(struct st_big_music_schedule *s, struct st_big_music *m,
    const struct st_big_music_sink *sink, size_t *budget)
{
	if (s == NULL || m == NULL || budget == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	while (s->armed && s->remaining == 0) {
		if (!s->pending) {
			st_big_music_callback_begin(&s->callback);
			s->pending = true;
		}
		uint16_t returned = 0;
		bool     complete = false;
		int      status = st_big_music_callback_poll(&s->callback, m, sink, budget, &returned, &complete);
		if (status != 0)
			return status;
		if (!complete)
			return IO_ERROR_PROTOCOL;
		s->pending = false;
		if (m->running == 0)
			s->armed = false;
		else
			duration(s, m->divider, returned);
	}
	return 0;
}

int
st_big_music_schedule_consume(struct st_big_music_schedule *s, uint64_t frames)
{
	if (s == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!s->armed)
		return 0;
	if (frames > s->remaining)
		return IO_ERROR_OUT_OF_RANGE;
	s->remaining -= frames;
	return 0;
}

void
st_big_music_schedule_cancel(struct st_big_music_schedule *s)
{
	if (s != NULL)
		*s = (struct st_big_music_schedule) {0};
}

int
st_big_music_warm_write(unsigned index, uint8_t *address, uint8_t *value)
{
	if (address == NULL || value == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (index >= ST_BIG_MUSIC_WARM_WRITES)
		return IO_ERROR_OUT_OF_RANGE;
	static const uint8_t offset[] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21};
	uint8_t              reg = 0, data = 0;
	if (index < 135) {
		unsigned slot = 0;
		for (;;) {
			bool     carrier = (slot / 3) % 2 != 0;
			unsigned count   = carrier ? 7 : 8;
			if (index < count) {
				/* The ordinary default templates at relative volume127 pack
				 * these exact values. Raw N never changes library templates. */
				if (carrier && index >= 3)
					++index; /* No C0 request for carriers. */
				switch (index) {
					case 0:
						reg = 0xbd;
						break;
					case 1:
						reg = 8;
						break;
					case 2:
						reg  = (uint8_t)(0x40 + offset[slot]);
						data = carrier ? 0 : 0x4f;
						break;
					case 3:
						reg  = (uint8_t)(0xc0 + (slot / 6) * 3 + slot % 3);
						data = 6;
						break;
					case 4:
						reg  = (uint8_t)(0x60 + offset[slot]);
						data = carrier ? 0xf2 : 0xf1;
						break;
					case 5:
						reg  = (uint8_t)(0x80 + offset[slot]);
						data = carrier ? 0x74 : 0x53;
						break;
					case 6:
						reg  = (uint8_t)(0x20 + offset[slot]);
						data = carrier ? 0x11 : 1;
						break;
					case 7:
						reg = (uint8_t)(0xe0 + offset[slot]);
						break;
				}
				break;
			}
			index -= count;
			++slot;
		}
	}
	else if (index < 137)
		reg = 0xbd;
	else if (index == 137)
		reg = 8;
	else if (index < 156)
		reg = (uint8_t)((index % 2 ? 0xb0 : 0xa0) + (index - 138) / 2);
	else if (index < 174)
		reg = (uint8_t)(0xe0 + offset[index - 156]);
	else {
		reg  = 1;
		data = 0x20;
	}
	*address = reg;
	*value   = data;
	return 0;
}
