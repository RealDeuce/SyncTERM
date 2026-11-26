/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "st_rip3_graphical_bytes.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_local_variables.h"
#include "st_rip3_mouse.h"
#include "st_rip3_playback.h"

enum {
	DECODED_LIMIT = 20
};

bool
st_rip3_playback_prefix(uint8_t prefix)
{
	return prefix == ST_RIP3_PLAYBACK_SCENE || prefix == ST_RIP3_PLAYBACK_WAVE
	       || prefix == ST_RIP3_PLAYBACK_BITMAP || prefix == ST_RIP3_PLAYBACK_JPEG;
}

size_t
st_rip3_playback_token(const uint8_t *bytes, size_t length)
{
	if (length < 3 || bytes[0] != '$' || !st_rip3_playback_prefix(bytes[1]))
		return 0;
	for (size_t i = 2; i < length && bytes[i] != 0; i++) {
		if (bytes[i] == '$' && bytes[i - 1] != '\\')
			return i + 1;
	}
	return 0;
}

static const char *
default_extension(enum st_rip3_playback_kind kind)
{
	switch (kind) {
		case ST_RIP3_PLAYBACK_SCENE:
			return ".RIP";
		case ST_RIP3_PLAYBACK_WAVE:
			return ".wav";
		case ST_RIP3_PLAYBACK_BITMAP:
			return ".BMP";
		case ST_RIP3_PLAYBACK_JPEG:
			return ".JPG";
	}
	return "";
}

bool
st_rip3_playback_name(char result[ST_RIP3_PLAYBACK_NAME_SIZE], const uint8_t *bytes, size_t length, bool converted,
    const struct st_rip3_host_variables *host)
{
	char   decoded[4 * DECODED_LIMIT + 1];
	size_t units = 0, used = 0;
	for (size_t i = 2; i < length - 1;) {
		if (bytes[i] == '$') {
			/* The token scanner has already verified this dollar is quoted. */
			decoded[used - 1] = '$';
			++i;
			continue;
		}
		if (units == DECODED_LIMIT)
			return false;
		size_t  count = converted ? st_rip3_graphical_character(bytes + i, length - 1 - i) : 1;
		uint8_t first = bytes[i];
		if (first >= 'a' && first <= 'z')
			first -= 'a' - 'A';
		decoded[used++] = (char)first;
		memcpy(decoded + used, bytes + i + 1, count - 1);
		used += count - 1;
		i += count;
		++units;
	}
	decoded[used] = 0;

	char identity[6] = "";
	if (host->field_context != NULL && host->field_context->active)
		(void)snprintf(identity, sizeof(identity), "%u",
		    (unsigned)(uint16_t)host->field_context->identity);
	bool     held      = st_rip3_mouse_press_token(host->mouse) != 0;
	unsigned modifiers = 0;
	if (host->local != NULL && host->local->modifiers != NULL && strpbrk(decoded, "[]") != NULL
	    && host->local->modifiers(host->local->context, &modifiers) != 0)
		modifiers = 0;
	size_t identity_length = strlen(identity);
	char  *out             = result;
	for (size_t i = 0; i < used; i++) {
		switch (decoded[i]) {
			case '*':
				memcpy(out, identity, identity_length);
				out += identity_length;
				break;
			case '?':
				*out++ = held ? '1' : '0';
				break;
			case '[':
				*out++ = (modifiers & ST_RIP3_MODIFIER_SHIFT) != 0 ? '1' : '0';
				break;
			case ']':
				*out++ = (modifiers & ST_RIP3_MODIFIER_CTRL) != 0 ? '1' : '0';
				break;
			default:
				*out++ = decoded[i];
				break;
		}
	}
	*out = 0;
	if (out == result)
		return true;
	enum st_rip3_playback_kind kind      = (enum st_rip3_playback_kind)bytes[1];
	char                      *extension = strrchr(result, '.');
	if (extension == NULL)
		strcpy(out, default_extension(kind));
	else if (kind == ST_RIP3_PLAYBACK_BITMAP) {
		if (strcmp(extension, ".ICN") == 0)
			memcpy(extension, ".BMP", 5);
		else if (strcmp(extension, ".HIC") == 0)
			memcpy(extension, ".BMH", 5);
		else if (strcmp(extension, ".MSK") == 0)
			memcpy(extension, ".BMM", 5);
	}
	return true;
}
