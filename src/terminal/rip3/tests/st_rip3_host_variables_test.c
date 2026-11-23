/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_features.h"
#include "st_rip3_geometry.h"
#include "st_rip3_palettes.h"
#include "st_rip3_table_host.h"
#include "st_rip3_backup.h"
#include "st_rip3_tones.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct observation {
	unsigned errors, replies;
	enum st_rip3_host_diagnostic kind;
	size_t length;
	uint8_t bytes[8];
};

static void
receive(void *context, const uint8_t *bytes, size_t length)
{
	struct observation *seen=context;
	seen->replies++;seen->length=length;
	if (length<=sizeof(seen->bytes)) memcpy(seen->bytes,bytes,length);
}

static void
diagnose(void *context, enum st_rip3_host_diagnostic kind)
{
	struct observation *seen=context;
	seen->errors++;seen->kind=kind;
}

static int
recovery(const struct st_rip3_host_variables *host, struct st_rip3_palettes *palettes)
{
	struct observation seen={0};
	const struct st_rip3_host_sink sink={&seen,receive,diagnose};
	const char *const invalid[]={"$CUR(\"PAL\",7)$","$CUR(PAL,7\\,8)$","$CUR(PAL,7/8)$"};
	for (size_t i=0; i<sizeof(invalid)/sizeof(invalid[0]); i++) {
		unsigned current;
		st_rip3_host_variables_process(host,&sink,(const uint8_t *)invalid[i],strlen(invalid[i]));
		CHECK(seen.errors==i+1 && seen.replies==0 && seen.kind==ST_RIP3_HOST_INVALID_REFERENCE);
		CHECK(st_rip3_palettes_current(palettes,&current)==0 && current==5);
	}
	const char *valid="$CUR(P\\AL,7)$";
	st_rip3_host_variables_process(host,&sink,(const uint8_t *)valid,strlen(valid));
	CHECK(seen.errors==3 && seen.replies==0);
	st_rip3_host_variables_process(host,&sink,(const uint8_t *)"$CUR(PAL)$",10);
	CHECK(seen.errors==3 && seen.replies==1 && seen.length==1 && seen.bytes[0]=='7');
	return 0;
}

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

static void
request_tones(void *context, const struct st_rip3_tones *tones)
{
	struct st_rip3_tones *received = context;
	*received = *tones;
}

static int
tone_requests(void)
{
	struct st_rip3_tones received;
	struct st_rip3_host_variables host = {.tones_context = &received, .tones = request_tones};
	struct observation seen = {0};
	const struct st_rip3_host_sink sink = {&seen, receive, diagnose};
	const struct {
		const char *command;
		size_t count, notes_per_repeat;
		struct st_rip3_tone notes[6];
		int step;
	} cases[] = {
		{"$ALARM$", 6, 2, {{320, 200}, {160, 225}}, 0},
		{"$ALARM(0)$", 0, 0, {{0}}, 0},
		{"$BEEP$", 1, 1, {{1000, 75}}, 0},
		{"$BEEP(23)$", 1, 1, {{23, 75}}, 0},
		{"$BLIP$", 1, 1, {{1000, 25}}, 0},
		{"$BLIP(0,0)$", 1, 1, {{0, 0}}, 0},
		{"$MUSIC$", 24, 6, {{1125, 17}, {775, 14}, {625, 10}, {725, 14}, {825, 10}, {600, 25}}, 0},
		{"$MUSIC(65535)$", 393210, 6, {{1125, 17}, {775, 14}, {625, 10}, {725, 14}, {825, 10}, {600, 25}}, 0},
		{"$PHASER$", 123, 1, {{2500, 2}}, -20},
		{"$REVPHASER$", 123, 1, {{50, 2}}, 20},
		{"$PHASER(100,60,20,4)$", 3, 1, {{100, 4}}, -20},
		{"$REVPHASER(65530,65534,10)$", 32768, 1, {{65530, 2}}, 10},
		{"$PHASER(4,0,32768,1)$", 2, 1, {{4, 1}}, -32768},
		{"$T(65535,65535)$", 1, 1, {{65535, 65535}}, 0},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
		memset(&received, 0, sizeof(received));
		const char *command = cases[i].command;
		st_rip3_host_variables_process(&host, &sink, (const uint8_t *)command, strlen(command));
		CHECK(seen.errors == 0 && seen.replies == 0);
		struct st_rip3_tone note;
		for (size_t n = 0; n < cases[i].count; n++) {
			struct st_rip3_tone expected = cases[i].notes[n % cases[i].notes_per_repeat];
			expected.frequency = (uint16_t)((int32_t)expected.frequency + (int32_t)n * cases[i].step);
			CHECK(st_rip3_tones_next(&received, &note));
			CHECK(note.frequency == expected.frequency && note.milliseconds == expected.milliseconds);
		}
		CHECK(!st_rip3_tones_next(&received, &note));
	}
	const char *const invalid[] = {
		"$T$", "$T(1000)$", "$T(1,2,3)$", "$BLIP(-1,2)$", "$BEEP(2,x)$", "$ALARM(65536)$",
		"$MUSIC(0,1)$", "$PHASER(1,2)$", "$REVPHASER(1,1)$",
		"$PHASER(20,1,0,2)$", "$REVPHASER(0,20,1,0)$",
	};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		size_t length = strlen(invalid[i]);
		st_rip3_host_variables_process(&host, &sink, (const uint8_t *)invalid[i], length);
		CHECK(received.repeats == 0 && seen.errors == i + 1 && seen.replies == i + 1);
		CHECK(seen.kind == ST_RIP3_HOST_INVALID_REFERENCE && seen.length == length);
	}
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

	CHECK(tone_requests() == 0);
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
	CHECK(run(&host, "$IFS(,ALL)$", 0, "0") == 0);
	CHECK(run(&host, "$IFS(,LIST,,ALL,)$", 0, "BMP,JPEG") == 0);
	const struct st_rip3_terminal_identity identity={"Client","Vendor","1"};host.identity=&identity;
	CHECK(run(&host,"$TERMINFO(ABCDEFGHIJKLMNOPQRST)$",0,"NONE")==0);
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
	CHECK(run(&host, "$PALENTRY(CUR,15)$", 0, "6;63:63:63") == 0);
	CHECK(run(&host, "$UNRECOGNIZED$", IO_ERROR_NOT_FOUND, NULL) == 0);
	CHECK(run(&host, "$SHIFT$", IO_ERROR_NOT_SUPPORTED, NULL) == 0);
	CHECK(run(&host, "$P$", IO_ERROR_NOT_FOUND, NULL) == 0);
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
	CHECK(recovery(&host,palettes)==0);
	st_rip3_backup_destroy(backup);
	st_rip3_palettes_destroy(palettes);
	return 0;
}
