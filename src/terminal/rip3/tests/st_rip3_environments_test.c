/* Literal environment settings, frame associations and nonfatal recovery. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_environments.h"
#include "st_rip3_backup.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_table_host.h"
#include "io_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static bool
same(const struct st_rip3_environment *a, const struct st_rip3_environment *b)
{
	return a->graphics_style == b->graphics_style && a->button_style == b->button_style
	    && a->port == b->port && a->text_window == b->text_window && a->palette == b->palette
	    && a->world_width == b->world_width && a->world_height == b->world_height
	    && a->base_math == b->base_math && a->coordinate_size == b->coordinate_size
	    && a->rgb_bits == b->rgb_bits && a->mouse_pointer == b->mouse_pointer
	    && a->baud_rate == b->baud_rate;
}

static struct st_rip3_environment
custom(void)
{
	return (struct st_rip3_environment){.graphics_style = 1, .button_style = 2,
	    .port = 3, .text_window = 4, .palette = 5, .world_width = 1000,
	    .world_height = 1000, .base_math = 64, .coordinate_size = 5,
	    .rgb_bits = 8, .mouse_pointer = 6, .baud_rate = UINT64_C(123456789012)};
}

static int
records(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_environments *environments = NULL, *other = NULL;
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_environment defaults, value, initial = custom();
	struct st_rip3_environment_info info;
	struct st_rip3_backup_info saved;
	struct st_rip3_frame frame;
	struct st_rip3_point middle = {500,500}, device;
	unsigned current;

	CHECK(st_rip3_environment_defaults(&defaults) == 0);
	CHECK(defaults.world_width == 640 && defaults.world_height == 350
	    && defaults.base_math == 36 && defaults.coordinate_size == 2
	    && defaults.rgb_bits == 0 && defaults.mouse_pointer == 0 && defaults.baud_rate == 0);
	CHECK(st_rip3_environments_create(&environments, canvas) == 0);
	CHECK(st_rip3_environments_current(environments, &current) == 0 && current == 0);
	for (unsigned i = 0; i < 36; i++) {
		CHECK(st_rip3_environments_info(environments, i, &info) == 0);
		CHECK(info.defined == (i == 0) && !info.protected);
	}
	value = initial;
	CHECK(st_rip3_environments_get(environments, 35, &value) == IO_ERROR_NOT_FOUND && same(&value, &initial));
	CHECK(st_rip3_environments_set(environments, 35, &initial) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_environments_select(environments, 35) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_environments_define(environments, 35, &initial) == 0);
	CHECK(st_rip3_environments_select(environments, 35) == 0);
	CHECK(st_rip3_environments_frame(environments, 35, &frame) == 0);
	CHECK(frame.device_width == width && frame.device_height == height
	    && frame.world_width == 1000 && frame.world_height == 1000);
	CHECK(st_rip3_world_to_device(&frame, &middle, &device) == 0);
	CHECK(device.x == (int32_t)width / 2 && device.y == (int32_t)height / 2);
	CHECK(st_rip3_environments_get(environments, 0, &value) == 0 && same(&value, &defaults));
	CHECK(st_rip3_environments_protect(environments, 35, true) == 0);
	CHECK(st_rip3_environments_set(environments, 35, &defaults) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_environments_define(environments, 35, &defaults) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_environments_protect(environments, 0, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_environments_protect(environments, 2, true) == 0);
	CHECK(st_rip3_environments_info(environments, 2, &info) == 0 && !info.defined && !info.protected);
	CHECK(st_rip3_environments_backup_create(&backup) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 9, environments) == 0);
	CHECK(st_rip3_environments_create(&other, canvas == ST_RIP3_CANVAS_640_480
	    ? ST_RIP3_CANVAS_800_600 : ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 9, other) == IO_ERROR_CONFLICT);
	CHECK(st_rip3_backup_info(backup, &saved) == 0 && saved.slot_used[9]);
	st_rip3_environments_destroy(other);
	CHECK(st_rip3_environments_protect(environments, 35, false) == 0);
	CHECK(st_rip3_environments_set(environments, 35, &defaults) == 0);
	CHECK(st_rip3_environments_define(environments, 2, &initial) == 0);
	CHECK(st_rip3_environments_select(environments, 0) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 9, environments) == 0);
	CHECK(st_rip3_environments_current(environments, &current) == 0 && current == 35);
	CHECK(st_rip3_environments_get(environments, 35, &value) == 0 && same(&value, &initial));
	CHECK(st_rip3_environments_info(environments, 35, &info) == 0 && info.protected);
	CHECK(st_rip3_environments_info(environments, 2, &info) == 0 && !info.defined);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, environments) == 0);
	st_rip3_environments_destroy(environments);
	CHECK(st_rip3_environments_create(&environments, canvas) == 0);
	CHECK(st_rip3_environments_define(environments, 35, &defaults) == 0);
	CHECK(st_rip3_environments_protect(environments, 35, true) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, environments) == 0);
	CHECK(st_rip3_environments_get(environments, 35, &value) == 0 && same(&value, &initial));
	CHECK(st_rip3_environments_current(environments, &current) == 0 && current == 35);
	CHECK(st_rip3_environments_protect(environments, 35, false) == 0);
	for (unsigned i = 0; i < 12; i++) {
		value = initial;
		switch (i) {
			case 0: value.world_width = 65536; break;
			case 1: value.world_height = 65536; break;
			case 2: value.base_math = 35; break;
			case 3: value.coordinate_size = 1; break;
			case 4: value.coordinate_size = 6; break;
			case 5: value.rgb_bits = 9; break;
			case 6: value.mouse_pointer = 7; break;
			case 7: value.graphics_style = 36; break;
			case 8: value.button_style = 36; break;
			case 9: value.port = 36; break;
			case 10: value.text_window = 36; break;
			case 11: value.palette = 36; break;
		}
		CHECK(st_rip3_environments_define(environments, 1, &value) == IO_ERROR_OUT_OF_RANGE);
		CHECK(st_rip3_environments_info(environments, 1, &info) == 0 && !info.defined);
		CHECK(st_rip3_environments_set(environments, 35, &value) == IO_ERROR_OUT_OF_RANGE);
		CHECK(st_rip3_environments_get(environments, 35, &value) == 0 && same(&value, &initial));
	}
	value = initial; value.world_width = 1; value.world_height = 32767; value.baud_rate = UINT64_MAX;
	CHECK(st_rip3_environments_set(environments, 35, &value) == 0);
	CHECK(st_rip3_environments_get(environments, 35, &initial) == 0 && same(&value, &initial));
	CHECK(st_rip3_environments_get(environments, 36, &value) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_environments_create(NULL, canvas) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_environments_select(environments, 36) == IO_ERROR_OUT_OF_RANGE);
	st_rip3_backup_destroy(backup);
	st_rip3_environments_destroy(environments);
	return 0;
}

struct observation {
	char reply[100];
	size_t length;
	unsigned errors;
	enum st_rip3_host_diagnostic diagnostic;
};

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct observation *value = context;

	if (length < sizeof(value->reply)) {
		memcpy(value->reply, bytes, length);
		value->reply[length] = 0;
		value->length = length;
	}
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic code)
{
	struct observation *value = context;
	value->errors++;
	value->diagnostic = code;
}

static int
run(const struct st_rip3_host_variables *host, const char *text, const char *expected,
    int error)
{
	struct observation result = {0};
	const struct st_rip3_host_sink sink = {&result,reply,diagnostic};

	st_rip3_host_variables_process(host, &sink, (const uint8_t *)text, strlen(text));
	CHECK(strcmp(result.reply, expected) == 0);
	CHECK(result.errors == (unsigned)(error >= 0));
	if (error >= 0)
		CHECK(result.diagnostic == (enum st_rip3_host_diagnostic)error);
	return 0;
}

static int
color_precisions(void)
{
	struct st_rip3_environments *environments=NULL;
	struct st_rip3_table_host tables={0};
	const struct st_rip3_host_variables host={.tables=&tables};
	CHECK(st_rip3_environments_create(&environments,ST_RIP3_CANVAS_640_480)==0);
	tables.tables[ST_RIP3_TABLE_ENV].live=environments;
	struct st_rip3_environment initial;
	CHECK(st_rip3_environment_defaults(&initial)==0);
	CHECK(st_rip3_environments_define(environments,35,&initial)==0
	    && st_rip3_environments_protect(environments,35,true)==0);
	for (unsigned slot=0;slot<=35;slot+=35) for (unsigned bits=1;bits<=8;bits++) {
		char wire[80],expected[8];
		(void)snprintf(wire,sizeof(wire),"$COLORMODE(%u,RGB,%u)$",slot,bits);
		CHECK(run(&host,wire,"",-1)==0);
		(void)snprintf(wire,sizeof(wire),"$COLORMODE(%u)$",slot);
		(void)snprintf(expected,sizeof(expected),"%u",bits);
		CHECK(run(&host,wire,expected,-1)==0);
		CHECK(run(&host,"$CUR(ENV)$","0",-1)==0);
	}
	CHECK(st_rip3_environments_select(environments,35)==0);
	CHECK(run(&host,"$COLORMODE(CUR,RGB,3)$","",-1)==0
	    && run(&host,"$COLORMODE$","3",-1)==0
	    && run(&host,"$COLORMODE(0)$","8",-1)==0);
	CHECK(st_rip3_environments_select(environments,0)==0);
	CHECK(run(&host,"$COLORMODE(35,PAL)$","",-1)==0
	    && run(&host,"$COLORMODE$","8",-1)==0);
	CHECK(run(&host,"$ISPROT(ENV,35)$","1",-1)==0);
	CHECK(run(&host,"$COLORMODE(6)$","-1",-1)==0);
	CHECK(run(&host,"$INUSE(ENV,6)$","0",-1)==0);
	CHECK(run(&host,"$COLORMODE(6,RGB,1)$","",ST_RIP3_HOST_INVALID_REFERENCE)==0);
	const char *invalid[]={"$COLORMODE(CUR,RGB,0)$","$COLORMODE(CUR,RGB,9)$",
	    "$COLORMODE(CUR,RGB,-1)$","$COLORMODE(CUR,PAL,8)$","$COLORMODE(CUR,RGB)$"};
	for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
		CHECK(run(&host,invalid[i],"",ST_RIP3_HOST_INVALID_REFERENCE)==0);
		CHECK(run(&host,"$COLORMODE$","8",-1)==0);
	}
	st_rip3_environments_destroy(environments);return 0;
}

static int
world_ranges(void)
{
	static const unsigned input[]={0,1,32767,32768,65535,65536,65537};
	static const unsigned stored[]={0,1,32767,32768,65535,0,1};
	struct st_rip3_environments *environments=NULL;
	struct st_rip3_table_host tables={0};
	const struct st_rip3_host_variables host={.tables=&tables};
	CHECK(st_rip3_environments_create(&environments,ST_RIP3_CANVAS_640_480)==0);
	tables.tables[ST_RIP3_TABLE_ENV].live=environments;
	for (unsigned slot=0;slot<=35;slot+=35) {
		struct st_rip3_environment defaults;
		CHECK(st_rip3_environment_defaults(&defaults)==0);
		CHECK(st_rip3_environments_define(environments,35,&defaults)==0);
		for (size_t x=0;x<7;x++) for (size_t y=0;y<7;y++) {
			CHECK(st_rip3_environments_set(environments,slot,&defaults)==0);
			char wire[100],expected[100];
			(void)snprintf(wire,sizeof(wire),"$WORLD(%u,%u,%u)$",slot,input[x],input[y]);
			bool rejected=input[x]>65535 && input[y]>65535;
			CHECK(run(&host,wire,"",rejected ? ST_RIP3_HOST_INVALID_REFERENCE : -1)==0);
			(void)snprintf(wire,sizeof(wire),"$WORLD(%u)$",slot);
			(void)snprintf(expected,sizeof(expected),"%u:%u",rejected ? 640 : stored[x],rejected ? 350 : stored[y]);
			CHECK(run(&host,wire,expected,-1)==0);
		}
	}
	static const char *targets[]={"CUR","cur","0","35","6","BOGUS"};
	for (unsigned current=0;current<=35;current+=35) {
		CHECK(st_rip3_environments_select(environments,current)==0);
		for (unsigned axis=0;axis<2;axis++) for (size_t target=0;target<6;target++) for (size_t n=0;n<7;n++) {
			CHECK(run(&host,"$WORLD(CUR,640,350)$","",-1)==0);
			char wire[100],expected[100];
			(void)snprintf(wire,sizeof(wire),"$WORLD%c(%s,%u)$",axis==0 ? 'W' : 'H',targets[target],input[n]);
			bool rejected=target<2 || input[n]>65535;
			CHECK(run(&host,wire,"",rejected ? ST_RIP3_HOST_INVALID_REFERENCE : -1)==0);
			(void)snprintf(expected,sizeof(expected),"%u:%u",!rejected && axis==0 ? input[n] : 640,
			    !rejected && axis==1 ? input[n] : 350);
			CHECK(run(&host,"$WORLD$",expected,-1)==0);
			CHECK(run(&host,"$WORLD(6)$","-1",-1)==0);
		}
	}
	st_rip3_environments_destroy(environments);return 0;
}

static int
host_variables(void)
{
	struct st_rip3_environments *environments = NULL;
	struct st_rip3_environment data = custom();
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_table_host tables = {0};
	const struct st_rip3_host_variables host = {.tables = &tables};

	CHECK(st_rip3_environments_create(&environments, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_environments_define(environments, 35, &data) == 0);
	CHECK(st_rip3_environments_backup_create(&backup) == 0);
	tables.tables[ST_RIP3_TABLE_ENV] = (struct st_rip3_table_binding){environments,backup};
	CHECK(run(&host, "$BASEMATH$", "36", -1) == 0);
	CHECK(run(&host, "$COORDSIZE$", "2", -1) == 0);
	CHECK(run(&host, "$WORLD$", "640:350", -1) == 0);
	CHECK(run(&host, "$WORLDW$", "640", -1) == 0);
	CHECK(run(&host, "$WORLDH$", "350", -1) == 0);
	CHECK(run(&host, "$COLORMODE$", "0", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL$", "0", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL(CUR,9600)$", "", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL$", "9600", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL(CUR)$", "9600", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL(CUR,2400)$", "", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL$", "2400", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL(CUR,0)$", "", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL$", "0", -1) == 0);
	CHECK(run(&host, "$BASEMATH(35)$", "64", -1) == 0);
	CHECK(run(&host, "$COORDSIZE(35)$", "5", -1) == 0);
	CHECK(run(&host, "$WORLD(35)$", "1000:1000", -1) == 0);
	CHECK(run(&host, "$COLORMODE(35)$", "8", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL(35)$", "123456789012", -1) == 0);
	CHECK(run(&host, "$BASEMATH(3)$", "NONE", -1) == 0);
	CHECK(run(&host, "$COORDSIZE(3)$", "-1", -1) == 0);
	CHECK(run(&host, "$WORLD(3)$", "-1", -1) == 0);
	CHECK(run(&host, "$WORLDW(3)$", "-1", -1) == 0);
	CHECK(run(&host, "$WORLDH(3)$", "-1", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL(3)$", "NONE", -1) == 0);
	CHECK(run(&host, "$COLORMODE(3)$", "-1", -1) == 0);
	CHECK(run(&host, "$basemath(cur,64)$", "", -1) == 0);
	CHECK(run(&host, "$WORLD(CUR,DEFAULT)$", "", -1) == 0);
	CHECK(run(&host, "$WORLD$", "4096:3072", -1) == 0);
	CHECK(run(&host, "$BASEMATH(CUR,36)$", "", -1) == 0);
	CHECK(run(&host, "$WORLD(CUR,default)$", "", -1) == 0);
	CHECK(run(&host, "$WORLD$", "1280:960", -1) == 0);
	CHECK(run(&host, "$WORLD(CUR,2000,1000)$", "", -1) == 0);
	CHECK(run(&host, "$WORLD(CUR,32767,1)$", "", -1) == 0);
	CHECK(run(&host, "$WORLD$", "32767:1", -1) == 0);
	CHECK(run(&host, "$WORLD(CUR,1000,1000)$", "", -1) == 0);
	CHECK(run(&host, "$WORLDW(BOGUS,1234)$", "", -1) == 0);
	CHECK(run(&host, "$WORLDH(35,777)$", "", -1) == 0);
	CHECK(run(&host, "$WORLD$", "1234:777", -1) == 0);
	CHECK(run(&host, "$COORDSIZE(CUR,5)$", "", -1) == 0);
	CHECK(run(&host, "$COLORMODE(CUR,RGB,8)$", "", -1) == 0);
	CHECK(run(&host, "$COLORMODE(CUR,RGB,6)$", "", -1) == 0);
	CHECK(run(&host, "$COLORMODE$", "6", -1) == 0);
	CHECK(run(&host, "$COLORMODE(CUR,PAL)$", "", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL(CUR,999999999999)$", "", -1) == 0);
	CHECK(run(&host, "$BAUDEMUL$", "999999999999", -1) == 0);
	{
		struct st_rip3_builtin_reference reference;
		struct st_rip3_environment before, after;
		char output[30], untouched[30];
		const char *set = "$WORLD(CUR,44,55)$", *get = "$WORLD$";

		memset(output, 'Q', sizeof(output)); memcpy(untouched, output, sizeof(output));
		CHECK(st_rip3_environments_get(environments, 0, &before) == 0);
		CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)set, strlen(set)) == 0);
		CHECK(st_rip3_environment_host_execute(&host, &reference, output, 0) == IO_ERROR_OUT_OF_RANGE);
		CHECK(memcmp(output, untouched, sizeof(output)) == 0);
		CHECK(st_rip3_environments_get(environments, 0, &after) == 0 && same(&before, &after));
		CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)get, strlen(get)) == 0);
		CHECK(st_rip3_environment_host_execute(&host, &reference, output, 8) == IO_ERROR_OUT_OF_RANGE);
		CHECK(memcmp(output, untouched, sizeof(output)) == 0);
		CHECK(st_rip3_environment_host_execute(NULL, &reference, output, sizeof(output)) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_environment_host_execute(&host, NULL, output, sizeof(output)) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_environment_host_execute(&host, &reference, NULL, sizeof(output)) == IO_ERROR_INVALID_ARGUMENT);
	}
	static const char *invalid[] = {"$WORLD(CUR,65536,65536)$", "$WORLD(CUR,-1,1)$",
	    "$WORLD(CUR,18446744073709551616,1)$", "$WORLD(CUR,10)$", "$WORLD(CUR,DEFAULT,1)$",
	    "$WORLD(CUR,20,BAD)$", "$WORLDH(CUR,32768)$", "$WORLDW(CUR,-1)$",
	    "$BASEMATH(CUR,63)$", "$COORDSIZE(CUR,1)$", "$COORDSIZE(CUR,6)$",
	    "$COLORMODE(CUR,RGB)$", "$COLORMODE(CUR,PAL,8)$", "$COLORMODE(CUR,RGB,9)$",
	    "$BAUDEMUL(CUR,18446744073709551616)$", "$BAUDEMUL(CUR,-1)$", "$WORLD(3,100,100)$",
	    "$COMPAT(3)$", "$WORLD(36)$", "$WORLD(CUR,1,2,3)$", "$BASEMATH(,36)$"};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		struct st_rip3_environment before, after;
		CHECK(st_rip3_environments_get(environments, 0, &before) == 0);
		CHECK(run(&host, invalid[i], "", ST_RIP3_HOST_INVALID_REFERENCE) == 0);
		CHECK(st_rip3_environments_get(environments, 0, &after) == 0 && same(&before, &after));
		CHECK(run(&host, "$WORLD$", "1234:777", -1) == 0);
	}
	CHECK(run(&host, "$WORLDX$", "", ST_RIP3_HOST_UNKNOWN_REFERENCE) == 0);
	/* BAUDEMUL permits arbitrary nonnegative rates; thirteen digits are not
	 * a syntax error. Its concrete storage still checks uint64_t overflow. */
	CHECK(run(&host,"$BAUDEMUL(CUR,1000000000000)$","",-1)==0);
	CHECK(run(&host,"$BAUDEMUL$","1000000000000",-1)==0);
	CHECK(run(&host, "$CUR(ENV)$", "0", -1) == 0);
	CHECK(run(&host, "$CUR(ENV,35)$", "", ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(run(&host, "$INUSE(ENV,35)$", "1", -1) == 0);
	CHECK(run(&host, "$INUSE(ENV,ANY)$", "1", -1) == 0);
	CHECK(run(&host, "$INUSE(ENV,ALL)$", "0", -1) == 0);
	CHECK(run(&host, "$PROT(ENV,35,BAD)$", "", ST_RIP3_HOST_INVALID_REFERENCE) == 0);
	CHECK(run(&host, "$ISPROT(ENV,35)$", "0", -1) == 0);
	CHECK(run(&host, "$PROT(ENV,35)$", "", -1) == 0);
	CHECK(run(&host, "$ISPROT(ENV,35)$", "1", -1) == 0);
	CHECK(run(&host, "$BASEMATH(35,36)$", "", ST_RIP3_HOST_OPERATION_FAILED) == 0);
	CHECK(run(&host, "$BASEMATH(35)$", "64", -1) == 0);
	CHECK(run(&host, "$UNPROT(ENV,35)$", "", -1) == 0);
	CHECK(run(&host, "$COMPAT(35)$", "", -1) == 0);
	CHECK(st_rip3_environments_get(environments, 35, &data) == 0);
	CHECK(data.world_width == 640 && data.world_height == 350 && data.base_math == 36
	    && data.coordinate_size == 2 && data.rgb_bits == 0 && data.baud_rate == 0
	    && data.port == 3 && data.palette == 5 && data.mouse_pointer == 6);
	CHECK(st_rip3_environments_select(environments, 35) == 0);
	CHECK(run(&host, "$SENV$", "", ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(run(&host, "$INUSE(ENV,BASE)$", "0", -1) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, environments) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, environments) == 0);
	CHECK(run(&host, "$INUSE(ENV,BASE)$", "1", -1) == 0);
	CHECK(run(&host, "$BASEMATH(CUR,64)$", "", -1) == 0);
	CHECK(st_rip3_environments_select(environments, 0) == 0);
	CHECK(run(&host, "$RENV(POP)$", "", ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(run(&host, "$INUSE(ENV,STACK)$", "1", -1) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_STACK, 0, environments) == 0);
	CHECK(run(&host, "$CUR(ENV)$", "35", -1) == 0);
	CHECK(run(&host, "$BASEMATH$", "36", -1) == 0);
	CHECK(run(&host, "$COMPAT$", "", -1) == 0);
	CHECK(run(&host, "$WORLD$", "640:350", -1) == 0);
	CHECK(run(&host, "$COPY(ENV,TBL,S0)$", "", -1) == 0);
	CHECK(run(&host, "$COORDSIZE(CUR,4)$", "", -1) == 0);
	CHECK(run(&host, "$COPY(ENV,S0,TBL)$", "", -1) == 0);
	CHECK(run(&host, "$COORDSIZE$", "2", -1) == 0);
	tables.tables[ST_RIP3_TABLE_ENV].live = NULL;
	CHECK(run(&host, "$WORLD$", "", ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	st_rip3_backup_destroy(backup);
	st_rip3_environments_destroy(environments);
	return 0;
}

int
main(void)
{
	CHECK(color_precisions() == 0);
	CHECK(world_ranges() == 0);
	CHECK(records(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(records(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(records(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	CHECK(host_variables() == 0);
	return 0;
}
