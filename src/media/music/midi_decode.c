/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "music_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool reset_message(const uint8_t *b, size_t n)
{
	if (n == 6 && b[1] == 0x7e && b[3] == 9 && b[4] >= 1 && b[4] <= 3) return true;
	if (n == 11 && b[1] == 0x41 && b[3] == 0x42 && b[4] == 0x12
	    && b[5] == 0x40 && b[6] == 0 && b[7] == 0x7f && b[8] == 0 && b[9] == 0x41) return true;
	return n == 9 && b[1] == 0x43 && (b[2] & 0xf0) == 0x10 && b[3] == 0x4c
	    && b[4] == 0 && b[5] == 0 && b[6] == 0x7e && b[7] == 0;
}
static int controller(struct st_midi_sink *s, const uint8_t *bytes,
	st_midi_emit_t emit, void *context, bool force)
{
	uint8_t message[3]; memcpy(message, bytes, sizeof(message));
	unsigned channel = message[0] & 15;
	double left = pow(10.0, s->left_db / 20.0), right = pow(10.0, s->right_db / 20.0);
	if (message[1] == 7) {
		s->volume[channel] = message[2];
		double angle = s->pan[channel] * 1.57079632679489661923 / 127;
		double gain = hypot(cos(angle) * left, sin(angle) * right);
		double value = message[2] * sqrt(gain);
		message[2] = value >= 127 ? 127 : (uint8_t)(value + 0.5);
	} else if (message[1] == 10) {
		s->pan[channel] = message[2];
		double angle = message[2] * 1.57079632679489661923 / 127;
		double value = atan2(sin(angle) * right, cos(angle) * left) * 127 / 1.57079632679489661923;
		message[2] = value < 0 ? 0 : value > 127 ? 127 : (uint8_t)(value + 0.5);
	}
	unsigned flag = message[1] == 7 ? 1 : 2;
	uint8_t *last = message[1] == 7 ? s->sent_volume : s->sent_pan;
	if (!force && (s->gain_sent[channel] & flag) && last[channel] == message[2]) return 0;
	int status = emit(context, message, 3);
	if (status == 0) { last[channel] = message[2]; s->gain_sent[channel] |= flag; }
	if (status == 0 && force && message[1] == 10) {
		/* Changing the file pan also changes attenuation under unequal
		 * stream gains (including a hard-panned, muted side). */
		uint8_t volume[] = {(uint8_t)(0xb0 | channel), 7, s->volume[channel]};
		status = controller(s, volume, emit, context, false);
	}
	return status;
}
int st_midi_gain_controllers(struct st_midi_sink *s, float left, float right,
	st_midi_emit_t emit, void *context)
{
	s->left_db = left; s->right_db = right;
	for (unsigned ch = 0; ch < 16; ++ch) {
		uint8_t message[3] = {(uint8_t)(0xb0 | ch), 7, s->volume[ch]};
		int status = controller(s, message, emit, context, false);
		if (status != 0) return status;
		message[1] = 10; message[2] = s->pan[ch];
		status = controller(s, message, emit, context, false);
		if (status != 0) return status;
	}
	return 0;
}
static int decode(struct st_midi_sink *s, const uint8_t *bytes,
	size_t size, st_midi_emit_t emit, void *context, bool stream)
{
	for (size_t i = 0; i < size; ++i) {
		uint8_t b = bytes[i];
		if (b >= 0xf8) {
			int status = emit(context, &b, 1); if (status != 0) return status;
			continue;
		}
		if (s->sysex_size != 0) {
			if (b >= 0x80 && b != 0xf7) {
				if (!stream) return IO_ERROR_INVALID_FORMAT;
				s->sysex_size = 0;
			} else {
				if (s->sysex_size == ST_MUSIC_STORAGE_LIMIT / 2) return IO_ERROR_MESSAGE_TOO_LARGE;
				if (!s->validate_only && s->sysex_size == s->sysex_capacity) {
					size_t cap = s->sysex_capacity * 2;
					uint8_t *p = realloc(s->sysex, cap);
					if (p == NULL) return IO_ERROR_OUT_OF_MEMORY;
					s->sysex = p; s->sysex_capacity = cap;
				}
				if (!s->validate_only) s->sysex[s->sysex_size] = b;
				++s->sysex_size;
				if (b == 0xf7) {
					bool reset = !s->validate_only && reset_message(s->sysex, s->sysex_size);
					int status = s->validate_only ? 0 : emit(context, s->sysex, s->sysex_size);
					s->sysex_size = 0;
					if (status != 0) return status;
					/* GM/GS/XG resets restore file controllers as well as
					 * programs. Other SysEx remains untouched. */
					if (reset && s->gain == NULL && s->render == NULL) {
						memset(s->volume, 100, sizeof(s->volume));
						memset(s->pan, 64, sizeof(s->pan));
						memset(s->gain_sent, 0, sizeof(s->gain_sent));
						status = st_midi_gain_controllers(s, s->left_db, s->right_db, emit, context);
						if (status != 0) return status;
					}
				}
				continue;
			}
		}
		if (b == 0xf0) {
			if (s->used != 0 && !stream) return IO_ERROR_INVALID_FORMAT;
			s->status = 0; s->used = 0;
			if (!s->validate_only && s->sysex_capacity == 0) {
				s->sysex = malloc(256);
				if (s->sysex == NULL) return IO_ERROR_OUT_OF_MEMORY;
				s->sysex_capacity = 256;
			}
			if (!s->validate_only) s->sysex[0] = b;
			s->sysex_size = 1;
			continue;
		}
		if (b >= 0x80) {
			if (s->used != 0 && !stream) return IO_ERROR_INVALID_FORMAT;
			if (stream && (b == 0xf4 || b == 0xf5 || b == 0xf7)) {
				s->status = 0; s->used = 0;
				continue;
			}
			s->status = b; s->message[0] = b; s->used = 1;
			s->needed = b < 0xf0 ? ((b & 0xe0) == 0xc0 ? 2 : 3)
			    : b == 0xf1 || b == 0xf3 ? 2 : b == 0xf2 ? 3 : 1;
		} else {
			if (s->status == 0) {
				if (stream) continue;
				return IO_ERROR_INVALID_FORMAT;
			}
			if (s->used == 0) { s->message[0] = s->status; s->used = 1; }
			s->message[s->used++] = b;
		}
		if (s->used == s->needed) {
			int status;
			if (s->render == NULL && s->gain == NULL && (s->message[0] & 0xf0) == 0xb0
			    && (s->message[1] == 7 || s->message[1] == 10))
				status = controller(s, s->message, emit, context, true);
			else status = emit(context, s->message, s->needed);
			if (status != 0) return status;
			s->used = 0;
			if (s->status >= 0xf0) s->status = 0;
		}
	}
	return 0;
}

int st_midi_decode(struct st_midi_sink *s, const uint8_t *bytes,
	size_t size, st_midi_emit_t emit, void *context)
{
	return decode(s, bytes, size, emit, context, false);
}
int st_midi_decode_stream(struct st_midi_sink *s, const uint8_t *bytes,
	size_t size, st_midi_emit_t emit, void *context)
{
	return decode(s, bytes, size, emit, context, true);
}
