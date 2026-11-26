/* BIG N arithmetic, shared state, original record callback and warm writes. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_MUSIC_H_
#define ST_BIG_MUSIC_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
	ST_BIG_MUSIC_CAPACITY    = 65534,
	ST_BIG_MUSIC_WARM_WRITES = 175,
	ST_BIG_MUSIC_RATE        = 44100
};

/* Actual owned allocation, independent of the loaded-length WORD. Callers
 * initialize new storage and serialize each read/store with playback. */
struct st_big_music_buffer {
	uint8_t bytes[ST_BIG_MUSIC_CAPACITY];
};

int st_big_music_buffer_read(const struct st_big_music_buffer *, uint16_t offset, bool word, uint16_t *value);
int st_big_music_buffer_store(struct st_big_music_buffer *, uint16_t offset, uint8_t value);

struct st_big_music {
	/* One owner for X7, the raw loader/clock scratch and record progress. */
	uint16_t sync, position_low, position_high, scratch;
	uint8_t  running;
	uint16_t length, enabled, cached_divider, divider;
	uint16_t end_low, end_high, delay, address, data, value_offset;
};

uint16_t st_big_music_selector(uint16_t word);
uint16_t st_big_music_pair(uint16_t whole, uint16_t remainder, bool load_length);
uint32_t st_big_music_end(uint16_t length, bool slice, uint16_t to);
uint32_t st_big_music_start(uint32_t end, bool slice, uint16_t from);
uint32_t st_big_music_position(const struct st_big_music *);

struct st_big_music_sink {
	void *context;
	/* The native owner checks its allocated capacity, independently of
	 * loaded length. word reads one little-endian WORD at this start offset;
	 * do not silently zero-fill out-of-capacity reads. No DOS segment model.
	 * IN_PROGRESS retries the same offset without repeating prior stages. */
	int (*read)(void *, uint16_t offset, bool word, uint16_t *value);
	/* Raw value's full WORD is retained here; the OPL sink uses its low byte.
	 * IN_PROGRESS means not accepted; retry the same latched arguments. */
	int (*write)(void *, uint16_t address, uint16_t value);
};

enum st_big_music_callback_phase {
	ST_BIG_MUSIC_CALLBACK_OFF,
	ST_BIG_MUSIC_CALLBACK_ENTRY,
	ST_BIG_MUSIC_CALLBACK_POINTER,
	ST_BIG_MUSIC_CALLBACK_ADDRESS,
	ST_BIG_MUSIC_CALLBACK_DATA,
	ST_BIG_MUSIC_CALLBACK_DELAY,
	ST_BIG_MUSIC_CALLBACK_REGISTER,
	ST_BIG_MUSIC_CALLBACK_OUTPUT,
	ST_BIG_MUSIC_CALLBACK_LOW,
	ST_BIG_MUSIC_CALLBACK_HIGH,
	ST_BIG_MUSIC_CALLBACK_END,
	ST_BIG_MUSIC_CALLBACK_NEXT,
	ST_BIG_MUSIC_CALLBACK_TAIL,
	ST_BIG_MUSIC_CALLBACK_DONE,
	ST_BIG_MUSIC_CALLBACK_FAILED
};

struct st_big_music_callback {
	enum st_big_music_callback_phase phase;
	uint16_t                         output_address, output_data;
	uint16_t                         read_offset, returned_delay;
	bool                             carry, output_latched, read_latched;
	int                              error;
};

void st_big_music_callback_begin(struct st_big_music_callback *);
/* A budget bounds work per call, never the callback's total length. The
 * nonff back edge bypasses running/endpoint checks at entry. Current fields
 * are reloaded at their specified stages; a selected I/O call's arguments
 * remain latched until it returns. Callers may change state between polls;
 * callbacks must not reenter this operator. No clock/IRQ/PCM/device is owned. */
int st_big_music_callback_poll(struct st_big_music_callback *, struct st_big_music *,
    const struct st_big_music_sink *, size_t *budget, uint16_t *returned_delay, bool *complete);

/* Native local-sample schedule. Each arm converts its requested delay using
 * the current divider (zero means65536), carrying fractional samples between
 * callbacks. No PIT/IRQ/BIOS clock, debt or output-bus delay is modeled.
 * Rate changes rescale an unexpired local duration; they do not re-arm it,
 * restart a pending callback or update the cached/header divider.
 * Explicit arm replaces pending work and starts a new fractional interval.
 * It changes no protocol/chip fields; count0 is immediately due.
 *
 * Poll at a sample boundary before generating PCM. It services due original
 * callbacks until a positive span, stopped callback or work-budget boundary.
 * IN_PROGRESS preserves the accepted prefix and due time; do not consume
 * samples while it is pending. There is no total callback/cycle cap. Once
 * stopped, no redundant callbacks are needed and held chip voices persist.
 * Consume only a returned remaining span, then poll again at its endpoint.
 * Idle consumption is a no-op. Cancel changes only this local schedule.
 * Owner serializes protocol state, buffer, schedule and chip access. */
struct st_big_music_schedule {
	struct st_big_music_callback callback;
	uint64_t                     remaining;
	uint32_t                     fraction;
	bool                         armed, pending;
};

int  st_big_music_schedule_arm(struct st_big_music_schedule *, const struct st_big_music *, uint16_t count);
int  st_big_music_schedule_rate(struct st_big_music_schedule *, struct st_big_music *, uint16_t divider);
int  st_big_music_schedule_poll(struct st_big_music_schedule *, struct st_big_music *,
    const struct st_big_music_sink *, size_t *budget);
int  st_big_music_schedule_consume(struct st_big_music_schedule *, uint64_t frames);
void st_big_music_schedule_cancel(struct st_big_music_schedule *);
/* Normal protocol-reachable library state: private depth/note-select fields
 * stay zero; raw N writes do not change them. Only the 175 reached register
 * requests are retained, not unused linked pitch/instrument API tables.
 * Errors preserve outputs. Each request can be applied/resumed separately. */
int st_big_music_warm_write(unsigned index, uint8_t *address, uint8_t *value);
#endif
