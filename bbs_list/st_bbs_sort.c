/* Typed, representation-independent BBS directory sorting. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_bbs_list_internal.h"

#include "io_error.h"
#include "string_util.h"

#include <stdlib.h>
#include <string.h>

struct sort_context {
	const int *fields;
	size_t count;
};

static _Thread_local struct sort_context current_sort;

int
st_bbs_sort_field_normalize(int field)
{
	int magnitude = field < 0 ? -field : field;
	int sign = field < 0 ? -1 : 1;

	if (magnitude == 30)
		magnitude = ST_BBS_SORT_EXPLICIT;
	if ((magnitude >= ST_BBS_SORT_NAME
	    && magnitude <= ST_BBS_SORT_EXPLICIT)
	    || magnitude == ST_BBS_SORT_LF_EXPAND)
		return sign * magnitude;
	return ST_BBS_SORT_NONE;
}

const char *
st_bbs_sort_field_name(int field)
{
	static const char *names[] = {
		NULL, "Entry Name", "Date Added", "Date Last Connected",
		"Total Calls", "Dialing List", "Address", "Port", "Username",
		"Password", "System Password", "Connection Type", "Screen Mode",
		"Status Line Visibility", "Download Directory", "Upload Directory",
		"Log File", "Transfer Log Level", "BPS Rate", "ANSI Music",
		"Address Family", "Font", "Hide Popups", "RIP", "Flow Control",
		"Comment", "Force LCF", "Yellow Is Yellow", "Terminal Type",
		"Explicit Sort Order"
	};
	int normalized = st_bbs_sort_field_normalize(field);
	int magnitude = normalized < 0 ? -normalized : normalized;

	if (magnitude == ST_BBS_SORT_LF_EXPAND)
		return "LF Expand";
	if (magnitude <= ST_BBS_SORT_EXPLICIT)
		return names[magnitude];
	return NULL;
}

static int
compare_nullable_ascii(const char *left, const char *right)
{
	if (left == NULL)
		left = "";
	if (right == NULL)
		right = "";
	return ascii_case_compare(left, right);
}

#define COMPARE_VALUE(a, b) ((a) < (b) ? -1 : (a) > (b) ? 1 : 0)

static int
compare_field(const struct st_profile *a, enum st_bbs_source a_source,
    const struct st_profile *b, enum st_bbs_source b_source, int field)
{
	switch (field) {
	case ST_BBS_SORT_NAME: return compare_nullable_ascii(a->name, b->name);
	case ST_BBS_SORT_ADDED: return COMPARE_VALUE(a->added, b->added);
	case ST_BBS_SORT_LAST_CONNECTED:
		return COMPARE_VALUE(a->connected, b->connected);
	case ST_BBS_SORT_CALLS: return COMPARE_VALUE(a->calls, b->calls);
	case ST_BBS_SORT_SOURCE:
		return COMPARE_VALUE(a_source, b_source);
	case ST_BBS_SORT_ADDRESS: return compare_nullable_ascii(a->address, b->address);
	case ST_BBS_SORT_PORT: return COMPARE_VALUE(a->port, b->port);
	case ST_BBS_SORT_USERNAME: return compare_nullable_ascii(a->username, b->username);
	case ST_BBS_SORT_PASSWORD: return compare_nullable_ascii(a->password, b->password);
	case ST_BBS_SORT_SYSTEM_PASSWORD:
		return compare_nullable_ascii(a->system_password, b->system_password);
	case ST_BBS_SORT_CONNECTION_TYPE:
		return COMPARE_VALUE(a->connection_type, b->connection_type);
	case ST_BBS_SORT_SCREEN_MODE:
		return COMPARE_VALUE(a->screen_mode, b->screen_mode);
	case ST_BBS_SORT_STATUS_HIDDEN:
		return COMPARE_VALUE(a->hide_status, b->hide_status);
	case ST_BBS_SORT_DOWNLOAD_DIRECTORY:
		return compare_nullable_ascii(a->download_directory, b->download_directory);
	case ST_BBS_SORT_UPLOAD_DIRECTORY:
		return compare_nullable_ascii(a->upload_directory, b->upload_directory);
	case ST_BBS_SORT_LOG_PATH: return compare_nullable_ascii(a->log_path, b->log_path);
	case ST_BBS_SORT_TRANSFER_LOG_LEVEL:
		return COMPARE_VALUE(a->transfer_log_level, b->transfer_log_level);
	case ST_BBS_SORT_BPS_RATE: return COMPARE_VALUE(a->bps_rate, b->bps_rate);
	case ST_BBS_SORT_MUSIC: return COMPARE_VALUE(a->music, b->music);
	case ST_BBS_SORT_ADDRESS_FAMILY:
		return COMPARE_VALUE(a->address_family, b->address_family);
	case ST_BBS_SORT_FONT: return compare_nullable_ascii(a->font, b->font);
	case ST_BBS_SORT_HIDE_POPUPS:
		return COMPARE_VALUE(a->hide_popups, b->hide_popups);
	case ST_BBS_SORT_RIP: return COMPARE_VALUE(a->rip_version, b->rip_version);
	case ST_BBS_SORT_FLOW_CONTROL:
		return COMPARE_VALUE(a->flow_control, b->flow_control);
	case ST_BBS_SORT_COMMENT: return compare_nullable_ascii(a->comment, b->comment);
	case ST_BBS_SORT_FORCE_LCF:
		return COMPARE_VALUE(a->force_lcf, b->force_lcf);
	case ST_BBS_SORT_YELLOW_IS_YELLOW:
		return COMPARE_VALUE(a->yellow_is_yellow, b->yellow_is_yellow);
	case ST_BBS_SORT_TERMINAL_TYPE:
		return compare_nullable_ascii(a->terminal_name, b->terminal_name);
	case ST_BBS_SORT_EXPLICIT:
		return COMPARE_VALUE(a->sort_order, b->sort_order);
	case ST_BBS_SORT_LF_EXPAND:
		return COMPARE_VALUE(a->expand_lf, b->expand_lf);
	default: return 0;
	}
}

int
st_bbs_compare_profiles(const struct st_profile *left,
    enum st_bbs_source left_source, const struct st_profile *right,
    enum st_bbs_source right_source, const int *fields, size_t field_count)
{
	if (left == NULL || right == NULL)
		return left == right ? 0 : left == NULL ? -1 : 1;
	for (size_t i = 0; i < field_count; ++i) {
		int requested = fields[i];
		int field = requested < 0 ? -requested : requested;
		bool reverse = requested < 0;
		int result = compare_field(left, left_source, right, right_source,
		    field);

		if (st_bbs_sort_field_reversed(field))
			reverse = !reverse;
		if (result != 0)
			return reverse ? -result : result;
	}
	return compare_nullable_ascii(left->name, right->name);
}

bool
st_bbs_sort_field_reversed(int field)
{
	return field == ST_BBS_SORT_ADDED
	    || field == ST_BBS_SORT_LAST_CONNECTED
	    || field == ST_BBS_SORT_CALLS;
}

static int
compare_entries(const void *left_pointer, const void *right_pointer)
{
	const struct st_bbs_owned_entry *left = *(const void *const *)left_pointer;
	const struct st_bbs_owned_entry *right = *(const void *const *)right_pointer;

	return st_bbs_compare_profiles(&left->profile, left->public.source,
	    &right->profile, right->public.source, current_sort.fields,
	    current_sort.count);
}

void
st_bbs_view_sort(struct st_bbs_view *view, const int *fields,
    size_t field_count)
{
	current_sort.fields = fields;
	current_sort.count = field_count;
	qsort(view->entries, array_list_count((array_list_t)view->entries),
	    sizeof(*view->entries), compare_entries);
	current_sort.fields = NULL;
	current_sort.count = 0;
}

int
st_bbs_list_sort(st_bbs_list_t *list, const int *fields, size_t field_count)
{
	int *copy = NULL;

	if (list == NULL || (field_count != 0 && fields == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	if (field_count != 0) {
		copy = malloc(field_count * sizeof(*copy));
		if (copy == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		for (size_t i = 0; i < field_count; ++i) {
			copy[i] = st_bbs_sort_field_normalize(fields[i]);
			if (copy[i] == ST_BBS_SORT_NONE) {
				free(copy);
				return IO_ERROR_INVALID_ARGUMENT;
			}
		}
	}
	free(list->sort_fields);
	list->sort_fields = copy;
	list->sort_field_count = field_count;
	st_bbs_view_sort(&list->view, copy, field_count);
	list->generation++;
	return 0;
}
