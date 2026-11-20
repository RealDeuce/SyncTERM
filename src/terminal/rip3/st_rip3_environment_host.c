/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_environments.h"
#include "io_error.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static bool
equal(const char *text, const char *uppercase)
{
	for (size_t i = 0; ; i++) {
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
baud_number(const char *text, uint64_t *result)
{
	uint64_t value = 0;

	if (text[0] == 0)
		return IO_ERROR_INVALID_FORMAT;
	for (size_t i = 0; text[i] != 0; i++) {
		unsigned digit = (unsigned char)text[i] - (unsigned)'0';

		if (digit > 9 || value > UINT64_MAX / 10
		    || (value == UINT64_MAX / 10 && digit > UINT64_MAX % 10))
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

int
st_rip3_environment_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	struct st_rip3_environments *environments;
	struct st_rip3_environment value;

	if (host == NULL || reference == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	const char *name = reference->name;
	bool base = strcmp(name, "BASEMATH") == 0;
	bool coord = strcmp(name, "COORDSIZE") == 0;
	bool world = strcmp(name, "WORLD") == 0;
	bool width = strcmp(name, "WORLDW") == 0;
	bool height = strcmp(name, "WORLDH") == 0;
	bool color = strcmp(name, "COLORMODE") == 0;
	bool baud = strcmp(name, "BAUDEMUL") == 0;
	bool compat = strcmp(name, "COMPAT") == 0;
	bool setting = compat || reference->count >= 2;
	unsigned index;
	int status;

	if (!base && !coord && !world && !width && !height && !color && !baud && !compat)
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_builtin_reference words;
	status=st_rip3_builtin_reference_words(&words,reference);
	if (status!=0) return status;
	reference=&words;
	if (capacity == 0)
		return IO_ERROR_OUT_OF_RANGE;
	if (reference->count > (compat ? 1u : world || color ? 3u : 2u))
		return IO_ERROR_INVALID_FORMAT;
	/* The shipped two-argument axis setters invert their target test:
	 * literal CUR errors; every other target text writes the current ENV.
	 * Getter forms still validate and honor the requested numeric slot. */
	bool axis_setting=(width || height) && reference->count==2;
	if (axis_setting && equal(reference->parameters[0].text,"CUR"))
		return IO_ERROR_INVALID_FORMAT;
	if (!axis_setting && reference->count != 0 && !equal(reference->parameters[0].text, "CUR")) {
		status = st_rip3_host_number(reference->parameters[0].text, 0, 35, &index);
		if (status != 0)
			return status;
	}
	if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_ENV].live == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	environments = host->tables->tables[ST_RIP3_TABLE_ENV].live;
	if (axis_setting || reference->count == 0 || equal(reference->parameters[0].text, "CUR")) {
		status = st_rip3_environments_current(environments, &index);
		if (status != 0)
			return status;
	}
	status = st_rip3_environments_get(environments, index, &value);
	if (status == IO_ERROR_NOT_FOUND) {
		if (setting)
			return IO_ERROR_INVALID_FORMAT;
		if (color)
			return IO_ERROR_NOT_SUPPORTED; /* NONE versus -1 remains blocked, #23. */
		return reply_text(result, capacity, base || baud ? "NONE" : "-1");
	}
	if (status != 0)
		return status;
	if (!setting) {
		char text[48];
		int length;

		if (world)
			length = snprintf(text, sizeof(text), "%" PRIu32 ":%" PRIu32,
			    value.world_width, value.world_height);
		else {
			uint64_t number = base ? value.base_math : coord ? value.coordinate_size
			    : width ? value.world_width : height ? value.world_height
			    : color ? value.rgb_bits : value.baud_rate;
			length = snprintf(text, sizeof(text), "%" PRIu64, number);
		}
		if (length < 0 || (size_t)length >= sizeof(text))
			return IO_ERROR_IO;
		return reply_text(result, capacity, text);
	}
	if (compat) {
		value.world_width = 640; value.world_height = 350;
		value.base_math = 36; value.coordinate_size = 2;
		value.rgb_bits = 0; value.baud_rate = 0;
	}
	else if (baud) {
		status = baud_number(reference->parameters[1].text, &value.baud_rate);
		if (status != 0)
			return status;
	}
	else if (color) {
		const char *mode = reference->parameters[1].text;
		unsigned bits;

		if (equal(mode, "PAL") && reference->count == 2)
			value.rgb_bits = 0;
		else if (equal(mode, "RGB") && reference->count == 3) {
			status = st_rip3_host_number(reference->parameters[2].text, 1, 8, &bits);
			if (status != 0)
				return status;
			if (bits != 8)
				return IO_ERROR_NOT_SUPPORTED; /* Shipping v3 is explicitly eight-bit only. */
			value.rgb_bits = bits;
		}
		else
			return IO_ERROR_INVALID_FORMAT;
	}
	else if (world) {
		uint64_t x, y;

		if (reference->count == 2 && equal(reference->parameters[1].text, "DEFAULT")) {
			value.world_width = value.base_math == 36 ? 1280 : 4096;
			value.world_height = value.base_math == 36 ? 960 : 3072;
		}
		else if (reference->count == 3) {
			status = baud_number(reference->parameters[1].text, &x);
			if (status == 0)
				status = baud_number(reference->parameters[2].text, &y);
			if (status != 0)
				return status;
			/* Preserve WORLD's documented AND-shaped validation defect;
			 * the wire f command independently rejects either oversized axis. */
			if (x>65535 && y>65535) return IO_ERROR_INVALID_FORMAT;
			value.world_width=(uint32_t)(x&UINT64_C(65535));
			value.world_height=(uint32_t)(y&UINT64_C(65535));
		}
		else
			return IO_ERROR_INVALID_FORMAT;
	}
	else {
		unsigned number;
		status = st_rip3_host_number(reference->parameters[1].text,
		    base ? 36 : coord ? 2 : 0, base ? 64 : coord ? 5 : 65535, &number);
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
	status = st_rip3_environments_set(environments, index, &value);
	if (status == 0)
		result[0] = 0;
	return status;
}
