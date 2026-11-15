/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_variables.h"

#include "st_rip3_features.h"
#include "st_rip3_geometry.h"
#include "st_rip3_table_host.h"
#include "io_error.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int
st_rip3_host_variables_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	if (host == NULL || reference == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (reference->count > 40)
		return IO_ERROR_OUT_OF_RANGE;
	if (strcmp(reference->name, "NULL") == 0) {
		if (reference->count != 0)
			return IO_ERROR_INVALID_FORMAT;
		if (capacity == 0)
			return IO_ERROR_OUT_OF_RANGE;
		result[0] = 0;
		return 0;
	}
	if (strcmp(reference->name, "IFS") == 0) {
		if (reference->count == 0 || reference->count > 2)
			return IO_ERROR_INVALID_FORMAT;
		if (host->features == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		return st_rip3_features_query(host->features, reference->parameters[0].text,
		    reference->count == 2 ? reference->parameters[1].text : NULL,
		    result, capacity);
	}
	if (strcmp(reference->name, "RESX") == 0 || strcmp(reference->name, "RESY") == 0) {
		char text[16];
		uint32_t dimension;
		int length;

		if (reference->count != 0)
			return IO_ERROR_INVALID_FORMAT;
		if (host->frame == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		dimension = strcmp(reference->name, "RESX") == 0
		    ? host->frame->device_width : host->frame->device_height;
		length = snprintf(text, sizeof(text), "%" PRIu32, dimension);
		if (length < 0 || (size_t)length >= sizeof(text))
			return IO_ERROR_IO;
		if ((size_t)length >= capacity)
			return IO_ERROR_OUT_OF_RANGE;
		memcpy(result, text, (size_t)length + 1);
		return 0;
	}
	if (strcmp(reference->name, "APP") == 0 || strcmp(reference->name, "GOTOURL") == 0)
		return IO_ERROR_NOT_SUPPORTED;
	if (host->tables != NULL)
		return st_rip3_table_host_execute(host->tables, reference, result, capacity);
	return IO_ERROR_NOT_SUPPORTED;
}
