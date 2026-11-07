/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_music.h"
#include "io_error.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }
static bool digit(char c) { return c >= '0' && c <= '9'; }
static int number(const char **cursor)
{
	int value = 0;
	while (digit(**cursor)) {
		int d = *(*cursor)++ - '0';
		value = value > (INT_MAX - d) / 10 ? INT_MAX : value * 10 + d;
	}
	return value;
}
static int clamp(int n, int low, int high) { return n < low ? low : n > high ? high : n; }
static bool mode(struct st_ansi_music_state *s, int c)
{
	switch (upper(c)) {
	case 'F': s->foreground = true; return true;
	case 'B': s->foreground = false; return true;
	case 'N': s->shape = 0; return true;
	case 'L': s->shape = 1; return true;
	case 'S': s->shape = 2; return true;
	default: return false;
	}
}
void st_ansi_music_init(struct st_ansi_music_state *s)
{
	*s = (struct st_ansi_music_state){120, 4, 4, 0, true};
}
double st_music_note_frequency(int pitch)
{
	return pitch < 0 || pitch > 83 ? 0 : 440.0 * exp2((pitch - 33) / 12.0);
}
int st_ansi_music_parse(const char *text, bool prefix_modes,
	struct st_ansi_music_state *state, struct st_music_note **result,
	size_t *count, bool *foreground)
{
	if (text == NULL || state == NULL || result == NULL || count == NULL || foreground == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (state->tempo < 32 || state->tempo > 255 || state->length < 1 || state->length > 64
	    || state->octave < 0 || state->octave > 6 || state->shape < 0 || state->shape > 2)
		return IO_ERROR_INVALID_ARGUMENT;
	static const char allowed[] = "aAbBcCdDeEfFgGlLmMnNoOpPsStT0123456789.-+#<> ";
	static const char names[] = "C#D#EF#G#A#B";
	struct st_ansi_music_state s = *state;
	size_t length = strlen(text), used = 0;
	struct st_music_note *notes;
	bool fore = false;
	const char *p = text;
	if (length > 1023 || strspn(text, allowed) != length)
		return IO_ERROR_INVALID_FORMAT;
	notes = malloc((length != 0 ? length : 1) * sizeof(*notes));
	if (notes == NULL) return IO_ERROR_OUT_OF_MEMORY;
	if (prefix_modes && *p != 0 && (upper(*p) != 'N' || !digit(p[1])) && mode(&s, *p)) ++p;
	while (*p != 0) {
		int c = upper(*p++);
		if (c == 'M') {
			if (*p != 0 && mode(&s, *p)) ++p;
		} else if (c == 'T') s.tempo = clamp(number(&p), 32, 255);
		else if (c == 'O') s.octave = clamp(number(&p), 0, 6);
		else if (c == 'L') s.length = clamp(number(&p), 1, 64);
		else if (c == '<') s.octave = clamp(s.octave - 1, 0, 6);
		else if (c == '>') s.octave = clamp(s.octave + 1, 0, 6);
		else if (c == 'N' || c == 'P' || (c >= 'A' && c <= 'G')) {
			int pitch, offset = 0, len = s.length, dotted = 0;
			if (c == 'N') pitch = number(&p);
			else if (c == 'P') pitch = -1;
			else pitch = s.octave * 12 + (int)(strchr(names, c) - names);
			for (;;) {
				if (*p == '+' || *p == '#') { ++offset; ++p; }
				else if (*p == '-') { --offset; ++p; }
				else if (*p == '.') { dotted = 1; ++p; }
				else if (digit(*p)) len = number(&p);
				else break;
			}
			if (c == 'P' || (int64_t)pitch + offset < 0 || (int64_t)pitch + offset > 83) pitch = -1;
			else pitch += offset;
			int duration = (dotted ? 360000 : 240000) / s.tempo / clamp(len, 1, 64);
			int pause = s.shape == 1 ? 0 : duration / (s.shape == 2 ? 4 : 8);
			notes[used++] = (struct st_music_note){pitch,
			    AUDIO_SAMPLE_RATE * (unsigned)(duration - pause) / 1000,
			    AUDIO_SAMPLE_RATE * (unsigned)pause / 1000};
			fore |= s.foreground;
		}
	}
	*state = s;
	*result = notes;
	*count = used;
	*foreground = fore;
	return 0;
}
