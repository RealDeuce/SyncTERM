/* Checked, seekable SMF/RMID iterator; payloads are valid until next(). */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_MIDI_FILE_H_
#define ST_MIDI_FILE_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ST_MUSIC_STORAGE_LIMIT (64U * 1024U * 1024U)
struct st_midi_file;
struct st_midi_event {
	uint64_t time_ns;
	const uint8_t *bytes;
	size_t size;
};
/* Borrows FILE; validates all event lengths and times before success. */
int st_midi_file_open(struct st_midi_file **result, FILE *stream,
	bool (*cancelled)(void *), void *context);
/* Returns 0 for an event, 1 at the end (time_ns is the song duration),
 * or a negative IO_ERROR. Meta events are interpreted or skipped. */
int st_midi_file_next(struct st_midi_file *file, struct st_midi_event *event);
void st_midi_file_close(struct st_midi_file *file);
#endif
