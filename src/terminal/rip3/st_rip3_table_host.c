/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_table_host.h"

#include "st_rip3_backup.h"
#include "io_error.h"

#include <stdbool.h>
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
table_type(const char *text, enum st_rip3_table_type *result)
{
	static const char *const names[] = {"TW","BUT","STYLE","PAL","PORT","MOUSE","ENV","SCREEN"};
	_Static_assert(sizeof(names) / sizeof(*names) == ST_RIP3_TABLE_COUNT,
	    "Every table class needs its protocol keyword");

	for (unsigned i = 0; i < ST_RIP3_TABLE_COUNT; i++) {
		if (equal(text, names[i])) {
			*result = (enum st_rip3_table_type)i;
			return 0;
		}
	}
	return IO_ERROR_INVALID_FORMAT;
}

static int
location(const char *text, bool source, struct st_rip3_backup_location *result)
{
	struct st_rip3_backup_location value = {ST_RIP3_BACKUP_BASE,0};

	if (equal(text, "TBL")) value.place = ST_RIP3_BACKUP_TABLE;
	else if (equal(text, "BASE")) value.place = ST_RIP3_BACKUP_BASE;
	else if (equal(text, source ? "POP" : "PUSH")) value.place = ST_RIP3_BACKUP_STACK;
	else if ((text[0] == 'S' || text[0] == 's') && text[1] >= '0'
	    && text[1] <= '9' && text[2] == 0) {
		value.place = ST_RIP3_BACKUP_SLOT;
		value.index = (unsigned)(text[1] - '0');
	}
	else if (equal(text, "CUR"))
		return IO_ERROR_NOT_SUPPORTED;
	else if (text[0] >= '0' && text[0] <= '9') {
		unsigned number = 0;
		for (size_t i = 0; text[i] != 0; i++) {
			if (text[i] < '0' || text[i] > '9' || number > 35)
				return IO_ERROR_INVALID_FORMAT;
			number = number * 10 + (unsigned)(text[i] - '0');
		}
		return number <= 35 ? IO_ERROR_NOT_SUPPORTED : IO_ERROR_INVALID_FORMAT;
	}
	else
		return IO_ERROR_INVALID_FORMAT;
	*result = value;
	return 0;
}

int
st_rip3_table_host_execute(const struct st_rip3_table_host *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	const struct st_rip3_table_binding *binding;
	enum st_rip3_table_type type;
	int status;
	bool copy, backstat, save, restore;

	if (host == NULL || reference == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (reference->count > 40)
		return IO_ERROR_OUT_OF_RANGE;
	copy = strcmp(reference->name, "COPY") == 0;
	backstat = strcmp(reference->name, "BACKSTAT") == 0;
	save = strcmp(reference->name, "SCP") == 0;
	restore = strcmp(reference->name, "RCP") == 0;
	if (!copy && !backstat && !save && !restore)
		return IO_ERROR_NOT_SUPPORTED;
	if (capacity == 0)
		return IO_ERROR_OUT_OF_RANGE;
	if (save || restore) {
		struct st_rip3_backup_location place = {ST_RIP3_BACKUP_BASE,0};

		if (reference->count > 1)
			return IO_ERROR_INVALID_FORMAT;
		if (reference->count == 1) {
			const char *parameter = reference->parameters[0].text;
			if (parameter[0] >= '0' && parameter[0] <= '9') {
				unsigned number = 0;

				for (size_t i = 0; parameter[i] != 0; i++) {
					if (parameter[i] < '0' || parameter[i] > '9' || number > 9)
						return IO_ERROR_INVALID_FORMAT;
					number = number * 10 + (unsigned)(parameter[i] - '0');
				}
				if (number > 9)
					return IO_ERROR_INVALID_FORMAT;
				place.place = ST_RIP3_BACKUP_SLOT;
				place.index = number;
			}
			else {
				status = location(parameter, restore, &place);
				if (status != 0 || place.place == ST_RIP3_BACKUP_TABLE || place.place == ST_RIP3_BACKUP_SLOT)
					return IO_ERROR_INVALID_FORMAT;
			}
		}
		binding = &host->tables[ST_RIP3_TABLE_PAL];
		if (binding->live == NULL || binding->backup == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		status = save ? st_rip3_backup_save(binding->backup, place.place, place.index, binding->live)
		    : st_rip3_backup_restore(binding->backup, place.place, place.index, binding->live);
	}
	else {
		if (reference->count < (copy ? 3u : 1u) || (backstat && reference->count > 2))
			return IO_ERROR_INVALID_FORMAT;
		status = table_type(reference->parameters[0].text, &type);
		if (status != 0)
			return status;
		binding = &host->tables[type];
		if (backstat) {
			bool protection = reference->count == 2 && equal(reference->parameters[1].text, "PROT");

			if (reference->count == 2 && !protection && !equal(reference->parameters[1].text, "USE"))
				return IO_ERROR_INVALID_FORMAT;
			if (binding->backup == NULL)
				return IO_ERROR_NOT_SUPPORTED;
			return st_rip3_backup_status(binding->backup, protection, result, capacity);
		}
		else {
			struct st_rip3_backup_location source, destinations[38];
			bool needs_live;

			status = location(reference->parameters[1].text, true, &source);
			if (status != 0)
				return status;
			needs_live = source.place == ST_RIP3_BACKUP_TABLE;
			for (size_t i = 2; i < reference->count; i++) {
				status = location(reference->parameters[i].text, false, &destinations[i - 2]);
				if (status != 0)
					return status;
				needs_live |= destinations[i - 2].place == ST_RIP3_BACKUP_TABLE;
			}
			if (binding->backup == NULL || (needs_live && binding->live == NULL))
				return IO_ERROR_NOT_SUPPORTED;
			status = st_rip3_backup_copy(binding->backup, source, destinations,
			    reference->count - 2, binding->live);
		}
	}
	if (status == 0)
		result[0] = 0;
	return status;
}
