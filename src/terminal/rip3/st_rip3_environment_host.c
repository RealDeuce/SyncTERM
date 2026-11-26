/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_environments.h"
#include "st_rip3_host_internal.h"

static bool
equal(const char *text, const char *uppercase)
{
	for (size_t i = 0;; i++) {
		unsigned char ch = (unsigned char)text[i];
		if (ch >= 'a' && ch <= 'z')
			ch = (unsigned char)(ch - 'a' + 'A');
		if (ch != (unsigned char)uppercase[i])
			return false;
		if (ch == 0)
			return true;
	}
}

static int
decimal_number(const char *text, uint64_t *result)
{
	uint64_t value = 0;

	if (text[0] == 0)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; text[i] != 0; i++) {
		unsigned digit = (unsigned char)text[i] - (unsigned)'0';

		if (digit > 9 || value > UINT64_MAX / 10 || (value == UINT64_MAX / 10 && digit > UINT64_MAX % 10))
			return IO_ERROR_INVALID_FORMAT;
		value = value * 10 + digit;
	}
	*result = value;
	return 0;
}

static int
reply_text(char *result, size_t capacity, const char *text)
{
	size_t length = strlen(text);

	if (length >= capacity)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(result, text, length + 1);
	return 0;
}

static int
charset_keyword(const char *text, enum st_rip3_charset *charset)
{
	if (equal(text, "USASCII"))
		*charset = ST_RIP3_CHARSET_USASCII;
	else if (equal(text, "KANJI"))
		*charset = ST_RIP3_CHARSET_KANJI;
	else if (equal(text, "OFF"))
		*charset = ST_RIP3_CHARSET_UNSET;
	else
		return IO_ERROR_INVALID_FORMAT;
	return 0;
}

static int
charset_metadata(const struct st_rip3_host_variables *host, const struct st_rip3_builtin_reference *reference,
    char *result, size_t capacity)
{
	bool shared = strcmp(reference->name, "SDCS") == 0;
	bool stored = strcmp(reference->name, "DCS") == 0;
	if (reference->count > (stored ? 2u : 1u))
		return IO_ERROR_INVALID_FORMAT;
	if (capacity == 0)
		return IO_ERROR_OUT_OF_RANGE;
	if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_ENV].live == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_builtin_reference words;
	int                              status = st_rip3_builtin_reference_words(&words, reference);
	if (status != 0)
		return status;
	struct st_rip3_environments *environments = host->tables->tables[ST_RIP3_TABLE_ENV].live;
	enum st_rip3_charset         charset      = st_rip3_environments_shared_charset(environments);
	if (shared) {
		if (words.count != 0 && !equal(words.parameters[0].text, "QUERY")) {
			status = charset_keyword(words.parameters[0].text, &charset);
			if (status != 0)
				return status;
			st_rip3_environments_set_shared_charset(environments, charset);
			result[0] = 0;
			return 0;
		}
	}
	else {
		unsigned                   index;
		struct st_rip3_environment value;
		if (words.count == 0 || equal(words.parameters[0].text, "CUR"))
			status = st_rip3_environments_active(environments, &index, &value);
		else {
			status = st_rip3_host_number(words.parameters[0].text, 0, 35, &index);
			if (status == 0)
				status = st_rip3_environments_get(environments, index, &value);
		}
		if (status == IO_ERROR_NOT_FOUND)
			return reply_text(result, capacity, "-1");
		if (status != 0)
			return status;
		if (words.count == 2 && !equal(words.parameters[1].text, "QUERY")) {
			status = charset_keyword(words.parameters[1].text, &charset);
			if (status != 0)
				return status;
			st_rip3_environments_set_charset(environments, index, charset);
			result[0] = 0;
			return 0;
		}
		if (stored || value.charset != ST_RIP3_CHARSET_UNSET)
			charset = value.charset;
	}
	if (charset == ST_RIP3_CHARSET_UNSET && (shared || stored))
		return reply_text(result, capacity, shared ? "-1" : "0");
	return reply_text(result, capacity, charset == ST_RIP3_CHARSET_KANJI ? "KANJI" : "USASCII");
}

static int
playback_rate(const struct st_rip3_host_variables *host, const struct st_rip3_builtin_reference *reference,
    char *result, size_t capacity)
{
	if (reference->count > 1)
		return IO_ERROR_INVALID_FORMAT;
	if (capacity == 0)
		return IO_ERROR_OUT_OF_RANGE;
	if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_ENV].live == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_environments *environments = host->tables->tables[ST_RIP3_TABLE_ENV].live;
	if (reference->count == 0) {
		char text[sizeof("2147483647")];
		(void)snprintf(text, sizeof(text), "%" PRIu32, st_rip3_environments_playback_rate(environments));
		return reply_text(result, capacity, text);
	}
	uint64_t rate;
	int      status = decimal_number(reference->parameters[0].text, &rate);
	if (status != 0 || rate > INT32_MAX)
		return IO_ERROR_INVALID_FORMAT;
	result[0] = 0;
	return st_rip3_environments_set_playback_rate(environments, (uint32_t)rate);
}

