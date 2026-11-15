/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_features.h"
#include "st_rip3_geometry.h"
#include "st_rip3_palettes.h"
#include "st_rip3_table_host.h"
#include "st_rip3_backup.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
run(const struct st_rip3_host_variables *host, const char *command,
    int expected, const char *reply)
{
	struct st_rip3_builtin_reference reference;
	char output[100], before[100];

	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)command, strlen(command)) == 0);
	memset(output, 'Q', sizeof(output)); memcpy(before, output, sizeof(output));
	CHECK(st_rip3_host_variables_execute(host, &reference, output, sizeof(output)) == expected);
	if (expected == 0)
		CHECK(strcmp(output, reply) == 0);
	else
		CHECK(memcmp(output, before, sizeof(output)) == 0);
	return 0;
}

int
main(void)
{
	struct st_rip3_host_variables host = {0};
	struct st_rip3_features features = {0};
	struct st_rip3_frame frame;
	struct st_rip3_table_host tables = {0};
	struct st_rip3_palettes *palettes = NULL;
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_builtin_reference reference;
	char output[20], before[20];
	const struct {enum st_rip3_canvas canvas; const char *x, *y;} canvases[] = {
		{ST_RIP3_CANVAS_640_480,"640","480"},
		{ST_RIP3_CANVAS_800_600,"800","600"},
		{ST_RIP3_CANVAS_1024_768,"1024","768"},
	};

	CHECK(run(&host, "$NULL$", 0, "") == 0);
	CHECK(run(&host, "$NULL(1)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(run(&host, "$IFS$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(run(&host, "$IFS(JPEG)$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	CHECK(run(&host, "$RESX$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	CHECK(run(&host, "$CUR(PAL)$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	host.features = &features;
	CHECK(run(&host, "$IFS(JPEG)$", 0, "0") == 0);
	CHECK(run(&host, "$IFS(EXTAPPS)$", 0, "0") == 0);
	CHECK(run(&host, "$IFS(LIST)$", 0, "") == 0);
	CHECK(st_rip3_features_set(&features, ST_RIP3_FEATURE_BMP, true) == 0);
	CHECK(st_rip3_features_set(&features, ST_RIP3_FEATURE_JPEG, true) == 0);
	CHECK(run(&host, "$iFs(jPeG)$", 0, "1") == 0);
	CHECK(run(&host, "$IFS(LIST, _IMAGE)$", 0, "BMP,JPEG") == 0);
	CHECK(run(&host, "$IFS(LIST,ALL)$", 0, "BMP,JPEG") == 0);
	CHECK(run(&host, "$IFS(LIST)$", 0, "_IMAGE") == 0);
	CHECK(run(&host, "$IFS(JPEG,ALL)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(run(&host, "$IFS(LIST,ALL,EXTRA)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(run(&host, "$IFS(,ALL)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(run(&host, "$APP(program)$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	CHECK(run(&host, "$GOTOURL(url)$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	CHECK(run(&host, "$RIPVER$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	host.frame = &frame;
	for (size_t i = 0; i < 3; i++) {
		CHECK(st_rip3_frame_init(&frame, canvases[i].canvas) == 0);
		CHECK(run(&host, "$RESX$", 0, canvases[i].x) == 0);
		CHECK(run(&host, "$RESY()$", 0, canvases[i].y) == 0);
		CHECK(st_rip3_frame_world(&frame, 320, 200) == 0);
		CHECK(run(&host, "$RESX$", 0, canvases[i].x) == 0);
		CHECK(run(&host, "$RESY$", 0, canvases[i].y) == 0);
	}
	CHECK(run(&host, "$RESX(1)$", IO_ERROR_INVALID_FORMAT, NULL) == 0);
	CHECK(st_rip3_palettes_create(&palettes) == 0);
	CHECK(st_rip3_palettes_backup_create(&backup) == 0);
	tables.tables[ST_RIP3_TABLE_PAL] = (struct st_rip3_table_binding){palettes,backup};
	host.tables = &tables;
	CHECK(run(&host, "$CUR(PAL,5)$", 0, "") == 0);
	CHECK(run(&host, "$PROT(PAL,CUR)$", 0, "") == 0);
	CHECK(run(&host, "$COPY(PAL,TBL,S0,BASE)$", 0, "") == 0);
	CHECK(run(&host, "$BACKSTAT(PAL)$", 0, "1:0:1:9:1:0:0:0:0:0:0:0:0:0") == 0);
	CHECK(run(&host, "$CUR(PAL,0)$", 0, "") == 0);
	CHECK(run(&host, "$RCP(0)$", 0, "") == 0);
	CHECK(run(&host, "$CUR(PAL)$", 0, "5") == 0);
	CHECK(run(&host, "$ISPROT(PAL,CUR)$", 0, "1") == 0);
	CHECK(run(&host, "$PALENTRY(CUR,15)$", 0, "8;255:255:255") == 0);
	CHECK(run(&host, "$UNRECOGNIZED$", IO_ERROR_NOT_FOUND, NULL) == 0);
	CHECK(run(&host, "$SHIFT$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	CHECK(run(&host, "$P$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	CHECK(run(&host, "$SAVE0$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	memset(output, 'Q', sizeof(output)); memcpy(before, output, sizeof(output));
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$CUR(PAL,7)$", 12) == 0);
	CHECK(st_rip3_host_variables_execute(&host, &reference, output, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(run(&host, "$CUR(PAL)$", 0, "5") == 0);
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$IFS(LIST)$", 11) == 0);
	CHECK(st_rip3_host_variables_execute(&host, &reference, output, 6) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_host_variables_execute(NULL, &reference, output, sizeof(output)) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_host_variables_execute(&host, NULL, output, sizeof(output)) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_host_variables_execute(&host, &reference, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(memcmp(output, before, sizeof(output)) == 0);
	st_rip3_backup_destroy(backup);
	st_rip3_palettes_destroy(palettes);
	return 0;
}
