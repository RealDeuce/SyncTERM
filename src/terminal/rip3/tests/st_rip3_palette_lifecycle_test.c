/* Independent palette lifecycle/status/copy and parser recovery vectors. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_backup.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_palettes.h"

#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			return 1;                                       \
		}                                                       \
	} while (0)

static int
literal_defaults(const struct st_rip3_palette *p)
{
	/* Independent EGA/default lookup samples, including extended entries. */
	CHECK(p->rgb[0] == 0 && p->rgb[1] == 0xaa && p->rgb[2] == 0xaa00 && p->rgb[12] == 0xff5555
	      && p->rgb[14] == 0xffff55 && p->rgb[15] == 0xffffff && p->rgb[255] == 0xffffff);
	return 0;
}

static int
reset_states(void)
{
	struct st_rip3_palettes    *p = NULL;
	struct st_rip3_palette      color, unchanged;
	struct st_rip3_palette_info info;
	unsigned                    current;
	const uint32_t              edit = 0x123456;
	CHECK(st_rip3_palettes_create(&p) == 0);
	for (unsigned slot = 0; slot < 36; slot++) {
		CHECK(st_rip3_palettes_reset_entry(p, slot) == 0 && st_rip3_palettes_info(p, slot, &info) == 0
		      && info.defined == (slot == 0) && !info.protected);
		CHECK(st_rip3_palettes_switch(p, slot) == 0 && st_rip3_palettes_rgb(p, 0, 8, &edit, 1) == 0);
		CHECK(st_rip3_palettes_reset_entry(p, slot) == 0 && st_rip3_palettes_current(p, &current) == 0
		      && current == slot);
		CHECK(st_rip3_palettes_current_palette(p, &color) == 0 && literal_defaults(&color) == 0);
		if (slot != 0) {
			unchanged = color;
			CHECK(st_rip3_palettes_get(p, slot, &color) == IO_ERROR_NOT_FOUND
			      && memcmp(&color, &unchanged, sizeof(color)) == 0);
			CHECK(
			    st_rip3_palettes_switch(p, slot) == 0 && st_rip3_palettes_protect(p, slot, true) == 0);
			CHECK(st_rip3_palettes_reset_entry(p, slot) == IO_ERROR_PERMISSION_DENIED);
			CHECK(st_rip3_palettes_info(p, slot, &info) == 0 && info.defined && info.protected);
			CHECK(st_rip3_palettes_protect(p, slot, false) == 0);
		}
		CHECK(st_rip3_palettes_rgb(p, 0, 8, &edit, 1) == 0);
	}
	CHECK(st_rip3_palettes_protect(p, 5, true) == 0 && st_rip3_palettes_reset(p, false) == 0);
	CHECK(st_rip3_palettes_current(p, &current) == 0 && current == 35);
	for (unsigned slot = 0; slot < 36; slot++) {
		CHECK(st_rip3_palettes_info(p, slot, &info) == 0 && info.defined == (slot == 0 || slot == 5)
		      && info.protected == (slot == 5));
		if (slot == 5)
			CHECK(st_rip3_palettes_get(p, slot, &color) == 0 && color.rgb[0] == edit);
	}
	CHECK(st_rip3_palettes_current_palette(p, &color) == 0 && literal_defaults(&color) == 0);
	CHECK(st_rip3_palettes_reset(p, true) == 0 && st_rip3_palettes_current(p, &current) == 0 && current == 0);
	for (unsigned slot = 0; slot < 36; slot++) {
		CHECK(
		    st_rip3_palettes_info(p, slot, &info) == 0 && info.defined == (slot == 0) && !info.protected);
		CHECK(st_rip3_palettes_switch(p, slot) == 0 && st_rip3_palettes_get(p, slot, &color) == 0
		      && literal_defaults(&color) == 0);
	}
	CHECK(st_rip3_palettes_reset_entry(p, 36) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palettes_reset_entry(NULL, 0) == IO_ERROR_INVALID_ARGUMENT
	      && st_rip3_palettes_reset(NULL, false) == IO_ERROR_INVALID_ARGUMENT
	      && st_rip3_palettes_current_palette(p, NULL) == IO_ERROR_INVALID_ARGUMENT);
	st_rip3_palettes_destroy(p);
	return 0;
}

