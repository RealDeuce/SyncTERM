/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_table_host.h"

#include "st_rip3_backup.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_environments.h"
#include "st_rip3_styles.h"
#include "st_rip3_button_styles.h"
#include "st_rip3_text_window.h"
#include "st_rip3_mouse.h"
#include "st_rip3_screen.h"
#include "io_error.h"

#include <stdbool.h>
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
		unsigned index;
		return st_rip3_host_number(text, 0, 35, &index) == 0
		    ? IO_ERROR_NOT_SUPPORTED : IO_ERROR_INVALID_FORMAT;
	}
	else
		return IO_ERROR_INVALID_FORMAT;
	*result = value;
	return 0;
}

static int
entry_current(enum st_rip3_table_type type, void *live, unsigned *result)
{
	if (live == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if (type == ST_RIP3_TABLE_TW) {
		const struct st_rip3_text_window_operations *windows=live;
		return windows->current==NULL ? IO_ERROR_NOT_SUPPORTED : windows->current(windows->context,result);
	}
	if (type == ST_RIP3_TABLE_PAL)
		return st_rip3_palettes_current(live, result);
	if (type == ST_RIP3_TABLE_PORT)
		return st_rip3_ports_current(live, result);
	if (type == ST_RIP3_TABLE_ENV)
		return st_rip3_environments_current(live, result);
	if (type == ST_RIP3_TABLE_STYLE)
		return st_rip3_styles_current(live, result);
	if (type == ST_RIP3_TABLE_BUT)
		return st_rip3_button_styles_current(live,result);
	return IO_ERROR_NOT_SUPPORTED;
}

static int
entry_info(enum st_rip3_table_type type, void *live, unsigned index,
    bool *defined, bool *protected)
{
	if (live == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	if (type == ST_RIP3_TABLE_TW) {
		const struct st_rip3_text_window_operations *windows=live;
		struct st_rip3_text_window_info info;
		int status=windows->info==NULL ? IO_ERROR_NOT_SUPPORTED : windows->info(windows->context,index,&info);
		if (status==0) {*defined=info.defined;*protected=info.protected;}
		return status;
	}
	if (type == ST_RIP3_TABLE_PAL) {
		struct st_rip3_palette_info info;
		int status = st_rip3_palettes_info(live, index, &info);
		if (status != 0)
			return status;
		*defined = info.defined;
		*protected = info.protected;
		return 0;
	}
	if (type == ST_RIP3_TABLE_PORT) {
		struct st_rip3_port_info info;
		int status = st_rip3_ports_info(live, index, &info);
		if (status != 0)
			return status;
		*defined = info.defined;
		*protected = info.protected;
		return 0;
	}
	if (type == ST_RIP3_TABLE_STYLE) {
		struct st_rip3_style_info info;
		int status = st_rip3_styles_info(live, index, &info);
		if (status != 0)
			return status;
		*defined = info.defined;
		*protected = info.protected;
		return 0;
	}
	if (type == ST_RIP3_TABLE_BUT) {
		struct st_rip3_button_style_info info;
		int status=st_rip3_button_styles_info(live,index,&info);
		if (status==0) {*defined=info.defined;*protected=info.protected;}
		return status;
	}
	if (type == ST_RIP3_TABLE_ENV) {
		struct st_rip3_environment_info info;
		int status = st_rip3_environments_info(live, index, &info);
		if (status != 0)
			return status;
		*defined = info.defined;
		*protected = info.protected;
		return 0;
	}
	return IO_ERROR_NOT_SUPPORTED;
}

static int
reply_number(char *result, size_t capacity, int value)
{
	char text[16];
	int length = snprintf(text, sizeof(text), "%d", value);

	if (length < 0 || (size_t)length >= sizeof(text))
		return IO_ERROR_IO;
	if ((size_t)length >= capacity)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(result, text, (size_t)length + 1);
	return 0;
}

static int
palette_entries(const struct st_rip3_table_host *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	struct st_rip3_palettes *palettes = host->tables[ST_RIP3_TABLE_PAL].live;
	struct st_rip3_palette palette;
	char output[2 + 256 * 12];
	size_t length = 2;
	unsigned index, first, last;
	int status;

	if (reference->count < 2 || reference->count > 3)
		return IO_ERROR_INVALID_FORMAT;
	if (equal(reference->parameters[0].text, "CUR")) {
		if (palettes == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		status = st_rip3_palettes_current(palettes, &index);
	}
	else
		status = st_rip3_host_number(reference->parameters[0].text, 0, 35, &index);
	if (status != 0)
		return status;
	if (equal(reference->parameters[1].text, "ALL")) {
		if (reference->count != 2)
			return IO_ERROR_INVALID_FORMAT;
		first = 0; last = 255;
	}
	else {
		status = st_rip3_host_number(reference->parameters[1].text, 0, 255, &first);
		if (status != 0)
			return status;
		last = first;
		if (reference->count == 3) {
			status = st_rip3_host_number(reference->parameters[2].text, first, 255, &last);
			if (status != 0)
				return status;
		}
	}
	if (palettes == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	status = st_rip3_palettes_get(palettes, index, &palette);
	if (status == IO_ERROR_NOT_FOUND)
		return reply_number(result, capacity, -1);
	if (status != 0)
		return status;
	/* Every published component retains its six-bit value in its high bits,
	 * including 255 for full-scale white. */
	memcpy(output, "6;", 2);
	for (unsigned i = first; i <= last; i++) {
		uint32_t rgb = palette.rgb[i];
		int added = snprintf(output + length, sizeof(output) - length,
		    "%s%u:%u:%u", i == first ? "" : ",",
		    (unsigned)((rgb >> 18) & 63), (unsigned)((rgb >> 10) & 63), (unsigned)((rgb >> 2) & 63));

		if (added < 0 || (size_t)added >= sizeof(output) - length)
			return IO_ERROR_OUT_OF_RANGE;
		length += (size_t)added;
	}
	if (length >= capacity)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(result, output, length + 1);
	return 0;
}

enum element {
	ENTRY, SLOT, CURRENT, ALL, ANY, ALLSLOTS, ANYSLOTS, BASE, STACK, BACKUP,
};

static int
element(const char *text, enum element *result, unsigned *index)
{
	static const struct {const char *name; enum element value;} words[] = {
		{"CUR",CURRENT}, {"ALL",ALL}, {"ANY",ANY}, {"ALLSLOTS",ALLSLOTS},
		{"ANYSLOTS",ANYSLOTS}, {"BASE",BASE}, {"STACK",STACK}, {"BACKUP",BACKUP},
	};

	for (size_t i = 0; i < sizeof(words) / sizeof(*words); i++) {
		if (equal(text, words[i].name)) {
			*result = words[i].value;
			return 0;
		}
	}
	if ((text[0] == 's' || text[0] == 'S') && text[1] >= '0'
	    && text[1] <= '9' && text[2] == 0) {
		*result = SLOT;
		*index = (unsigned)(text[1] - '0');
		return 0;
	}
	if (st_rip3_host_number(text, 1, 35, index) == 0) {
		*result = ENTRY;
		return 0;
	}
	return IO_ERROR_INVALID_FORMAT;
}

static int
objects(const struct st_rip3_table_host *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	bool current = strcmp(reference->name, "CUR") == 0;
	bool inuse = strcmp(reference->name, "INUSE") == 0;
	bool isprot = strcmp(reference->name, "ISPROT") == 0;
	bool protect = strcmp(reference->name, "PROT") == 0;
	bool active = protect || strcmp(reference->name, "UNPROT") == 0;
	enum st_rip3_table_type type;
	const struct st_rip3_table_binding *binding;
	int status;

	if (reference->count < (current ? 1u : 2u)
	    || (!active && reference->count > 2))
		return IO_ERROR_INVALID_FORMAT;
	if (inuse && equal(reference->parameters[0].text, "TV"))
		return IO_ERROR_NOT_SUPPORTED;
	status = table_type(reference->parameters[0].text, &type);
	if (status != 0)
		return status;
	binding = &host->tables[type];
	if (current) {
		unsigned index;

		if (type == ST_RIP3_TABLE_SCREEN || type == ST_RIP3_TABLE_MOUSE)
			return IO_ERROR_INVALID_FORMAT;
		if (reference->count == 1) {
			status = entry_current(type, binding->live, &index);
			return status == 0 ? reply_number(result, capacity, (int)index) : status;
		}
		status = st_rip3_host_number(reference->parameters[1].text, 0, 35, &index);
		if (status != 0)
			return status;
		if (binding->live == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		if (type == ST_RIP3_TABLE_PAL)
			status = st_rip3_palettes_switch(binding->live, index);
		else if (type == ST_RIP3_TABLE_PORT)
			status = st_rip3_ports_select(binding->live, index);
		else if (type == ST_RIP3_TABLE_STYLE)
			status = st_rip3_styles_switch(binding->live,index);
		else if (type == ST_RIP3_TABLE_ENV)
			status = st_rip3_environments_activate(binding->live,index);
		else if (type == ST_RIP3_TABLE_BUT)
			status = st_rip3_button_styles_select(binding->live,index);
		else if (type == ST_RIP3_TABLE_TW) {
			const struct st_rip3_text_window_operations *windows=binding->live;
			status=windows->select==NULL ? IO_ERROR_NOT_SUPPORTED : windows->select(windows->context,index);
		}
		else
			return IO_ERROR_NOT_SUPPORTED;
		if (status == 0)
			result[0] = 0;
		return status;
	}
	{
		bool entries[36] = {0}, slots[10] = {0};
		bool uses_entries = false, uses_slots = false;
		enum element selected = ENTRY;

		for (size_t i = 1; i < reference->count; i++) {
			enum element kind;
			unsigned index = 0;

			status = element(reference->parameters[i].text, &kind, &index);
			if (status != 0)
				return status;
			selected = kind;
			if (kind == CURRENT) {
				if (inuse || type == ST_RIP3_TABLE_SCREEN || type == ST_RIP3_TABLE_MOUSE)
					return IO_ERROR_INVALID_FORMAT;
				status = entry_current(type, binding->live, &index);
				if (status != 0)
					return status;
				entries[index] = true; uses_entries = true;
			}
			else if (kind == ENTRY || kind == ALL || kind == ANY) {
				if (type == ST_RIP3_TABLE_SCREEN || (type == ST_RIP3_TABLE_MOUSE
				    && (!inuse || kind == ENTRY)) || (active && kind == ANY))
					return IO_ERROR_INVALID_FORMAT;
				uses_entries = true;
				if (kind == ENTRY)
					entries[index] = true;
				else
					for (unsigned j = 1; j < 36; j++) entries[j] = true;
			}
			else if (kind == SLOT || kind == ALLSLOTS || kind == ANYSLOTS || (inuse && kind == BACKUP)) {
				if (active && kind == ANYSLOTS)
					return IO_ERROR_INVALID_FORMAT;
				uses_slots = true;
				if (kind == SLOT)
					slots[index] = true;
				else
					for (unsigned j = 0; j < sizeof(slots) / sizeof(slots[0]); j++)
						slots[j] = true;
			}
			else if (!inuse)
				return IO_ERROR_INVALID_FORMAT;
		}
		if (uses_entries && (binding->live == NULL
		    || (type != ST_RIP3_TABLE_PAL && type != ST_RIP3_TABLE_PORT
		    && type != ST_RIP3_TABLE_ENV && type != ST_RIP3_TABLE_STYLE && type != ST_RIP3_TABLE_TW
		    && type != ST_RIP3_TABLE_BUT)))
			return IO_ERROR_NOT_SUPPORTED;
		if ((uses_slots || selected == BASE || selected == STACK) && binding->backup == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		if (active) {
			/* The complete selector list and required bindings are validated.
			 * These bounded protection setters allocate nothing and cannot fail
			 * for valid bound objects and the indexes collected above. */
			for (unsigned i = 0; i < 36; i++) {
				if (!entries[i]) continue;
				if (type == ST_RIP3_TABLE_PAL)
					(void)st_rip3_palettes_protect(binding->live, i, protect);
				else if (type == ST_RIP3_TABLE_PORT)
					(void)st_rip3_ports_protect(binding->live, i, protect);
				else if (type == ST_RIP3_TABLE_STYLE)
					(void)st_rip3_styles_protect(binding->live, i, protect);
				else if (type == ST_RIP3_TABLE_BUT)
					(void)st_rip3_button_styles_protect(binding->live,i,protect);
				else if (type == ST_RIP3_TABLE_TW) {
					const struct st_rip3_text_window_operations *windows=binding->live;
					if (windows->protect==NULL) return IO_ERROR_NOT_SUPPORTED;
					status=windows->protect(windows->context,i,protect);
					if (status!=0) return status;
				}
				else
					(void)st_rip3_environments_protect(binding->live, i, protect);
			}
			for (unsigned i = 0; i < 10; i++) {
				if (slots[i])
					(void)st_rip3_backup_protect(binding->backup, i, protect);
			}
			result[0] = 0;
			return 0;
		}
		{
			unsigned total = 0, matched = 0;
			bool single_defined = false, single_protected = false;

			if (uses_entries) {
				for (unsigned i = 0; i < 36; i++) {
					bool defined, protection;
					if (!entries[i]) continue;
					status = entry_info(type, binding->live, i, &defined, &protection);
					if (status != 0) return status;
					total++; matched += defined && (!isprot || protection);
					single_defined = defined; single_protected = protection;
				}
			}
			else {
				struct st_rip3_backup_info info;

				status = st_rip3_backup_info(binding->backup, &info);
				if (status != 0) return status;
				if (selected == BASE || selected == STACK)
					return reply_number(result, capacity, selected == BASE ? info.base : info.stack != 0);
				if (selected == BACKUP && (info.base || info.stack != 0))
					return reply_number(result, capacity, 1);
				for (unsigned i = 0; i < 10; i++) {
					if (!slots[i]) continue;
					total++; matched += info.slot_used[i] && (!isprot || info.slot_protected[i]);
					single_defined = info.slot_used[i]; single_protected = info.slot_protected[i];
				}
			}
			if (isprot && (selected == ENTRY || selected == CURRENT || selected == SLOT))
				return reply_number(result, capacity, !single_defined && !((type==ST_RIP3_TABLE_PAL || type==ST_RIP3_TABLE_BUT) && selected==CURRENT)
				    ? -1 : single_protected);
			return reply_number(result, capacity, (selected == ALL || selected == ALLSLOTS)
			    ? matched == total : matched != 0);
		}
	}
}

enum table_reset_kind { RESET_ENTRY, RESET_CURRENT, RESET_TABLE, RESET_BASE,
	RESET_SLOT, RESET_SLOTS, RESET_STACK, RESET_BACKUP, RESET_ALL };

static int
table_reset(const struct st_rip3_table_host *host,const struct st_rip3_table_binding *binding,
    const struct st_rip3_builtin_reference *reference, enum st_rip3_table_type type,bool viewport)
{
	struct {enum table_reset_kind kind; unsigned index;} actions[39];
	size_t count=reference->count==1 ? 1 : reference->count-1;
	bool needs_live=false,needs_backup=false;
	/* Validate the complete selector list and every required binding before
	 * executing anything, including a mixed live-table/backup reset. */
	for (size_t i=0; i<count; i++) {
		const char *word=reference->count==1 ? ((type==ST_RIP3_TABLE_MOUSE || type==ST_RIP3_TABLE_SCREEN) ? "TBL" : "CUR") : reference->parameters[i+1].text;
		enum table_reset_kind kind;unsigned index=0;
		if (equal(word,"CUR")) kind=RESET_CURRENT;
		else if (equal(word,"TBL")) kind=RESET_TABLE;
		else if (equal(word,"BASE")) kind=RESET_BASE;
		else if (equal(word,"SLOTS")) kind=RESET_SLOTS;
		else if (equal(word,"STACK")) kind=RESET_STACK;
		else if (equal(word,"BACKUP")) kind=RESET_BACKUP;
		else if (equal(word,"ALL")) kind=RESET_ALL;
		else if ((word[0]=='S' || word[0]=='s') && word[1]>='0' && word[1]<='9' && word[2]==0) {
			kind=RESET_SLOT;index=(unsigned)(word[1]-'0');
		}
		else if (st_rip3_host_number(word,0,35,&index)==0) kind=RESET_ENTRY;
		else return IO_ERROR_INVALID_FORMAT;
		if (viewport && kind!=RESET_ENTRY && kind!=RESET_CURRENT && kind!=RESET_TABLE) return IO_ERROR_INVALID_FORMAT;
		if (type==ST_RIP3_TABLE_MOUSE && (kind==RESET_ENTRY || kind==RESET_CURRENT)) return IO_ERROR_INVALID_FORMAT;
		if (type==ST_RIP3_TABLE_SCREEN && (kind==RESET_ENTRY || kind==RESET_CURRENT || (kind==RESET_TABLE && reference->count!=1))) return IO_ERROR_INVALID_FORMAT;
		actions[i].kind=kind;actions[i].index=index;
		needs_live|=kind==RESET_ENTRY || kind==RESET_CURRENT || kind==RESET_TABLE || kind==RESET_ALL;
		needs_backup|=kind!=RESET_ENTRY && kind!=RESET_CURRENT && kind!=RESET_TABLE;
	}
	if ((needs_live && binding->live==NULL) || (needs_backup && binding->backup==NULL))
		return IO_ERROR_NOT_SUPPORTED;
	if (type==ST_RIP3_TABLE_TW && needs_live) {
		const struct st_rip3_text_window_operations *windows=binding->live;
		if (windows->current==NULL || windows->reset_entry==NULL) return IO_ERROR_NOT_SUPPORTED;
	}
	for (size_t i=0; i<count; i++) {
		enum table_reset_kind kind=actions[i].kind;unsigned index=actions[i].index;
		int status=0;
		if (viewport) {
			if (kind==RESET_CURRENT) status=st_rip3_ports_current(binding->live,&index);
			if (status==0) {
				unsigned numbers[36];size_t number_count=kind==RESET_TABLE ? 36 : 1;
				for (unsigned j=0;j<number_count;j++) numbers[j]=kind==RESET_TABLE ? j : index;
				status=st_rip3_ports_operate_views(binding->live,numbers,number_count,ST_RIP3_VIEW_RESET,0);
			}
		}
		else if (type==ST_RIP3_TABLE_TW) {
			const struct st_rip3_text_window_operations *windows=binding->live;
			if (kind==RESET_CURRENT) status=windows->current(windows->context,&index);
			if (status==0 && (kind==RESET_CURRENT || kind==RESET_ENTRY))
				status=windows->reset_entry(windows->context,index);
			else if (kind==RESET_TABLE || kind==RESET_ALL) {
				for (unsigned number=0;number<36;number++) {
					int failure=windows->reset_entry(windows->context,number);
					if (failure!=0 && host->operation_failed!=NULL) host->operation_failed(host->operation_context,failure);
				}
			}
			/* TW's completed profile continues after each target failure.
			 * CUR is deliberately resolved here, after earlier selectors. */
			if (status!=0 && host->operation_failed!=NULL) host->operation_failed(host->operation_context,status);
			status=0;
		}
		else if (type==ST_RIP3_TABLE_SCREEN) {
			if (kind==RESET_TABLE || kind==RESET_ALL) status=st_rip3_screen_clear(binding->live);
		}
		else if (type==ST_RIP3_TABLE_MOUSE) {
			if (kind==RESET_TABLE || kind==RESET_ALL) st_rip3_mouse_kill(binding->live);
		}
		else if (type==ST_RIP3_TABLE_BUT) {
			if (kind==RESET_CURRENT) status=st_rip3_button_styles_reset_current(binding->live);
			else if (kind==RESET_ENTRY) status=st_rip3_button_styles_reset_entry(binding->live,index);
			else if (kind==RESET_TABLE || kind==RESET_ALL) status=st_rip3_button_styles_reset_table(binding->live);
		}
		else if (type==ST_RIP3_TABLE_PAL) {
			if (kind==RESET_CURRENT) status=st_rip3_palettes_current(binding->live,&index);
			if (status==0 && (kind==RESET_ENTRY || kind==RESET_CURRENT))
				status=st_rip3_palettes_reset_entry(binding->live,index);
			else if (kind==RESET_TABLE || kind==RESET_ALL)
				status=st_rip3_palettes_reset(binding->live,false);
		}
		if (status!=0) return status;
		if (kind==RESET_BASE) status=st_rip3_backup_clear(binding->backup,ST_RIP3_BACKUP_BASE,0);
		else if (kind==RESET_STACK) status=st_rip3_backup_clear(binding->backup,ST_RIP3_BACKUP_STACK,0);
		else if (kind==RESET_SLOT) {
			status=st_rip3_backup_clear(binding->backup,ST_RIP3_BACKUP_SLOT,index);
			if (status==IO_ERROR_PERMISSION_DENIED) status=0;
		}
		else if (kind==RESET_SLOTS) {
			for (unsigned slot=0; slot<10; slot++) {
				status=st_rip3_backup_clear(binding->backup,ST_RIP3_BACKUP_SLOT,slot);
				if (status!=0 && status!=IO_ERROR_PERMISSION_DENIED) return status;
			}
			status=0;
		}
		else if (kind==RESET_BACKUP || kind==RESET_ALL) status=st_rip3_backup_clear_all(binding->backup,false);
		if (status!=0) return status;
	}
	return 0;
}

static int
entry_copy(const struct st_rip3_table_binding *binding,enum st_rip3_table_type type,
    const struct st_rip3_builtin_reference *reference)
{
	unsigned source=0,destinations[38];
	for (size_t i=1; i<reference->count; i++) {
		const char *word=reference->parameters[i].text;unsigned index;
		int status;
		if (equal(word,"CUR")) status=entry_current(type,binding->live,&index);
		else status=st_rip3_host_number(word,0,35,&index);
		if (status!=0) return status;
		if (i==1) source=index;
		else destinations[i-2]=index;
	}
	if (binding->live==NULL) return IO_ERROR_NOT_SUPPORTED;
	switch (type) {
		case ST_RIP3_TABLE_ENV:
			return st_rip3_environments_copy_entries(binding->live,source,destinations,reference->count-2);
		case ST_RIP3_TABLE_STYLE:
			return st_rip3_styles_copy_entries(binding->live,source,destinations,reference->count-2);
		case ST_RIP3_TABLE_BUT:
			return st_rip3_button_styles_copy_entries(binding->live,source,destinations,reference->count-2);
		case ST_RIP3_TABLE_PAL:
			return st_rip3_palettes_copy_entries(binding->live,source,destinations,reference->count-2);
		case ST_RIP3_TABLE_TW: {
			const struct st_rip3_text_window_operations *windows=binding->live;
			return windows->copy_entries==NULL ? IO_ERROR_NOT_SUPPORTED
			    : windows->copy_entries(windows->context,source,destinations,reference->count-2);
		}
		default:
			return IO_ERROR_NOT_SUPPORTED;
	}
}

static int
action_location(const struct st_rip3_builtin_reference *reference,bool save,int fixed,
    struct st_rip3_backup_location *result)
{
	struct st_rip3_backup_location place={ST_RIP3_BACKUP_BASE,0};
	if (fixed>=0) {
		if (reference->count!=0) return IO_ERROR_INVALID_FORMAT;
		place.place=ST_RIP3_BACKUP_SLOT;place.index=(unsigned)fixed;
	}
	else {
		if (reference->count>1) return IO_ERROR_INVALID_FORMAT;
		if (reference->count==1) {
			const char *word=reference->parameters[0].text;
			if (equal(word,"BASE")) place.place=ST_RIP3_BACKUP_BASE;
			else if (equal(word,save ? "PUSH" : "POP")) place.place=ST_RIP3_BACKUP_STACK;
			else {
				if (st_rip3_host_number(word,0,9,&place.index)!=0) return IO_ERROR_INVALID_FORMAT;
				place.place=ST_RIP3_BACKUP_SLOT;
			}
		}
	}
	*result=place;return 0;
}

static int
screen_action(const struct st_rip3_table_host *host,const struct st_rip3_builtin_reference *reference,
    bool save,int fixed,char *result,size_t capacity)
{
	if (capacity==0) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_backup_location place;
	int syntax=action_location(reference,save,fixed,&place);
	if (syntax!=0) return syntax;
	const struct st_rip3_table_binding *binding=&host->tables[ST_RIP3_TABLE_SCREEN];
	if (binding->live==NULL || binding->backup==NULL) return IO_ERROR_NOT_SUPPORTED;
	int status=save ? st_rip3_backup_save(binding->backup,place.place,place.index,binding->live)
	    : st_rip3_backup_restore(binding->backup,place.place,place.index,binding->live);
	/* The action wrapper requests numbered deletion even after restore failure.
	 * Protected snapshots survive; POP consumption stays success-dependent. */
	if (!save && place.place==ST_RIP3_BACKUP_SLOT) (void)st_rip3_backup_clear(binding->backup,place.place,place.index);
	if (status!=0 && host->operation_failed!=NULL) host->operation_failed(host->operation_context,status);
	result[0]=0;return 0;
}

static int
aggregate_action(const struct st_rip3_table_host *host,const struct st_rip3_builtin_reference *reference,
    bool save,char *result,size_t capacity)
{
	static const enum st_rip3_table_type order[]={ST_RIP3_TABLE_TW,ST_RIP3_TABLE_PORT,
	    ST_RIP3_TABLE_MOUSE,ST_RIP3_TABLE_SCREEN,ST_RIP3_TABLE_PAL,ST_RIP3_TABLE_BUT,
	    ST_RIP3_TABLE_STYLE,ST_RIP3_TABLE_ENV};
	if (capacity==0) return IO_ERROR_OUT_OF_RANGE;
	struct st_rip3_backup_location place;
	int status=action_location(reference,save,-1,&place);
	if (status!=0) return status;
	for (size_t i=0;i<sizeof(order)/sizeof(order[0]);i++) {
		const struct st_rip3_table_binding *binding=&host->tables[order[i]];
		if (binding->live==NULL || binding->backup==NULL) status=IO_ERROR_NOT_SUPPORTED;
		else if (save) status=st_rip3_backup_save(binding->backup,place.place,place.index,binding->live);
		else if (place.place==ST_RIP3_BACKUP_SLOT)
			status=st_rip3_backup_restore_slot_retained(binding->backup,place.index,binding->live);
		else status=st_rip3_backup_restore(binding->backup,place.place,place.index,binding->live);
		/* Each family is independent: diagnostics never stop the sequence or
		 * turn a valid action into a variable error. There is no rollback. */
		if (status!=0 && host->operation_failed!=NULL) host->operation_failed(host->operation_context,status);
	}
	if (!save && place.place==ST_RIP3_BACKUP_SLOT) for (size_t i=0;i<sizeof(order)/sizeof(order[0]);i++) {
		struct st_rip3_backup *backup=host->tables[order[i]].backup;
		if (backup!=NULL) (void)st_rip3_backup_clear(backup,place.place,place.index);
	}
	result[0]=0;return 0;
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
	struct st_rip3_builtin_reference words;
	status=st_rip3_builtin_reference_words(&words,reference);
	if (status!=0) return status;
	reference=&words;
	if (strcmp(reference->name,"SAVEALL")==0 || strcmp(reference->name,"RESTOREALL")==0)
		return aggregate_action(host,reference,reference->name[0]=='S',result,capacity);
	if (strcmp(reference->name,"SAVE")==0 || strcmp(reference->name,"RESTORE")==0)
		return screen_action(host,reference,reference->name[0]=='S',-1,result,capacity);
	size_t name_length=strlen(reference->name);
	if (name_length==5 && memcmp(reference->name,"SAVE",4)==0 && reference->name[4]>='0' && reference->name[4]<='9')
		return screen_action(host,reference,true,reference->name[4]-'0',result,capacity);
	if (name_length==8 && memcmp(reference->name,"RESTORE",7)==0 && reference->name[7]>='0' && reference->name[7]<='9')
		return screen_action(host,reference,false,reference->name[7]-'0',result,capacity);
	if (strcmp(reference->name,"RESET")==0) {
		if (capacity==0) return IO_ERROR_OUT_OF_RANGE;
		if (reference->count==0) return IO_ERROR_NOT_SUPPORTED;
		const char *scope = reference->parameters[0].text;
		if (equal(scope, "SOFT") || equal(scope, "HARD")) {
			if (reference->count!=1) return IO_ERROR_INVALID_FORMAT;
			int (*reset)(void *) = equal(scope, "HARD") ? host->hard_reset : host->soft_reset;
			if (reset==NULL) return IO_ERROR_NOT_SUPPORTED;
			status=reset(host->reset_context);
			if (status==0) result[0]=0;
			else if (equal(scope, "HARD") && host->operation_failed != NULL) {
				host->operation_failed(host->operation_context, status);
				result[0] = 0;
				return 0;
			}
			return status;
		}
		enum st_rip3_table_type reset_type;bool viewport=false;
		if (equal(reference->parameters[0].text,"BUT")) reset_type=ST_RIP3_TABLE_BUT;
		else if (equal(reference->parameters[0].text,"PAL")) reset_type=ST_RIP3_TABLE_PAL;
		else if (equal(reference->parameters[0].text,"MOUSE")) reset_type=ST_RIP3_TABLE_MOUSE;
		else if (equal(reference->parameters[0].text,"SCREEN")) reset_type=ST_RIP3_TABLE_SCREEN;
		else if (equal(reference->parameters[0].text,"TW")) reset_type=ST_RIP3_TABLE_TW;
		else if (equal(reference->parameters[0].text,"VIEW")) {reset_type=ST_RIP3_TABLE_PORT;viewport=true;}
		else return IO_ERROR_NOT_SUPPORTED;
		status=table_reset(host,&host->tables[reset_type],reference,reset_type,viewport);
		if (status==0) result[0]=0;
		return status;
	}
	if (strcmp(reference->name, "PALENTRY") == 0)
		return palette_entries(host, reference, result, capacity);
	if (strcmp(reference->name, "CUR") == 0 || strcmp(reference->name, "INUSE") == 0
	    || strcmp(reference->name, "ISPROT") == 0 || strcmp(reference->name, "PROT") == 0
	    || strcmp(reference->name, "UNPROT") == 0) {
		if (capacity == 0)
			return IO_ERROR_OUT_OF_RANGE;
		return objects(host, reference, result, capacity);
	}
	copy = strcmp(reference->name, "COPY") == 0;
	backstat = strcmp(reference->name, "BACKSTAT") == 0;
	save = strcmp(reference->name, "SCP") == 0 || strcmp(reference->name,"SBS")==0 || strcmp(reference->name,"SMF")==0 || strcmp(reference->name,"STW")==0 || strcmp(reference->name,"SCB")==0 || strcmp(reference->name,"SGS")==0 || strcmp(reference->name,"SENV")==0;
	restore = strcmp(reference->name, "RCP") == 0 || strcmp(reference->name,"RBS")==0 || strcmp(reference->name,"RMF")==0 || strcmp(reference->name,"RTW")==0 || strcmp(reference->name,"RCB")==0 || strcmp(reference->name,"RGS")==0 || strcmp(reference->name,"RENV")==0;
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
				status = st_rip3_host_number(parameter, 0, 9, &place.index);
				if (status != 0)
					return status;
				place.place = ST_RIP3_BACKUP_SLOT;
			}
			else {
				status = location(parameter, restore, &place);
				if (status != 0 || place.place == ST_RIP3_BACKUP_TABLE || place.place == ST_RIP3_BACKUP_SLOT)
					return IO_ERROR_INVALID_FORMAT;
			}
		}
		binding = &host->tables[reference->name[1]=='E' ? ST_RIP3_TABLE_ENV : reference->name[1]=='G' ? ST_RIP3_TABLE_STYLE
		    : reference->name[2]=='B' ? ST_RIP3_TABLE_PORT : reference->name[1]=='T' ? ST_RIP3_TABLE_TW : reference->name[1]=='B' ? ST_RIP3_TABLE_BUT
		    : reference->name[1]=='M' ? ST_RIP3_TABLE_MOUSE : ST_RIP3_TABLE_PAL];
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
		if (copy && (type==ST_RIP3_TABLE_PAL || type==ST_RIP3_TABLE_ENV
		    || type==ST_RIP3_TABLE_STYLE || type==ST_RIP3_TABLE_BUT || type==ST_RIP3_TABLE_TW) && (equal(reference->parameters[1].text,"CUR")
		    || (reference->parameters[1].text[0]>='0' && reference->parameters[1].text[0]<='9'))) {
			status=entry_copy(binding,type,reference);
			if (status==0) result[0]=0;
			return status;
		}
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
