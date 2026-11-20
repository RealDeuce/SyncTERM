/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"

#include "st_rip3_features.h"
#include "st_rip3_geometry.h"
#include "st_rip3_input.h"
#include "st_rip3_table_host.h"
#include "io_error.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int
st_rip3_terminfo_select(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, const char **result)
{
	char keyword[21] = "NAME";
	const char *value;
	const struct st_rip3_terminal_identity *identity;

	if (host == NULL || reference == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (strcmp(reference->name, "TERMINFO") != 0)
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_builtin_reference words;
	int status=st_rip3_builtin_reference_words(&words,reference);
	if (status!=0) return status;
	reference=&words;
	if (reference->count > 1)
		return IO_ERROR_INVALID_FORMAT;
	if (reference->count == 1) {
		size_t length = reference->parameters[0].length;
		if (length > 20)
			return IO_ERROR_OUT_OF_RANGE;
		for (size_t i = 0; i < length; i++) {
			unsigned char ch = (unsigned char)reference->parameters[0].text[i];
			keyword[i] = ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : (char)ch;
		}
		keyword[length] = 0;
	}
	identity = host->identity;
	if (identity == NULL || identity->name == NULL || identity->vendor == NULL
	    || identity->version == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if (strcmp(keyword, "NAME") == 0) value = identity->name;
	else if (strcmp(keyword, "VENDOR") == 0) value = identity->vendor;
	else if (strcmp(keyword, "VERSION") == 0) value = identity->version;
	else if (strcmp(keyword, "LIST") == 0) value = "LIST,NAME,VENDOR,VERSION";
	else value = "NONE";
	*result = value;
	return 0;
}

int
st_rip3_host_variables_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	struct st_rip3_builtin_reference words;
	if (host == NULL || reference == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (reference->count > 40)
		return IO_ERROR_OUT_OF_RANGE;
	if (!st_rip3_builtin_known(reference->name, strlen(reference->name)))
		return IO_ERROR_NOT_FOUND;
	if (strcmp(reference->name,"D")==0 || strcmp(reference->name,"IFS")==0) {
		int status=st_rip3_builtin_reference_words(&words,reference);
		if (status!=0) return status;
		reference=&words;
	}
	if (strcmp(reference->name, "TERMINFO") == 0) {
		const char *value;
		int status = st_rip3_terminfo_select(host, reference, &value);
		if (status != 0)
			return status;
		size_t length = strlen(value);
		if (length >= capacity)
			return IO_ERROR_OUT_OF_RANGE;
		memcpy(result, value, length + 1);
		return 0;
	}
	if (strcmp(reference->name, "NULL") == 0) {
		if (reference->count != 0)
			return IO_ERROR_INVALID_FORMAT;
		if (capacity == 0)
			return IO_ERROR_OUT_OF_RANGE;
		result[0] = 0;
		return 0;
	}
	if (strcmp(reference->name,"RAND")==0)
		return st_rip3_random_execute(host,reference,result,capacity);
	{
		int status=st_rip3_local_variables_execute(host,reference,result,capacity);
		if (status!=IO_ERROR_NOT_SUPPORTED) return status;
	}
	if (strcmp(reference->name, "D") == 0) {
		unsigned ticks;
		if (reference->count != 1)
			return IO_ERROR_INVALID_FORMAT;
		int status = st_rip3_host_number(reference->parameters[0].text,1,65535,&ticks);
		if (status != 0)
			return status;
		if (capacity == 0)
			return IO_ERROR_OUT_OF_RANGE;
		if (host->input == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		/* The detailed D rule and inventory specify 60ths; its summary's
		 * milliseconds wording remains a documented conflict under #46. */
		st_rip3_input_delay(host->input,(uint32_t)ticks);
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
		int status = st_rip3_environment_host_execute(host, reference, result, capacity);
		if (status != IO_ERROR_NOT_SUPPORTED)
			return status;
	}
	{
		int status = st_rip3_port_query_execute(host, reference, result, capacity);
		if (status != IO_ERROR_NOT_SUPPORTED)
			return status;
	}
	{
		int status = st_rip3_image_style_host_execute(host, reference, result, capacity);
		if (status != IO_ERROR_NOT_SUPPORTED)
			return status;
	}
	{
		int status = st_rip3_viewport_host_execute(host, reference, result, capacity);
		if (status != IO_ERROR_NOT_SUPPORTED)
			return status;
	}
	{
		int status = st_rip3_text_window_host_execute(host, reference, result, capacity);
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
		if (strcmp(reference.name, "TERMINFO") == 0) {
			const char *value;
			status = st_rip3_terminfo_select(host, &reference, &value);
			if (status == 0) {
				size_t size = strlen(value);
				if (size != 0 && sink != NULL && sink->reply != NULL)
					sink->reply(sink->context, (const uint8_t *)value, size);
				return;
			}
		}
		else
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
