/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"

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
	if (!st_rip3_builtin_known(reference->name, strlen(reference->name)))
		return IO_ERROR_NOT_FOUND;
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
	{
		int status = st_rip3_datetime_execute(host, reference, result, capacity);
		if (status != IO_ERROR_NOT_SUPPORTED)
			return status;
	}
	{
		int status = st_rip3_viewport_host_execute(host, reference, result, capacity);
		if (status != IO_ERROR_NOT_SUPPORTED)
			return status;
	}
	if (host->tables != NULL)
		return st_rip3_table_host_execute(host->tables, reference, result, capacity);
	return IO_ERROR_NOT_SUPPORTED;
}

void
st_rip3_host_variables_process(const struct st_rip3_host_variables *host,
    const struct st_rip3_host_sink *sink, const uint8_t *text, size_t length)
{
	struct st_rip3_builtin_reference reference;
	/* Maximum formatted numeric/table reply: 8; plus 256 decimal RGB triples
	 * and commas, with a final NUL. Borrowed client text is emitted separately.
	 * Capacity is owned here, never a caller parse error. */
	char reply[2 + 256 * 12];
	enum st_rip3_host_diagnostic diagnostic;
	bool decoded = false;
	int status = st_rip3_builtin_reference_parse(&reference, text, length);

	if (status == 0) {
		decoded = true;
		/* The zone is borrowed client text, not a formatted numeric/table
		 * reply. Emit its complete phrase without imposing the scratch-buffer
		 * bound or allocating a copy. Parameter errors still use recovery. */
		if (host != NULL && reference.count == 0 && strcmp(reference.name, "TIMEZONE") == 0) {
			const char *zone = st_rip3_datetime_zone(host);
			if (sink != NULL && sink->reply != NULL)
				sink->reply(sink->context, (const uint8_t *)zone, strlen(zone));
			return;
		}
		status = st_rip3_host_variables_execute(host, &reference, reply, sizeof(reply));
	}
	if (status == 0) {
		size_t size = strlen(reply);
		if (size != 0 && sink != NULL && sink->reply != NULL)
			sink->reply(sink->context, (const uint8_t *)reply, size);
		return;
	}
	/* A command boundary was supplied by the enclosing RIP parser. Discard
	 * this reference only; no error is propagated and no failure is latched. */
	if (!decoded || status == IO_ERROR_INVALID_FORMAT || status == IO_ERROR_OUT_OF_RANGE
	    || status == IO_ERROR_INVALID_ARGUMENT)
		diagnostic = ST_RIP3_HOST_INVALID_REFERENCE;
	else if (status == IO_ERROR_NOT_SUPPORTED)
		diagnostic = ST_RIP3_HOST_UNSUPPORTED_REFERENCE;
	else if (status == IO_ERROR_NOT_FOUND
	    && !st_rip3_builtin_known(reference.name, strlen(reference.name)))
		diagnostic = ST_RIP3_HOST_UNKNOWN_REFERENCE;
	else if (status == IO_ERROR_OUT_OF_MEMORY)
		diagnostic = ST_RIP3_HOST_RESOURCE_FAILURE;
	else
		diagnostic = ST_RIP3_HOST_OPERATION_FAILED;
	if (sink != NULL && sink->diagnostic != NULL)
		sink->diagnostic(sink->context, diagnostic);
}