int
st_rip3_environment_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	struct st_rip3_environments *environments;
	struct st_rip3_environment   value;

	if (host == NULL || reference == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (strcmp(reference->name, "MCURSOR") == 0) {
		if (reference->count > 1)
			return IO_ERROR_INVALID_FORMAT;
		if (capacity == 0)
			return IO_ERROR_OUT_OF_RANGE;
		unsigned pointer = 0;
		int      status  = 0;
		if (reference->count != 0)
			status = st_rip3_host_number(reference->parameters[0].text, 0, 7, &pointer);
		if (status != 0)
			return status;
		if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_ENV].live == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		status = st_rip3_environments_mouse(host->tables->tables[ST_RIP3_TABLE_ENV].live, pointer);
		if (status == 0)
			result[0] = 0;
		return status;
	}
	const char *name = reference->name;
	if (strcmp(name, "BAUDEMUL") == 0)
		return playback_rate(host, reference, result, capacity);
	if (strcmp(name, "CHARSET") == 0 || strcmp(name, "DCS") == 0 || strcmp(name, "SDCS") == 0)
		return charset_metadata(host, reference, result, capacity);
	bool     base   = strcmp(name, "BASEMATH") == 0;
	bool     coord  = strcmp(name, "COORDSIZE") == 0;
	bool     world  = strcmp(name, "WORLD") == 0;
	bool     width  = strcmp(name, "WORLDW") == 0;
	bool     height = strcmp(name, "WORLDH") == 0;
	bool     color  = strcmp(name, "COLORMODE") == 0;
	bool     compat = strcmp(name, "COMPAT") == 0;
	unsigned index;
	int      status;

	if (!base && !coord && !world && !width && !height && !color && !compat)
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_builtin_reference words;
	status = st_rip3_builtin_reference_words(&words, reference);
	if (status != 0)
		return status;
	reference = &words;
	if (capacity == 0)
		return IO_ERROR_OUT_OF_RANGE;
	if (reference->count > (compat ? 1u : world || color ? 3u : 2u))
		return IO_ERROR_INVALID_FORMAT;
	/* The shipped two-argument axis setters invert their target test:
	 * literal CUR errors; every other target text writes the current ENV.
	 * Getter forms still validate and honor the requested numeric slot. */
	bool axis_setting = (width || height) && reference->count == 2;
	if (axis_setting && equal(reference->parameters[0].text, "CUR"))
		return IO_ERROR_INVALID_FORMAT;
	bool active = axis_setting || reference->count == 0 || equal(reference->parameters[0].text, "CUR");
	if (!active) {
		status = st_rip3_host_number(reference->parameters[0].text, 0, 35, &index);
		if (status != 0)
			return status;
	}
	if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_ENV].live == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	environments = host->tables->tables[ST_RIP3_TABLE_ENV].live;
	if (compat) {
		status = st_rip3_environments_compat(environments, active ? 0 : index, active);
		if (status == 0)
			result[0] = 0;
		return status;
	}
	bool setting = reference->count >= 2;
	status       = active ? st_rip3_environments_active(environments, &index, &value)
	                      : st_rip3_environments_get(environments, index, &value);
	if (status == IO_ERROR_NOT_FOUND) {
		if (setting)
			return IO_ERROR_INVALID_FORMAT;
		return reply_text(result, capacity, base ? "NONE" : "-1");
	}
	if (status != 0)
		return status;
	if (!setting) {
		char text[48];
		int  length;

		if (world)
			length = snprintf(text, sizeof(text), "%" PRIu32 ":%" PRIu32, value.world_width,
			    value.world_height);
		else {
			uint32_t number = base     ? value.base_math
			                  : coord  ? value.coordinate_size
			                  : width  ? value.world_width
			                  : height ? value.world_height
			                           : value.rgb_bits;
			length          = snprintf(text, sizeof(text), "%" PRIu32, number);
		}
		if (length < 0 || (size_t)length >= sizeof(text))
			return IO_ERROR_IO;
		return reply_text(result, capacity, text);
	}
	if (color) {
		const char *mode = reference->parameters[1].text;
		unsigned    bits;

		if (equal(mode, "PAL") && reference->count == 2)
			value.rgb_bits = 0;
		else if (equal(mode, "RGB") && reference->count == 3) {
			status = st_rip3_host_number(reference->parameters[2].text, 1, 8, &bits);
			if (status != 0)
				return status;
			value.rgb_bits = bits;
		}
		else
			return IO_ERROR_INVALID_FORMAT;
	}
	else if (world) {
		uint64_t x, y;

		if (reference->count == 2 && equal(reference->parameters[1].text, "DEFAULT")) {
			value.world_width  = value.base_math == 36 ? 1280 : 4096;
			value.world_height = value.base_math == 36 ? 960 : 3072;
		}
		else if (reference->count == 3) {
			status = decimal_number(reference->parameters[1].text, &x);
			if (status == 0)
				status = decimal_number(reference->parameters[2].text, &y);
			if (status != 0)
				return status;
			/* Preserve WORLD's documented AND-shaped validation defect;
			 * the wire f command independently rejects either oversized axis. */
			if (x > 65535 && y > 65535)
				return IO_ERROR_INVALID_FORMAT;
			value.world_width  = (uint32_t)(x & UINT64_C(65535));
			value.world_height = (uint32_t)(y & UINT64_C(65535));
		}
		else
			return IO_ERROR_INVALID_FORMAT;
	}
	else {
		unsigned number;
		status = st_rip3_host_number(reference->parameters[1].text,
		    base    ? 36
		    : coord ? 2
		            : 0,
		    base    ? 64
		    : coord ? 5
		            : 65535,
		    &number);
		if (status != 0)
			return status;
		if (base) {
			if (number != 36 && number != 64)
				return IO_ERROR_INVALID_FORMAT;
			value.base_math = number;
		}
		else if (coord)
			value.coordinate_size = number;
		else if (width)
			value.world_width = number;
		else
			value.world_height = number;
	}
	status = color ? st_rip3_environments_color(environments, index, value.rgb_bits)
	               : st_rip3_environments_set(environments, index, &value);
	if (status == 0)
		result[0] = 0;
	return status;
}
