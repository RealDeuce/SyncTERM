/* Shared ordinary HCL playback grammar and filename preparation. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_PLAYBACK_H_
#define ST_RIP3_PLAYBACK_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_host_variables;

enum st_rip3_playback_kind {
	ST_RIP3_PLAYBACK_SCENE = '>',
	ST_RIP3_PLAYBACK_WAVE = ')',
	ST_RIP3_PLAYBACK_BITMAP = '<',
	ST_RIP3_PLAYBACK_JPEG = '(',
};

/* Twenty source bytes can become a hundred decimal digits, plus extension. */
enum { ST_RIP3_PLAYBACK_NAME_SIZE = 105 };

bool st_rip3_playback_prefix(uint8_t prefix);
/* Complete token length including delimiters, or zero without a close. */
size_t st_rip3_playback_token(const uint8_t *bytes, size_t length);
/* Prepare a completed token. False keeps the whole token literal. Successful
 * empty names are consumed without media dispatch. The caller owns result. */
bool st_rip3_playback_name(char result[ST_RIP3_PLAYBACK_NAME_SIZE],
	const uint8_t *bytes, size_t length, bool converted,
	const struct st_rip3_host_variables *host);

#endif
