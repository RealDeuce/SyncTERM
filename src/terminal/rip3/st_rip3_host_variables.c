/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"

#include "st_rip3_environments.h"
#include "st_rip3_features.h"
#include "st_rip3_geometry.h"
#include "st_rip3_graphical_bytes.h"
#include "st_rip3_input.h"
#include "st_rip3_mouse.h"
#include "st_rip3_table_host.h"
#include "st_rip3_refresh.h"
#include "st_rip3_user_variables.h"
#include "io_error.h"
#include "st_rip3_tones.h"

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

size_t
st_rip3_directory_normalize(uint8_t *text, size_t length)
{
	size_t first = 0;
	while (first < length && text[first] != 0 && strchr(" \t\n\r\v\f",text[first]) != NULL) first++;
	while (length > first && text[length-1] != 0 && strchr(" \t\n\r\v\f",text[length-1]) != NULL) length--;
	for (size_t i = first; i < length; i++) {
		uint8_t ch = text[i];
		text[i-first] = ch >= 'a' && ch <= 'z' ? (uint8_t)(ch-'a'+'A') : ch;
	}
	return length-first;
}

int
st_rip3_directory_select(const struct st_rip3_host_directory *directory,
    const uint8_t *text, size_t length, bool converted, bool create)
{
	char name[ST_RIP3_HOST_DIRECTORY_CAPACITY];
	if (length >= sizeof(name)
	    || st_rip3_graphical_units(text,length,converted)>ST_RIP3_HOST_DIRECTORY_LIMIT)
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < length; i++) {
		uint8_t ch = text[i];
		if (ch < 32 || ch == 127 || strchr("\\:*?+/\",;><=[]|",ch) != NULL)
			return IO_ERROR_INVALID_FORMAT;
		name[i] = (char)ch;
	}
	name[length] = '\0';
	if (strcmp(name,".") == 0 || strcmp(name,"..") == 0) return IO_ERROR_INVALID_FORMAT;
	return directory->select(directory->context,name,create);
}

static int
host_directory(const struct st_rip3_host_directory *directory,
    const struct st_rip3_builtin_reference *reference, bool converted, char *result, size_t capacity)
{
	if (reference->count > 1) return IO_ERROR_INVALID_FORMAT;
	if (capacity == 0) return IO_ERROR_OUT_OF_RANGE;
	if (directory == NULL) return IO_ERROR_NOT_SUPPORTED;
	if (reference->count == 0) {
		const char *name = directory->current(directory->context);
		if (name[0] == '\0') name = "<NONE>";
		size_t length = strlen(name);
		if (length >= capacity) return IO_ERROR_OUT_OF_RANGE;
		memcpy(result,name,length+1);
		return 0;
	}
	uint8_t text[sizeof(reference->parameters[0].text)];
	size_t length = reference->parameters[0].length;
	if (length >= sizeof(text)) return IO_ERROR_INVALID_FORMAT;
	if (st_rip3_graphical_units((const uint8_t *)reference->parameters[0].text,length,converted)
	    > ST_RIP3_BUILTIN_PARAMETER_LIMIT) return IO_ERROR_OUT_OF_RANGE;
	memcpy(text,reference->parameters[0].text,length);
	length = st_rip3_directory_normalize(text,length);
	bool create = length != 3 || memcmp(text,"OFF",3) != 0;
	result[0] = '\0';
	return st_rip3_directory_select(directory,text,create ? length : 0,converted,create);
}

