/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_backup.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_palettes.h"
#include "st_rip3_ports.h"
#include "st_rip3_styles.h"
#include "st_rip3_table_host.h"

#define CHECK(expression)                                                                        \
	do {                                                                                     \
		if (!(expression)) {                                                             \
			fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
			return 1;                                                                \
		}                                                                                \
	} while (0)

static int
run(const struct st_rip3_table_host *host, const char *text, int expected, const char *reply)
{
	struct st_rip3_builtin_reference reference;
	char                             output[100], before[100];
	int                              status;

	memset(output, 'Q', sizeof(output));
	memcpy(before, output, sizeof(output));
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)text, strlen(text)) == 0);
	status = st_rip3_table_host_execute(host, &reference, output, sizeof(output));
	if (status != expected)
		fprintf(stderr, "%s: status %d, expected %d\n", text, status, expected);
	CHECK(status == expected);
	if (status == 0)
		CHECK(strcmp(output, reply) == 0);
	else
		CHECK(memcmp(output, before, sizeof(output)) == 0);
	return 0;
}

static int
palette_objects(void)
{
	struct st_rip3_table_host        host     = {0};
	struct st_rip3_palettes         *palettes = NULL;
	struct st_rip3_backup           *backup   = NULL;
	struct st_rip3_builtin_reference reference;
	struct st_rip3_palette_info      info;
	struct st_rip3_palette           palette;
	char                             command[80], output[10];

	CHECK(st_rip3_palettes_create(&palettes) == 0);
	CHECK(st_rip3_palettes_backup_create(&backup) == 0);
	host.tables[ST_RIP3_TABLE_PAL] = (struct st_rip3_table_binding) {palettes, backup};
	CHECK(run(&host, "$CUR(PAL)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(PAL,CUR)$", 0, "0") == 0);
	CHECK(run(&host, "$INUSE(PAL,ANY)$", 0, "0") == 0);
	CHECK(run(&host, "$INUSE(PAL,ALL)$", 0, "0") == 0);
	CHECK(run(&host, "$INUSE(PAL,BACKUP)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(PAL,ANY)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(PAL,ALL)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(PAL,4)$", 0, "-1") == 0);
	{
		const char *const query = "$ISPROT(PAL,4)$";
		CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)query, strlen(query)) == 0);
		memset(output, 'Q', sizeof(output));
		CHECK(st_rip3_table_host_execute(&host, &reference, output, 2) == IO_ERROR_OUT_OF_RANGE);
		for (size_t i = 0; i < sizeof(output); i++)
			CHECK(output[i] == 'Q');
		CHECK(st_rip3_palettes_info(palettes, 4, &info) == 0 && !info.defined);
	}
	CHECK(run(&host, "$PROT(PAL,4,ALL,ALLSLOTS,CUR)$", 0, "") == 0);
	CHECK(run(&host, "$ISPROT(PAL,ANY)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(PAL,ANYSLOTS)$", 0, "0") == 0);
	CHECK(run(&host, "$INUSE(PAL,4)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(PAL,S0)$", 0, "-1") == 0);
	CHECK(run(&host, "$CUR(PAL,04)$", 0, "") == 0);
	CHECK(run(&host, "$CUR(PAL)$", 0, "4") == 0);
	CHECK(run(&host, "$INUSE(PAL,4)$", 0, "1") == 0);
	CHECK(run(&host, "$INUSE(PAL,ANY)$", 0, "1") == 0);
	CHECK(st_rip3_palettes_get(palettes, 4, &palette) == 0 && palette.rgb[15] == 0xffffff);
	CHECK(run(&host, "$PROT(PAL,CUR)$", 0, "") == 0);
	CHECK(run(&host, "$ISPROT(PAL,CUR)$", 0, "1") == 0);
	CHECK(run(&host, "$ISPROT(PAL,ANY)$", 0, "1") == 0);
	CHECK(run(&host, "$ISPROT(PAL,ALL)$", 0, "0") == 0);
	CHECK(run(&host, "$unprot(pal,4)$", 0, "") == 0);
	CHECK(run(&host, "$ISPROT(PAL,4)$", 0, "0") == 0);
	CHECK(run(&host, "$INUSE(PAL,BASE)$", 0, "0") == 0);
	CHECK(run(&host, "$INUSE(PAL,STACK)$", 0, "0") == 0);
	CHECK(run(&host, "$INUSE(PAL,ANYSLOTS)$", 0, "0") == 0);
	CHECK(run(&host, "$SCP$", 0, "") == 0);
	CHECK(run(&host, "$SCP(PUSH)$", 0, "") == 0);
	CHECK(run(&host, "$INUSE(PAL,BASE)$", 0, "1") == 0);
	CHECK(run(&host, "$INUSE(PAL,BACKUP)$", 0, "1") == 0);
	CHECK(run(&host, "$INUSE(PAL,STACK)$", 0, "1") == 0);
	CHECK(run(&host, "$RCP(POP)$", 0, "") == 0);
	CHECK(run(&host, "$INUSE(PAL,STACK)$", 0, "0") == 0);
	CHECK(run(&host, "$SCP(0)$", 0, "") == 0);
	CHECK(run(&host, "$INUSE(PAL,S0)$", 0, "1") == 0);
	CHECK(run(&host, "$INUSE(PAL,ANYSLOTS)$", 0, "1") == 0);
	CHECK(run(&host, "$INUSE(PAL,ALLSLOTS)$", 0, "0") == 0);
	CHECK(run(&host, "$PROT(PAL,4,S0,BAD)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(run(&host, "$ISPROT(PAL,4)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(PAL,S0)$", 0, "0") == 0);
	CHECK(run(&host, "$PROT(PAL,4,STACK)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(run(&host, "$PROT(PAL,4,ANY)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$PROT(PAL,4)$", 13) == 0);
	CHECK(st_rip3_table_host_execute(&host, &reference, output, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(run(&host, "$ISPROT(PAL,4)$", 0, "0") == 0);
	host.tables[ST_RIP3_TABLE_PAL].backup = NULL;
	CHECK(run(&host, "$PROT(PAL,4,S0)$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	CHECK(st_rip3_palettes_info(palettes, 4, &info) == 0 && !info.protected);
	host.tables[ST_RIP3_TABLE_PAL].backup = backup;
	host.tables[ST_RIP3_TABLE_PAL].live   = NULL;
	CHECK(run(&host, "$PROT(PAL,S0,4)$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	CHECK(run(&host, "$ISPROT(PAL,S0)$", 0, "0") == 0);
	host.tables[ST_RIP3_TABLE_PAL].live = palettes;
	CHECK(run(&host, "$PROT(PAL,S0,S9)$", 0, "") == 0);
	CHECK(run(&host, "$ISPROT(PAL,S0)$", 0, "1") == 0);
	CHECK(run(&host, "$ISPROT(PAL,S9)$", 0, "-1") == 0);
	CHECK(run(&host, "$RCP(0)$", 0, "") == 0);
	CHECK(run(&host, "$INUSE(PAL,S0)$", 0, "1") == 0); /* Protected restore retains slot. */
	CHECK(run(&host, "$UNPROT(PAL,ALLSLOTS)$", 0, "") == 0);
	for (unsigned i = 1; i < 10; i++) {
		(void)snprintf(command, sizeof(command), "$SCP(%u)$", i);
		CHECK(run(&host, command, 0, "") == 0);
	}
	CHECK(run(&host, "$INUSE(PAL,ALLSLOTS)$", 0, "1") == 0);
	CHECK(run(&host, "$PROT(PAL,ALLSLOTS)$", 0, "") == 0);
	for (unsigned i = 0; i < 10; i++) {
		(void)snprintf(command, sizeof(command), "$ISPROT(PAL,S%u)$", i);
		CHECK(run(&host, command, 0, "1") == 0);
	}
	for (unsigned i = 1; i < 36; i++) {
		(void)snprintf(command, sizeof(command), "$CUR(PAL,%u)$", i);
		CHECK(run(&host, command, 0, "") == 0);
	}
	CHECK(run(&host, "$INUSE(PAL,ALL)$", 0, "1") == 0);
	CHECK(run(&host, "$PROT(PAL,ALL)$", 0, "") == 0);
	CHECK(run(&host, "$ISPROT(PAL,ALL)$", 0, "1") == 0);
	CHECK(run(&host, "$ISPROT(PAL,ALLSLOTS)$", 0, "1") == 0);
	CHECK(st_rip3_palettes_info(palettes, 0, &info) == 0 && !info.protected);
	for (unsigned i = 1; i < 36; i++)
		CHECK(st_rip3_palettes_info(palettes, i, &info) == 0 && info.protected);
	CHECK(run(&host, "$UNPROT(PAL,CUR,ALLSLOTS)$", 0, "") == 0);
	CHECK(run(&host, "$ISPROT(PAL,35)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(PAL,1)$", 0, "1") == 0);
	CHECK(run(&host, "$ISPROT(PAL,ALL)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(PAL,ANYSLOTS)$", 0, "0") == 0);
	CHECK(run(&host, "$INUSE(PAL,BACKUP)$", 0, "1") == 0);
	CHECK(run(&host, "$INUSE(TV,NEXT_IMG)$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	CHECK(run(&host, "$INUSE(PAL,CUR)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(run(&host, "$ISPROT(PAL,0)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(run(&host, "$CUR(PAL,36)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(run(&host, "$CUR(MOUSE)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(run(&host, "$PROT(SCREEN,ALL)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	st_rip3_backup_destroy(backup);
	st_rip3_palettes_destroy(palettes);
	return 0;
}

static void
reset_failure(void *context, int status)
{
	unsigned *failures = context;
	if (status == IO_ERROR_PERMISSION_DENIED)
		(*failures)++;
}

static int
style_reset(void)
{
	unsigned                  failures = 0;
	struct st_rip3_table_host host     = {.operation_context = &failures, .operation_failed = reset_failure};
	struct st_rip3_styles    *styles   = NULL;
	struct st_rip3_backup    *backup   = NULL;
	struct st_rip3_style      style, current;
	struct st_rip3_style_info info;

	CHECK(st_rip3_style_defaults(&style) == 0 && st_rip3_styles_create(&styles, &style) == 0);
	CHECK(st_rip3_styles_backup_create(&backup) == 0);
	host.tables[ST_RIP3_TABLE_STYLE] = (struct st_rip3_table_binding) {styles, backup};
	style.drawing_color              = 4;
	CHECK(st_rip3_styles_define(styles, 5, &style) == 0);
	style.drawing_color = 2;
	CHECK(st_rip3_styles_define(styles, 7, &style) == 0);
	CHECK(run(&host, "$PROT(STYLE,7)$", 0, "") == 0);
	CHECK(run(&host, "$CUR(STYLE,5)$", 0, "") == 0);
	CHECK(run(&host, "$RESET(STYLE,7,5)$", 0, "") == 0 && failures == 1);
	CHECK(run(&host, "$CUR(STYLE)$", 0, "5") == 0);
	CHECK(run(&host, "$INUSE(STYLE,5)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(STYLE,5)$", 0, "-1") == 0);
	CHECK(run(&host, "$ISPROT(STYLE,CUR)$", 0, "0") == 0);
	CHECK(st_rip3_styles_get(styles, 5, &current) == 0 && current.drawing_color == 15);
	CHECK(st_rip3_styles_get(styles, 7, &current) == 0 && current.drawing_color == 2);
	CHECK(st_rip3_styles_set(styles, 5, &style) == 0);
	CHECK(run(&host, "$INUSE(STYLE,5)$", 0, "0") == 0);
	CHECK(run(&host, "$RESET(STYLE)$", 0, "") == 0);
	CHECK(run(&host, "$INUSE(STYLE,5)$", 0, "1") == 0);
	CHECK(st_rip3_styles_get(styles, 5, &current) == 0 && current.drawing_color == 15);

	CHECK(run(&host, "$SGS$", 0, "") == 0);
	CHECK(run(&host, "$SGS(3)$", 0, "") == 0);
	CHECK(run(&host, "$SGS(4)$", 0, "") == 0);
	CHECK(run(&host, "$SGS(PUSH)$", 0, "") == 0);
	CHECK(run(&host, "$PROT(STYLE,S3)$", 0, "") == 0);
	CHECK(run(&host, "$RESET(STYLE,ALL)$", 0, "") == 0 && failures == 2);
	CHECK(run(&host, "$CUR(STYLE)$", 0, "5") == 0);
	CHECK(st_rip3_styles_info(styles, 0, &info) == 0 && info.defined && !info.protected);
	CHECK(run(&host, "$INUSE(STYLE,5)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(STYLE,7)$", 0, "1") == 0);
	CHECK(run(&host, "$INUSE(STYLE,S3)$", 0, "1") == 0);
	CHECK(run(&host, "$INUSE(STYLE,S4)$", 0, "0") == 0);
	CHECK(run(&host, "$INUSE(STYLE,BASE)$", 0, "0") == 0);
	CHECK(run(&host, "$INUSE(STYLE,STACK)$", 0, "0") == 0);

	CHECK(run(&host, "$CUR(STYLE,5)$", 0, "") == 0);
	CHECK(st_rip3_styles_set(styles, 5, &style) == 0);
	CHECK(run(&host, "$RESET(STYLE, 7, cur )$", 0, "") == 0 && failures == 3);
	CHECK(run(&host, "$INUSE(STYLE,5)$", 0, "1") == 0);
	CHECK(st_rip3_styles_get(styles, 5, &current) == 0 && current.drawing_color == 15);
	CHECK(run(&host, "$CUR(STYLE,7)$", 0, "") == 0);
	CHECK(run(&host, "$RESET(STYLE,TBL,CUR)$", 0, "") == 0 && failures == 5);
	CHECK(run(&host, "$CUR(STYLE)$", 0, "7") == 0);
	CHECK(run(&host, "$ISPROT(STYLE,7)$", 0, "1") == 0);
	CHECK(st_rip3_styles_get(styles, 7, &current) == 0 && current.drawing_color == 2);
	CHECK(run(&host, "$INUSE(STYLE,5)$", 0, "0") == 0);
	CHECK(st_rip3_styles_info(styles, 0, &info) == 0 && info.defined && !info.protected);

	st_rip3_backup_destroy(backup);
	st_rip3_styles_destroy(styles);
	return 0;
}

static int
port_objects(enum st_rip3_canvas canvas, int32_t width, int32_t height)
{
	struct st_rip3_table_host host  = {0};
	struct st_rip3_ports     *ports = NULL;
	struct st_rip3_port_info  info;

	CHECK(st_rip3_ports_create(&ports, canvas) == 0);
	host.tables[ST_RIP3_TABLE_PORT].live = ports;
	CHECK(run(&host, "$CUR(PORT)$", 0, "0") == 0);
	CHECK(run(&host, "$ISPROT(PORT,35)$", 0, "-1") == 0);
	CHECK(run(&host, "$CUR(PORT,35)$", 0, "") == 0);
	CHECK(run(&host, "$CUR(PORT)$", 0, "35") == 0);
	CHECK(st_rip3_ports_info(ports, 35, &info) == 0 && info.defined && !info.offscreen);
	CHECK(info.bounds.x0 == 0 && info.bounds.y0 == 0 && info.bounds.x1 == width - 1
	      && info.bounds.y1 == height - 1);
	CHECK(info.viewport_active && info.viewport.x1 == width - 1 && info.viewport.y1 == height - 1);
	CHECK(run(&host, "$INUSE(PORT,35)$", 0, "1") == 0);
	CHECK(run(&host, "$PROT(PORT,CUR)$", 0, "") == 0);
	CHECK(run(&host, "$ISPROT(PORT,CUR)$", 0, "1") == 0);
	CHECK(run(&host, "$CUR(PORT,0)$", 0, "") == 0);
	CHECK(run(&host, "$CUR(PORT,35)$", 0, "") == 0);
	CHECK(run(&host, "$UNPROT(PORT,CUR)$", 0, "") == 0);
	CHECK(run(&host, "$ISPROT(PORT,CUR)$", 0, "0") == 0);
	CHECK(run(&host, "$PROT(PORT,35,S0)$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	CHECK(run(&host, "$ISPROT(PORT,35)$", 0, "0") == 0);
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	CHECK(palette_objects() == 0);
	CHECK(style_reset() == 0);
	CHECK(port_objects(ST_RIP3_CANVAS_640_480, 640, 480) == 0);
	CHECK(port_objects(ST_RIP3_CANVAS_800_600, 800, 600) == 0);
	CHECK(port_objects(ST_RIP3_CANVAS_1024_768, 1024, 768) == 0);
	return 0;
}