static int
copies(void)
{
	struct st_rip3_palettes    *p = NULL;
	struct st_rip3_palette      color;
	struct st_rip3_palette_info info;
	const uint32_t              edit           = 0x123456;
	const unsigned              destinations[] = {0, 6, 8, 6};
	unsigned                    current;
	CHECK(st_rip3_palettes_create(&p) == 0 && st_rip3_palettes_switch(p, 5) == 0
	      && st_rip3_palettes_rgb(p, 2, 8, &edit, 1) == 0 && st_rip3_palettes_protect(p, 5, true) == 0);
	CHECK(st_rip3_palettes_copy_entries(p, 5, destinations, 4) == 0);
	for (size_t i = 0; i < 4; i++)
		CHECK(st_rip3_palettes_info(p, destinations[i], &info) == 0 && info.defined
		      && info.protected == (destinations[i] != 0)
		      && st_rip3_palettes_get(p, destinations[i], &color) == 0 && color.rgb[2] == edit);
	CHECK(st_rip3_palettes_current(p, &current) == 0 && current == 5);
	const unsigned bad[][2] = {
	    {1, 8 },
            {1, 5 },
            {1, 36}
        };
	for (size_t i = 0; i < 3; i++) {
		CHECK(st_rip3_palettes_copy_entries(p, 5, bad[i], 2) != 0);
		CHECK(st_rip3_palettes_info(p, 1, &info) == 0 && !info.defined);
	}
	CHECK(st_rip3_palettes_copy_entries(p, 2, destinations, 4) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_palettes_copy_entries(p, 36, destinations, 4) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_palettes_copy_entries(p, 5, NULL, 1) == IO_ERROR_INVALID_ARGUMENT
	      && st_rip3_palettes_copy_entries(p, 5, destinations, 0) == IO_ERROR_INVALID_ARGUMENT);
	st_rip3_palettes_destroy(p);
	return 0;
}

struct observation {
	char                         output[4000];
	size_t                       length;
	unsigned                     errors;
	enum st_rip3_host_diagnostic kind;
};

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct observation *o = context;
	if (length < sizeof(o->output) - o->length) {
		memcpy(o->output + o->length, bytes, length);
		o->length += length;
		o->output[o->length] = 0;
	}
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct observation *o = context;
	o->errors++;
	o->kind = kind;
}

static int
run(const struct st_rip3_host_variables *host, const char *wire, const char *expected, unsigned errors)
{
	struct observation             o    = {0};
	const struct st_rip3_host_sink sink = {&o, reply, diagnostic};
	st_rip3_host_variables_process(host, &sink, (const uint8_t *)wire, strlen(wire));
	CHECK(o.errors == errors && strcmp(o.output, expected) == 0);
	return 0;
}

