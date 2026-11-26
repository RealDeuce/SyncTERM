/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_ports.h"

int
st_rip3_viewport_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	struct st_rip3_ports       *ports;
	enum st_rip3_view_operation operation;

	enum {
		CURRENT = 36,
		ALL
	};

	bool     selected[36] = {0};
	unsigned numbers[36], current;
	size_t   count = 0;
	int      status;
	if (strcmp(reference->name, "CLS") == 0) {
		if (reference->count != 0)
			return IO_ERROR_INVALID_FORMAT;
		if (capacity == 0)
			return IO_ERROR_OUT_OF_RANGE;
		if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_PORT].live == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		/* The original callback uses the physical screen DC and BLACK_BRUSH. */
		ports  = host->tables->tables[ST_RIP3_TABLE_PORT].live;
		status = st_rip3_ports_screen_clear_black(ports);
		if (status == 0)
			result[0] = '\0';
		return status;
	}
	if (strcmp(reference->name, "PCB") == 0) {
		struct st_rip3_builtin_reference words;
		status = st_rip3_builtin_reference_words(&words, reference);
		if (status != 0)
			return status;
		if (capacity == 0)
			return IO_ERROR_OUT_OF_RANGE;
		if (words.count > 1)
			return IO_ERROR_INVALID_FORMAT;
		unsigned source      = 0;
		bool     use_current = false;
		if (words.count == 1) {
			char  *word  = words.parameters[0].text;
			size_t first = 0, last = words.parameters[0].length;
			while (
			    first < last && (word[first] == ' ' || (word[first] >= '\t' && word[first] <= '\r')))
				first++;
			while (last > first
			       && (word[last - 1] == ' ' || (word[last - 1] >= '\t' && word[last - 1] <= '\r')))
				last--;
			if (first == last)
				return IO_ERROR_INVALID_FORMAT;
			memmove(word, word + first, last - first);
			word[last - first] = 0;
			for (size_t i = 0; word[i] != 0; i++)
				if (word[i] >= 'a' && word[i] <= 'z')
					word[i] -= 'a' - 'A';
			use_current = strcmp(word, "CUR") == 0;
			if (!use_current && (status = st_rip3_host_number(word, 0, 35, &source)) != 0)
				return status;
		}
		if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_PORT].live == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		ports = host->tables->tables[ST_RIP3_TABLE_PORT].live;
		if (use_current && (status = st_rip3_ports_current(ports, &source)) != 0)
			return status;
		status = st_rip3_ports_paste(ports, words.count == 0 ? NULL : &source);
		if (status == 0)
			result[0] = 0;
		return status;
	}

	if (strcmp(reference->name, "AVP") == 0)
		operation = ST_RIP3_VIEW_ACTIVATE;
	else if (strcmp(reference->name, "DVP") == 0)
		operation = ST_RIP3_VIEW_DEACTIVATE;
	else if (strcmp(reference->name, "MVP") == 0)
		operation = ST_RIP3_VIEW_MAXIMIZE;
	else if (strcmp(reference->name, "EGW") == 0)
		operation = ST_RIP3_PORT_CLEAR_BLACK;
	else
		return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_builtin_reference words;
	status = st_rip3_builtin_reference_words(&words, reference);
	if (status != 0)
		return status;
	if (capacity == 0)
		return IO_ERROR_OUT_OF_RANGE;
	unsigned selectors[sizeof(words.parameters) / sizeof(words.parameters[0])] = {CURRENT};
	for (size_t i = 0; i < words.count; i++) {
		const char *parameter = words.parameters[i].text;
		char        word[21];

		for (size_t j = 0; j <= words.parameters[i].length; j++) {
			unsigned char ch = (unsigned char)parameter[j];
			word[j]          = ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : (char)ch;
		}
		if (strcmp(word, "CUR") == 0)
			selectors[i] = CURRENT;
		else if (strcmp(word, "ALL") == 0)
			selectors[i] = ALL;
		else {
			status = st_rip3_host_number(word, 0, 35, &selectors[i]);
			if (status != 0)
				return status;
		}
	}
	if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_PORT].live == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	ports  = host->tables->tables[ST_RIP3_TABLE_PORT].live;
	status = st_rip3_ports_current(ports, &current);
	if (status != 0)
		return status;
	size_t selector_count = words.count == 0 ? 1 : words.count;
	for (size_t i = 0; i < selector_count; i++) {
		unsigned selector = selectors[i];
		unsigned first    = selector == ALL ? 0 : selector == CURRENT ? current : selector;
		unsigned last     = selector == ALL ? 35 : first;
		for (unsigned port = first; port <= last; port++) {
			/* The operations are idempotent. Keep the first occurrence
			 * so repeated selectors do not repaint the same port. */
			if (!selected[port]) {
				selected[port]   = true;
				numbers[count++] = port;
			}
		}
	}
	status = st_rip3_ports_operate_views(ports, numbers, count, operation);
	if (status == 0)
		result[0] = 0;
	return status;
}
