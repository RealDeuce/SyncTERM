/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_tones.h"
#include "st_rip3_host_internal.h"
#include "io_error.h"

#include <stddef.h>
#include <string.h>

int
st_rip3_tones_parse(struct st_rip3_tones *result, const struct st_rip3_builtin_reference *reference)
{
	enum tone_kind {SINGLE, ALARM, MUSIC, DESCENDING, ASCENDING};
	static const struct {
		const char *name;
		enum tone_kind kind;
		unsigned minimum, maximum, defaults[4];
	} formats[] = {
		{"ALARM", ALARM, 0, 1, {3}},
		{"BEEP", SINGLE, 0, 2, {1000, 75}},
		{"BLIP", SINGLE, 0, 2, {1000, 25}},
		{"MUSIC", MUSIC, 0, 1, {4}},
		{"PHASER", DESCENDING, 0, 4, {2500, 50, 20, 2}},
		{"REVPHASER", ASCENDING, 0, 4, {50, 2500, 20, 2}},
		{"T", SINGLE, 2, 2, {0}},
	};
	size_t index = 0;
	while (index < sizeof(formats) / sizeof(*formats) && strcmp(reference->name, formats[index].name) != 0)
		index++;
	if (index == sizeof(formats) / sizeof(*formats))
		return IO_ERROR_NOT_SUPPORTED;
	if (reference->count < formats[index].minimum || reference->count > formats[index].maximum)
		return IO_ERROR_INVALID_FORMAT;
	unsigned args[4];
	memcpy(args, formats[index].defaults, sizeof(args));
	for (size_t i = 0; i < reference->count; i++) {
		int status = st_rip3_host_number(reference->parameters[i].text, 0, UINT16_MAX, &args[i]);
		if (status != 0)
			return status;
	}

	struct st_rip3_tones tones = {.count = 1, .repeats = 1};
	switch (formats[index].kind) {
		case ALARM:
			tones = (struct st_rip3_tones){
				.notes = {{320, 200}, {160, 225}}, .count = 2, .repeats = (uint16_t)args[0],
			};
			break;
		case MUSIC:
			tones = (struct st_rip3_tones){
				.notes = {{1125, 17}, {775, 14}, {625, 10}, {725, 14}, {825, 10}, {600, 25}},
				.count = 6, .repeats = (uint16_t)args[0],
			};
			break;
		case DESCENDING:
		case ASCENDING: {
			bool descending = formats[index].kind == DESCENDING;
			if ((descending ? args[0] <= args[1] : args[0] >= args[1]) || args[2] == 0 || args[3] == 0)
				return IO_ERROR_INVALID_FORMAT;
			tones.frequency = (uint16_t)args[0];
			tones.stop = (uint16_t)args[1];
			tones.step = descending ? -(int32_t)args[2] : (int32_t)args[2];
			tones.notes[0] = (struct st_rip3_tone){tones.frequency, (uint16_t)args[3]};
			break;
		}
		case SINGLE:
			tones.notes[0] = (struct st_rip3_tone){(uint16_t)args[0], (uint16_t)args[1]};
			break;
	}
	*result = tones;
	return 0;
}

bool
st_rip3_tones_next(struct st_rip3_tones *tones, struct st_rip3_tone *note)
{
	if (tones->repeats == 0)
		return false;
	*note = tones->notes[tones->index];
	if (tones->step != 0) {
		note->frequency = tones->frequency;
		uint16_t next = (uint16_t)((int32_t)tones->frequency + tones->step);
		/* Repetition of the start proves the modular sequence will cycle. */
		if (next == tones->notes[0].frequency || (tones->step < 0 ? next < tones->stop : next > tones->stop))
			tones->repeats = 0;
		tones->frequency = next;
	}
	else if (++tones->index == tones->count) {
		tones->index = 0;
		tones->repeats--;
	}
	return true;
}
