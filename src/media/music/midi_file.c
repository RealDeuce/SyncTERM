/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include "filesystem.h"
#include "io_error.h"
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum event_kind { CHANNEL, SYSEX, ESCAPE, TEMPO, META };
struct track {
	int64_t start, end, pos, payload;
	uint64_t tick;
	uint32_t length, tempo;
	uint8_t status, bytes[3];
	size_t short_size;
	enum event_kind kind;
};
struct st_midi_file {
	FILE *stream;
	struct track *tracks;
	size_t *heap, count, heap_count;
	uint16_t division;
	uint64_t tick, time, remainder;
	uint32_t tempo;
	uint8_t short_bytes[3], *payload;
	size_t capacity, budget;
	bool (*cancelled)(void *);
	void *context;
	struct st_midi_sink *validator;
};
static int validate_message(void *unused, const uint8_t *bytes, size_t size)
{
	(void)unused; (void)bytes; (void)size; return 0;
}
static uint32_t be32(const uint8_t *b)
{
	return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}
static uint32_t le32(const uint8_t *b)
{
	return (uint32_t)b[3] << 24 | (uint32_t)b[2] << 16 | (uint32_t)b[1] << 8 | b[0];
}
static int read_at(FILE *stream, int64_t pos, void *bytes, size_t size)
{
	int status = fs_stream_seek(stream, pos);
	if (status != 0) return status;
	return fread(bytes, 1, size, stream) == size ? 0 : IO_ERROR_INVALID_FORMAT;
}
static int byte(struct st_midi_file *file, struct track *track)
{
	if (track->pos >= track->end) return -1;
	++track->pos;
	return getc(file->stream);
}
static int vlq(struct st_midi_file *f, struct track *t, uint32_t *result)
{
	uint32_t value = 0;
	for (unsigned i = 0; i < 4; ++i) {
		int b = byte(f, t);
		if (b < 0) return IO_ERROR_INVALID_FORMAT;
		value = value << 7 | (unsigned)(b & 127);
		if ((b & 128) == 0) { *result = value; return 0; }
	}
	return IO_ERROR_INVALID_FORMAT;
}
static int peek(struct st_midi_file *f, struct track *t)
{
	uint32_t delta;
	int c;
	if (t->pos == t->end) return 1;
	if (fs_stream_seek(f->stream, t->pos) != 0 || vlq(f, t, &delta) != 0)
		return IO_ERROR_INVALID_FORMAT;
	if (t->tick > UINT64_MAX - delta) return IO_ERROR_OUT_OF_RANGE;
	t->tick += delta;
	c = byte(f, t);
	if (c < 0) return IO_ERROR_INVALID_FORMAT;
	t->short_size = 0;
	if (c < 128 || c < 0xf0) {
		int first = -1;
		if (c < 128) {
			if (t->status == 0) return IO_ERROR_INVALID_FORMAT;
			first = c;
			c = t->status;
		} else t->status = (uint8_t)c;
		t->kind = CHANNEL;
		t->bytes[0] = (uint8_t)c;
		t->short_size = (c & 0xe0) == 0xc0 ? 2 : 3;
		for (size_t i = 1; i < t->short_size; ++i) {
			int data = i == 1 && first >= 0 ? first : byte(f, t);
			if (data < 0 || data >= 128) return IO_ERROR_INVALID_FORMAT;
			t->bytes[i] = (uint8_t)data;
		}
		return 0;
	}
	t->status = 0;
	if (c == 0xff) {
		int type = byte(f, t);
		if (type < 0 || vlq(f, t, &t->length) != 0 || t->length > (uint64_t)(t->end - t->pos))
			return IO_ERROR_INVALID_FORMAT;
		t->kind = META;
		if (type == 0x51) {
			uint8_t tempo[3];
			if (t->length != 3 || fread(tempo, 1, 3, f->stream) != 3)
				return IO_ERROR_INVALID_FORMAT;
			t->tempo = (uint32_t)tempo[0] << 16 | (uint32_t)tempo[1] << 8 | tempo[2];
			if (t->tempo == 0) return IO_ERROR_INVALID_FORMAT;
			t->kind = TEMPO;
		}
		if (type == 0x2f) {
			if (t->length != 0) return IO_ERROR_INVALID_FORMAT;
			t->pos = t->end;
		} else t->pos += t->length;
		return 0;
	}
	if (c != 0xf0 && c != 0xf7) return IO_ERROR_INVALID_FORMAT;
	if (vlq(f, t, &t->length) != 0 || t->length > (uint64_t)(t->end - t->pos))
		return IO_ERROR_INVALID_FORMAT;
	t->kind = c == 0xf0 ? SYSEX : ESCAPE;
	t->payload = t->pos;
	t->pos += t->length;
	return 0;
}
static bool earlier(const struct st_midi_file *f, size_t a, size_t b)
{
	return f->tracks[a].tick < f->tracks[b].tick
	    || (f->tracks[a].tick == f->tracks[b].tick && a < b);
}
static void down(struct st_midi_file *f, size_t pos)
{
	for (;;) {
		size_t child = pos * 2 + 1;
		if (child >= f->heap_count) return;
		if (child + 1 < f->heap_count && earlier(f, f->heap[child + 1], f->heap[child])) ++child;
		if (!earlier(f, f->heap[child], f->heap[pos])) return;
		size_t temp = f->heap[pos]; f->heap[pos] = f->heap[child]; f->heap[child] = temp;
		pos = child;
	}
}
static int rewind_file(struct st_midi_file *f)
{
	f->tick = f->time = f->remainder = 0;
	f->tempo = 500000;
	f->heap_count = 0;
	for (size_t i = 0; i < f->count; ++i) {
		struct track *t = &f->tracks[i];
		t->pos = t->start; t->tick = 0; t->status = 0;
		int status = peek(f, t);
		if (status < 0) return status;
		if (status == 0) f->heap[f->heap_count++] = i;
	}
	for (size_t i = f->heap_count / 2; i > 0; --i) down(f, i - 1);
	return 0;
}
static int advance(struct st_midi_file *f, uint64_t tick)
{
	uint64_t numerator, divisor;
	if (f->division & 0x8000) {
		unsigned fps = 256 - (f->division >> 8);
		numerator = fps == 29 ? UINT64_C(1001000000000) : UINT64_C(1000000000);
		divisor = (fps == 29 ? 30000 : fps) * (f->division & 255);
	} else { numerator = (uint64_t)f->tempo * 1000; divisor = f->division; }
	uint64_t ticks = tick - f->tick, whole = ticks / divisor;
	uint64_t fraction = ticks % divisor * numerator + f->remainder;
	if (whole > (UINT64_MAX - f->time) / numerator) return IO_ERROR_OUT_OF_RANGE;
	f->time += whole * numerator;
	if (fraction / divisor > UINT64_MAX - f->time) return IO_ERROR_OUT_OF_RANGE;
	f->time += fraction / divisor;
	f->remainder = fraction % divisor;
	f->tick = tick;
	return 0;
}
int st_midi_file_next(struct st_midi_file *f, struct st_midi_event *event)
{
	while (f->heap_count != 0) {
		if (f->cancelled != NULL && f->cancelled(f->context)) return IO_ERROR_CANCELLED;
		struct track *t = &f->tracks[f->heap[0]];
		int status = advance(f, t->tick);
		if (status != 0) return status;
		event->time_ns = f->time;
		event->size = 0;
		if (t->kind == TEMPO) f->tempo = t->tempo;
		if (t->kind == CHANNEL) {
			memcpy(f->short_bytes, t->bytes, t->short_size);
			event->bytes = f->short_bytes; event->size = t->short_size;
			if (f->validator != NULL) {
				status = st_midi_decode(f->validator, event->bytes, event->size, validate_message, NULL);
				if (status != 0) return status;
			}
		} else if (t->kind == SYSEX || t->kind == ESCAPE) {
			size_t prefix = t->kind == SYSEX ? 1 : 0;
			size_t size = (size_t)t->length + prefix;
			if (size > f->budget) return IO_ERROR_MESSAGE_TOO_LARGE;
			if (f->validator != NULL) {
				/* Validation walks bytes without constructing a payload or
				 * retaining a second SysEx copy that will never be sent. */
				const uint8_t start = 0xf0;
				if (prefix) status = st_midi_decode(f->validator, &start, 1, validate_message, NULL);
				if (status != 0) return status;
				uint8_t block[4096];
				for (size_t at = 0; at < t->length;) {
					size_t count = t->length - at;
					if (count > sizeof(block)) count = sizeof(block);
					if (f->cancelled != NULL && f->cancelled(f->context)) return IO_ERROR_CANCELLED;
					status = read_at(f->stream, t->payload + (int64_t)at, block, count);
					if (status == 0) status = st_midi_decode(f->validator, block, count, validate_message, NULL);
					if (status != 0) return status;
					at += count;
				}
				event->bytes = NULL; event->size = size;
			} else {
			if (size > f->capacity) {
				uint8_t *data = realloc(f->payload, size);
				if (data == NULL) return IO_ERROR_OUT_OF_MEMORY;
				f->payload = data; f->capacity = size;
			}
			if (prefix) f->payload[0] = 0xf0;
			if (t->length && read_at(f->stream, t->payload, f->payload + prefix, t->length) != 0)
				return IO_ERROR_INVALID_FORMAT;
			event->bytes = f->payload; event->size = size;
			}
		}
		status = peek(f, t);
		if (status < 0) return status;
		if (status == 1) f->heap[0] = f->heap[--f->heap_count];
		down(f, 0);
		if (event->size != 0) return 0;
	}
	event->time_ns = f->time; event->size = 0; event->bytes = NULL;
	return 1;
}
void st_midi_file_close(struct st_midi_file *f)
{
	if (f == NULL) return;
	free(f->tracks); free(f->heap); free(f->payload); free(f);
}
int st_midi_file_open(struct st_midi_file **result, FILE *stream,
	bool (*cancelled)(void *), void *context)
{
	uint8_t header[14];
	int64_t size = fs_stream_size(stream), start = 0, end = size;
	int status = IO_ERROR_INVALID_FORMAT;
	struct st_midi_file *f = NULL;
	if (size < 14 || read_at(stream, 0, header, 12) != 0) return status;
	if (memcmp(header, "RIFF", 4) == 0) {
		if (memcmp(header + 8, "RMID", 4) != 0 || le32(header + 4) < 4
		    || (uint64_t)le32(header + 4) + 8 > (uint64_t)size) return status;
		end = (int64_t)le32(header + 4) + 8;
		for (start = 12; start <= end - 8;) {
			if (cancelled != NULL && cancelled(context)) return IO_ERROR_CANCELLED;
			if (read_at(stream, start, header, 8) != 0) return status;
			uint32_t len = le32(header + 4);
			if (len > (uint64_t)(end - start - 8)) return status;
			if (memcmp(header, "data", 4) == 0) { start += 8; end = start + len; break; }
			start += 8 + (int64_t)len + (len & 1);
		}
	}
	if (end - start < 14 || read_at(stream, start, header, 14) != 0
	    || memcmp(header, "MThd", 4) != 0) return status;
	uint32_t len = be32(header + 4);
	unsigned format = (unsigned)header[8] << 8 | header[9];
	size_t count = (size_t)header[10] << 8 | header[11];
	uint16_t division = (uint16_t)((unsigned)header[12] << 8 | header[13]);
	if (format > 1) return IO_ERROR_NOT_SUPPORTED;
	if (len < 6 || len > (uint64_t)(end - start - 8) || count == 0
	    || (format == 0 && count != 1) || division == 0) return status;
	if (division & 0x8000) {
		unsigned fps = 256 - (division >> 8);
		if ((division & 255) == 0 || (fps != 24 && fps != 25 && fps != 29 && fps != 30)) return status;
	}
	f = calloc(1, sizeof(*f));
	if (f == NULL) return IO_ERROR_OUT_OF_MEMORY;
	f->count = count; f->stream = stream; f->division = division;
	f->cancelled = cancelled; f->context = context;
	f->budget = ST_MUSIC_STORAGE_LIMIT / 2 - count * (sizeof(*f->tracks) + sizeof(*f->heap));
	f->tracks = calloc(count, sizeof(*f->tracks));
	f->heap = malloc(count * sizeof(*f->heap));
	if (f->tracks == NULL || f->heap == NULL) { status = IO_ERROR_OUT_OF_MEMORY; goto failed; }
	start += 8 + len;
	for (size_t i = 0; i < count;) {
		if (cancelled != NULL && cancelled(context)) { status = IO_ERROR_CANCELLED; goto failed; }
		if (start > end - 8 || read_at(stream, start, header, 8) != 0) goto failed;
		len = be32(header + 4);
		if (len > (uint64_t)(end - start - 8)) goto failed;
		if (memcmp(header, "MTrk", 4) == 0) {
			f->tracks[i].start = start + 8;
			f->tracks[i++].end = start + 8 + len;
		}
		start += 8 + (int64_t)len;
	}
	status = rewind_file(f);
	if (status != 0) goto failed;
	struct st_midi_event event;
	struct st_midi_sink decoder = {.validate_only = true};
	f->validator = &decoder;
	while ((status = st_midi_file_next(f, &event)) == 0) {}
	f->validator = NULL;
	if (status == 1 && (decoder.sysex_size != 0 || decoder.used != 0)) status = IO_ERROR_INVALID_FORMAT;
	free(decoder.sysex);
	if (status != 1) goto failed;
	status = rewind_file(f);
	if (status != 0) goto failed;
	*result = f;
	return 0;
failed:
	st_midi_file_close(f);
	return status;
}