static int
host_commands(void)
{
	struct st_rip3_palettes            *p      = NULL;
	struct st_rip3_backup              *backup = NULL;
	struct st_rip3_table_host           tables = {0};
	const struct st_rip3_host_variables host   = {.tables = &tables};
	struct st_rip3_palette              color;
	struct st_rip3_palette_info         info;
	struct st_rip3_backup_info          saved;
	CHECK(st_rip3_palettes_create(&p) == 0 && st_rip3_palettes_backup_create(&backup) == 0);
	tables.tables[ST_RIP3_TABLE_PAL] = (struct st_rip3_table_binding) {p, backup};
	CHECK(run(&host, "$CUR(PAL,5)$", "", 0) == 0 && run(&host, "$RESET(PAL)$", "", 0) == 0);
	CHECK(run(&host, "$CUR(PAL)$", "5", 0) == 0 && run(&host, "$INUSE(PAL,5)$", "0", 0) == 0
	      && run(&host, "$ISPROT(PAL,5)$", "-1", 0) == 0 && run(&host, "$ISPROT(PAL,CUR)$", "0", 0) == 0);
	CHECK(run(&host, "$PALENTRY(5,0)$", "-1", 0) == 0 && run(&host, "$CUR(PAL,5)$", "", 0) == 0);
	CHECK(run(&host, "$PROT(PAL,5)$", "", 0) == 0 && run(&host, "$COPY(PAL,CUR,6,8,0)$", "", 0) == 0
	      && run(&host, "$ISPROT(PAL,6)$", "1", 0) == 0 && run(&host, "$ISPROT(PAL,0)$", "", 1) == 0);
	CHECK(st_rip3_palettes_info(p, 0, &info) == 0 && info.defined && !info.protected);
	CHECK(run(&host, "$COPY(PAL,5,1,6,2)$", "", 1) == 0 && run(&host, "$INUSE(PAL,1)$", "0", 0) == 0);
	CHECK(run(&host, "$RESET(PAL,5)$", "", 1) == 0 && run(&host, "$INUSE(PAL,5)$", "1", 0) == 0);
	CHECK(run(&host, "$CUR(PAL,1)$", "", 0) == 0 && run(&host, "$COPY(PAL,5,CUR)$", "", 0) == 0
	      && run(&host, "$ISPROT(PAL,CUR)$", "1", 0) == 0);
	CHECK(run(&host, "$SCP$", "", 0) == 0 && run(&host, "$SCP(0)$", "", 0) == 0
	      && run(&host, "$SCP(1)$", "", 0) == 0 && run(&host, "$SCP(PUSH)$", "", 0) == 0
	      && run(&host, "$PROT(PAL,S1)$", "", 0) == 0);
	const char *invalid[] = {"$RESET(PAL,TBL,S0,BAD)$", "$RESET(PAL,BASE,S10)$", "$RESET(PAL,36)$",
	    "$RESET(PAL,TBL,ANY)$", "$COPY(PAL,5,2,36)$", "$COPY(PAL,5,2,5)$", "$COPY(PAL,5,2,BASE)$"};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		CHECK(run(&host, invalid[i], "", 1) == 0);
		CHECK(st_rip3_backup_info(backup, &saved) == 0 && saved.base && saved.stack == 1
		      && saved.slot_used[0] && saved.slot_used[1]);
		CHECK(st_rip3_palettes_info(p, 2, &info) == 0 && !info.defined);
	}
	CHECK(run(&host, "$RESET(PAL,BASE)$", "", 0) == 0 && st_rip3_backup_info(backup, &saved) == 0
	      && !saved.base && saved.stack == 1 && saved.slots == 2);
	CHECK(run(&host, "$SCP$", "", 0) == 0 && run(&host, "$RESET(PAL,STACK)$", "", 0) == 0
	      && st_rip3_backup_info(backup, &saved) == 0 && saved.base && saved.stack == 0 && saved.slots == 2);
	CHECK(run(&host, "$SCP(PUSH)$", "", 0) == 0 && run(&host, "$RESET(PAL,S0,S1)$", "", 0) == 0
	      && st_rip3_backup_info(backup, &saved) == 0 && saved.base && saved.stack == 1 && !saved.slot_used[0]
	      && saved.slot_used[1]);
	CHECK(run(&host, "$SCP(0)$", "", 0) == 0);
	/* SLOTS and BACKUP preserve protected numbered saves, independently of
	 * protected live records. Later valid references recover on the same host. */
	CHECK(run(&host, "$rEsEt(pAl, sLoTs)$", "", 0) == 0 && st_rip3_backup_info(backup, &saved) == 0
	      && saved.base && saved.stack == 1 && !saved.slot_used[0] && saved.slot_used[1]);
	CHECK(run(&host, "$RESET(PAL,BACKUP)$", "", 0) == 0 && st_rip3_backup_info(backup, &saved) == 0
	      && !saved.base && saved.stack == 0 && saved.slot_used[1]);
	CHECK(run(&host, "$RESET(PAL,ALL)$", "", 0) == 0 && run(&host, "$INUSE(PAL,6)$", "1", 0) == 0);
	CHECK(run(&host, "$UNPROT(PAL,ALL,S1)$", "", 0) == 0 && run(&host, "$RESET(PAL,ALL)$", "", 0) == 0);
	CHECK(run(&host, "$INUSE(PAL,ANY)$", "0", 0) == 0 && st_rip3_backup_info(backup, &saved) == 0
	      && saved.slots == 0);
	CHECK(run(&host, "$CUR(PAL)$", "1", 0) == 0 && run(&host, "$ISPROT(PAL,CUR)$", "0", 0) == 0
	      && st_rip3_palettes_current_palette(p, &color) == 0 && literal_defaults(&color) == 0);
	/* Validating missing bindings or a zero reply capacity cannot partially
	 * clear the live table. Backup-only resets require no live binding. */
	CHECK(run(&host, "$CUR(PAL,3)$", "", 0) == 0);
	tables.tables[ST_RIP3_TABLE_PAL].backup = NULL;
	CHECK(run(&host, "$RESET(PAL,TBL,BASE)$", "", 1) == 0 && run(&host, "$INUSE(PAL,3)$", "1", 0) == 0);
	struct st_rip3_builtin_reference ref;
	char                             output = 'Q';
	CHECK(st_rip3_builtin_reference_parse(&ref, (const uint8_t *)"$RESET(PAL,TBL)$", 16) == 0
	      && st_rip3_table_host_execute(&tables, &ref, &output, 0) == IO_ERROR_OUT_OF_RANGE && output == 'Q');
	CHECK(run(&host, "$INUSE(PAL,3)$", "1", 0) == 0);
	tables.tables[ST_RIP3_TABLE_PAL] = (struct st_rip3_table_binding) {NULL, backup};
	CHECK(run(&host, "$RESET(PAL,BASE,STACK,S0)$", "", 0) == 0);
	st_rip3_backup_destroy(backup);
	st_rip3_palettes_destroy(p);
	return 0;
}

int
main(void)
{
	CHECK(reset_states() == 0 && copies() == 0 && host_commands() == 0);
	return 0;
}
