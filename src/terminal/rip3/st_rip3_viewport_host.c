/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"

#include "st_rip3_ports.h"
#include "st_rip3_styles.h"
#include "st_rip3_environments.h"
#include "st_rip3_palettes.h"
#include "io_error.h"

#include <stdbool.h>
#include <string.h>

int
st_rip3_viewport_host_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	struct st_rip3_ports *ports;
	enum st_rip3_view_operation operation;
	bool selected[36] = {0}, current_requested = reference->count == 0;
	unsigned numbers[36], current;
	size_t count = 0;
	int status;

	if (strcmp(reference->name, "AVP") == 0) operation = ST_RIP3_VIEW_ACTIVATE;
	else if (strcmp(reference->name, "DVP") == 0) operation = ST_RIP3_VIEW_DEACTIVATE;
	else if (strcmp(reference->name, "MVP") == 0) operation = ST_RIP3_VIEW_MAXIMIZE;
	else if (strcmp(reference->name, "EGW") == 0) operation = ST_RIP3_VIEW_ERASE;
	else return IO_ERROR_NOT_SUPPORTED;
	struct st_rip3_builtin_reference words;
	status=st_rip3_builtin_reference_words(&words,reference);
	if (status!=0) return status;
	reference=&words;
	if (capacity == 0)
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < reference->count; i++) {
		const char *parameter = reference->parameters[i].text;
		char word[21];

		for (size_t j = 0; j <= reference->parameters[i].length; j++) {
			unsigned char ch = (unsigned char)parameter[j];
			word[j] = ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : (char)ch;
		}
		if (strcmp(word, "CUR") == 0)
			current_requested = true;
		else if (strcmp(word, "ALL") == 0) {
			for (unsigned j = 0; j < 36; j++) selected[j] = true;
		}
		else {
			unsigned number;
			status = st_rip3_host_number(word, 0, 35, &number);
			if (status != 0)
				return status;
			selected[number] = true;
		}
	}
	if (host->tables == NULL || host->tables->tables[ST_RIP3_TABLE_PORT].live == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	ports = host->tables->tables[ST_RIP3_TABLE_PORT].live;
	if (current_requested) {
		status = st_rip3_ports_current(ports, &current);
		if (status != 0)
			return status;
		selected[current] = true;
	}
	for (unsigned i = 0; i < 36; i++) {
		if (selected[i]) numbers[count++] = i;
	}
	uint8_t background=host->background;
	struct st_rip3_styles *styles=host->tables->tables[ST_RIP3_TABLE_STYLE].live;
	if (operation==ST_RIP3_VIEW_ERASE && styles!=NULL) {
		unsigned number;struct st_rip3_style style;bool direct=false;
		status=st_rip3_styles_current(styles,&number);
		if (status==0) status=st_rip3_styles_get(styles,number,&style);
		struct st_rip3_environments *environments=host->tables->tables[ST_RIP3_TABLE_ENV].live;
		if (status==0 && environments!=NULL) {
			struct st_rip3_environment environment;
			status=st_rip3_environments_current(environments,&number);
			if (status==0) status=st_rip3_environments_get(environments,number,&environment);
			if (status==0) direct=environment.rgb_bits==8;
		}
		if (status==0 && direct) {
			struct st_rip3_palettes *palettes=host->tables->tables[ST_RIP3_TABLE_PAL].live;
			status=palettes==NULL ? IO_ERROR_NOT_SUPPORTED : st_rip3_palettes_color(palettes,true,style.background_color,&background);
		}
		else if (status==0) {
			if (style.background_color>255) status=IO_ERROR_OUT_OF_RANGE;
			else background=(uint8_t)style.background_color;
		}
		if (status!=0) return status;
	}
	status = st_rip3_ports_operate_views(ports, numbers, count, operation, background);
	if (status == 0)
		result[0] = 0;
	return status;
}