static int
kill_mouse_fields(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference)
{
	if (reference->count != 0 && reference->count != 4 && reference->count != 5)
		return IO_ERROR_INVALID_FORMAT;
	if (host->mouse == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if (reference->count == 0) {
		st_rip3_mouse_kill(host->mouse);
		return 0;
	}

	unsigned coordinates[4];
	for (size_t i = 0; i < 4; ++i) {
		int status = st_rip3_host_number(reference->parameters[i].text, 0, 65535,
		    &coordinates[i]);
		if (status != 0)
			return status;
	}
	unsigned flags = 1;
	if (reference->count == 5) {
		const struct st_rip3_builtin_parameter *parameter = &reference->parameters[4];
		char selector[sizeof("OUT")] = {0};
		if (parameter->length >= sizeof(selector))
			return IO_ERROR_INVALID_FORMAT;
		for (size_t i = 0; i < parameter->length; ++i) {
			char ch = parameter->text[i];
			selector[i] = ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
		}
		if (strcmp(selector, "OUT") == 0)
			flags = 4;
		else if (strcmp(selector, "IN") != 0)
			return IO_ERROR_INVALID_FORMAT;
	}

	const struct st_rip3_environments *environments = host->tables == NULL ? NULL
	    : host->tables->tables[ST_RIP3_TABLE_ENV].live;
	struct st_rip3_frame frame;
	if (environments != NULL) {
		unsigned current;
		int status = st_rip3_environments_current(environments, &current);
		if (status == 0)
			status = st_rip3_environments_frame(environments, current, &frame);
		if (status != 0)
			return status;
	}
	else if (host->frame != NULL)
		frame = *host->frame;
	else
		return IO_ERROR_NOT_SUPPORTED;

	struct st_rip3_point first = {(int32_t)coordinates[0], (int32_t)coordinates[1]};
	struct st_rip3_point second = {(int32_t)coordinates[2], (int32_t)coordinates[3]};
	struct st_rip3_point a, b;
	int status = st_rip3_world_to_device(&frame, &first, &a);
	if (status == 0)
		status = st_rip3_world_to_device(&frame, &second, &b);
	if (status != 0)
		return status;
	/* Projection is monotonic, so ordering projected endpoints produces
	 * the same half-open selector as ordering the WORLD endpoints first. */
	struct st_rip3_rect area = {
		a.x < b.x ? a.x : b.x,
		a.y < b.y ? a.y : b.y,
		a.x < b.x ? b.x : a.x,
		a.y < b.y ? b.y : a.y,
	};
	return st_rip3_mouse_remove(host->mouse, &area, flags, NULL);
}

static bool
transfer_keyword(const char *text, const char *keyword)
{
	while (*keyword != 0) {
		char ch = *text++;
		if (ch >= 'a' && ch <= 'z')
			ch = (char)(ch - 'a' + 'A');
		if (ch != *keyword++)
			return false;
	}
	return *text == 0;
}

static int
transfer(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference)
{
	static const char *const protocols[] = {
		"XMODEM", "XMODEMCRC", "XMODEM1K", "XMODEM1KG", "KERMIT",
		"YMODEM", "YMODEMG", "ZMODEM", "ZMODEMCR",
	};
	static const struct { const char *name; unsigned number; } types[] = {
		{"RIP", 0}, {"NONE", 1}, {"IMAGE", 3}, {"ACTIVE", 5},
	};
	if (reference->count < 3)
		return IO_ERROR_INVALID_FORMAT;
	bool upload = transfer_keyword(reference->parameters[0].text, "SEND");
	if (!upload && !transfer_keyword(reference->parameters[0].text, "RECEIVE"))
		return IO_ERROR_INVALID_FORMAT;
	unsigned protocol = 0;
	while (protocol < sizeof(protocols) / sizeof(*protocols)
	    && !transfer_keyword(reference->parameters[1].text, protocols[protocol]))
		++protocol;
	unsigned type = 0;
	while (type < sizeof(types) / sizeof(*types)
	    && !transfer_keyword(reference->parameters[2].text, types[type].name))
		++type;
	if (protocol == sizeof(protocols) / sizeof(*protocols)
	    || type == sizeof(types) / sizeof(*types))
		return IO_ERROR_INVALID_FORMAT;

	char names[37 * sizeof(reference->parameters[0].text)] = {0};
	if (upload || protocol < 5) {
		if (reference->count == 3)
			return IO_ERROR_INVALID_FORMAT;
		size_t length = 0;
		for (size_t i = 3; i < reference->count; ++i) {
			if (i != 3)
				names[length++] = ' ';
			size_t size = reference->parameters[i].length;
			memcpy(names + length, reference->parameters[i].text, size);
			length += size;
		}
	}
	if (host->transfer == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	return host->transfer(host->transfer_context, upload, protocol, types[type].number, names);
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
	if (strcmp(reference->name, "XFER") == 0) {
		if (capacity == 0)
			return IO_ERROR_OUT_OF_RANGE;
		int status = transfer(host, reference);
		if (status == 0)
			result[0] = '\0';
		return status;
	}
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
	if (strcmp(reference->name, "RIPVER") == 0) {
		if (reference->count != 0)
			return IO_ERROR_INVALID_FORMAT;
		if (capacity < sizeof(ST_RIP3_IDENTIFICATION))
			return IO_ERROR_OUT_OF_RANGE;
		memcpy(result, ST_RIP3_IDENTIFICATION, sizeof(ST_RIP3_IDENTIFICATION));
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
	if (strcmp(reference->name,"HOSTDIR") == 0)
		return host_directory(host->directory,reference,host->codepage_converted,result,capacity);
	if (strcmp(reference->name,"RESET") == 0 && reference->count != 0) {
		int status = st_rip3_builtin_reference_words(&words, reference);
		if (status != 0)
			return status;
		const char *scope = words.parameters[0].text;
		if ((scope[0] == 'T' || scope[0] == 't') && (scope[1] == 'V' || scope[1] == 'v')
		    && scope[2] == '\0') {
			if (reference->count != 2)
				return IO_ERROR_INVALID_FORMAT;
			if (capacity == 0)
				return IO_ERROR_OUT_OF_RANGE;
			const struct st_rip3_builtin_parameter *name = &reference->parameters[1];
			status = st_rip3_user_variables_delete(host->user_variables, name->text, name->length);
			if (status == 0)
				result[0] = '\0';
			return status;
		}
	}
	if (strcmp(reference->name, "FILEEXISTS") == 0) {
		if (reference->count != 1)
			return IO_ERROR_INVALID_FORMAT;
		if (capacity < 2)
			return IO_ERROR_OUT_OF_RANGE;
		if (host->directory == NULL || host->directory->file_exists == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		bool present;
		int status = host->directory->file_exists(host->directory->context,
		    reference->parameters[0].text, &present);
		if (status != 0)
			return status;
		result[0] = present ? '1' : '0';
		result[1] = '\0';
		return 0;
	}
	if (strcmp(reference->name, "FILEDEL") == 0) {
		if (reference->count == 0)
			return IO_ERROR_INVALID_FORMAT;
		if (capacity == 0)
			return IO_ERROR_OUT_OF_RANGE;
		if (host->directory == NULL || host->directory->remove_file == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		for (size_t i = 0; i < reference->count; ++i)
			host->directory->remove_file(host->directory->context, reference->parameters[i].text);
		result[0] = '\0';
		return 0;
	}
	if (strcmp(reference->name, "REFRESH") == 0 || strcmp(reference->name, "NOREFRESH") == 0) {
		if (reference->count != 0)
			return IO_ERROR_INVALID_FORMAT;
		if (capacity == 0)
			return IO_ERROR_OUT_OF_RANGE;
		if (host->refresh == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		result[0] = 0;
		if (reference->name[0] == 'R')
			return st_rip3_refresh_start(host->refresh);
		st_rip3_refresh_clear(host->refresh);
		return 0;
	}
	if (strcmp(reference->name, "MKILL") == 0) {
		if (capacity == 0)
			return IO_ERROR_OUT_OF_RANGE;
		int status = kill_mouse_fields(host, reference);
		if (status == 0)
			result[0] = 0;
		return status;
	}
	if (strcmp(reference->name,"FIELDID")==0) {
		if (reference->count!=0) return IO_ERROR_INVALID_FORMAT;
		if (host->field_context==NULL || !host->field_context->active) return IO_ERROR_NOT_SUPPORTED;
		char text[16];int length=snprintf(text,sizeof(text),"%d",host->field_context->identity);
		if (length<0 || (size_t)length>=sizeof(text)) return IO_ERROR_IO;
		if ((size_t)length>=capacity) return IO_ERROR_OUT_OF_RANGE;
		memcpy(result,text,(size_t)length+1);return 0;
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
	if (host->overflow != NULL
	    && (strcmp(reference->name, "OVERFLOW") == 0 || strcmp(reference->name, "RESET") == 0)) {
		int status = host->overflow(host->overflow_context, reference, result, capacity);
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
		struct st_rip3_tones tones;
		int tone_status = st_rip3_tones_parse(&tones, &reference);
		if (tone_status != IO_ERROR_NOT_SUPPORTED) {
			if (tone_status == 0) {
				if (host != NULL && host->tones != NULL && tones.repeats != 0)
					host->tones(host->tones_context, &tones);
			}
			else if (sink != NULL) {
				if (sink->reply != NULL)
					sink->reply(sink->context, text, length);
				if (sink->diagnostic != NULL)
					sink->diagnostic(sink->context, ST_RIP3_HOST_INVALID_REFERENCE);
			}
			return;
		}
		/* The zone is borrowed client text, not a formatted numeric/table
		 * reply. Emit its complete phrase without imposing the scratch-buffer
		 * bound or allocating a copy. Parameter errors still use recovery. */
		if (host != NULL && reference.count == 0 && strcmp(reference.name, "TIMEZONE") == 0) {
			char buffer[ST_RIP3_TIME_ZONE_CAPACITY];
			const char *zone = st_rip3_datetime_zone(host, buffer, sizeof(buffer));
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
	/* These rejected calls keep their original spelling in the response. */
	if (decoded && status == IO_ERROR_INVALID_FORMAT
	    && (strcmp(reference.name, "FILEEXISTS") == 0 || strcmp(reference.name, "BAUDEMUL") == 0
	        || strcmp(reference.name, "OPTION") == 0)
	    && sink != NULL && sink->reply != NULL)
		sink->reply(sink->context, text, length);
	/* Report rejection to the evaluator that owns recovery. */
	if (status == IO_ERROR_CANCELLED)
		diagnostic = ST_RIP3_HOST_ABORTED;
	else if (!decoded || status == IO_ERROR_INVALID_FORMAT || status == IO_ERROR_OUT_OF_RANGE
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
