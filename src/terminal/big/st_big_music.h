/* BIG N arithmetic, shared state, original record callback and warm writes. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_MUSIC_H_
#define ST_BIG_MUSIC_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { ST_BIG_MUSIC_CAPACITY=65534, ST_BIG_MUSIC_WARM_WRITES=175 };
struct st_big_music {
	/* One owner for X7, the raw loader/clock scratch and record progress. */
	uint16_t sync, position_low, position_high, scratch;
	uint8_t running;
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
	ST_BIG_MUSIC_CALLBACK_OFF, ST_BIG_MUSIC_CALLBACK_ENTRY,
	ST_BIG_MUSIC_CALLBACK_POINTER, ST_BIG_MUSIC_CALLBACK_ADDRESS,
	ST_BIG_MUSIC_CALLBACK_DATA, ST_BIG_MUSIC_CALLBACK_DELAY,
	ST_BIG_MUSIC_CALLBACK_REGISTER, ST_BIG_MUSIC_CALLBACK_OUTPUT,
	ST_BIG_MUSIC_CALLBACK_LOW, ST_BIG_MUSIC_CALLBACK_HIGH,
	ST_BIG_MUSIC_CALLBACK_END, ST_BIG_MUSIC_CALLBACK_NEXT,
	ST_BIG_MUSIC_CALLBACK_TAIL, ST_BIG_MUSIC_CALLBACK_DONE,
	ST_BIG_MUSIC_CALLBACK_FAILED
};
struct st_big_music_callback {
	enum st_big_music_callback_phase phase;
	uint16_t output_address, output_data;
	uint16_t read_offset, returned_delay;
	bool carry, output_latched, read_latched;
	int error;
};
void st_big_music_callback_begin(struct st_big_music_callback *);
/* A budget bounds work per call, never the callback's total length. The
 * nonff back edge bypasses running/endpoint checks at entry. Current fields
 * are reloaded at their specified stages; a selected I/O call's arguments
 * remain latched until it returns. Callers may change state between polls;
 * callbacks must not reenter this operator. No clock/IRQ/PCM/device is owned. */
int st_big_music_callback_poll(struct st_big_music_callback *, struct st_big_music *,
    const struct st_big_music_sink *, size_t *budget, uint16_t *returned_delay, bool *complete);
/* Normal protocol-reachable library state: private depth/note-select fields
 * stay zero; raw N writes do not change them. Only the 175 reached register
 * requests are retained, not unused linked pitch/instrument API tables.
 * Errors preserve outputs. Each request can be applied/resumed separately. */
int st_big_music_warm_write(unsigned index, uint8_t *address, uint8_t *value);
#endif
