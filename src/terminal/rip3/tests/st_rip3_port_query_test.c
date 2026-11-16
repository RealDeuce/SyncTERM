/* Independent port-query replies, exclusive boundaries and parser recovery. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_variables.h"
#include "st_rip3_host_internal.h"
#include "st_rip3_ports.h"
#include "st_rip3_environments.h"
#include "st_pixels.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct observations {
	char text[64];
	size_t length, replies, diagnostics;
	enum st_rip3_host_diagnostic last;
};

static void
reply(void *context, const uint8_t *text, size_t length)
{
	struct observations *observed = context;

	observed->replies++;
	if (length >= sizeof(observed->text)) {
		observed->length = sizeof(observed->text);
		return;
	}
	memcpy(observed->text, text, length);
	observed->text[length] = 0;
	observed->length = length;
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct observations *observed = context;
	observed->diagnostics++; observed->last = kind;
}

static int
run(const struct st_rip3_host_variables *host, const char *command,
    const char *expected, int error)
{
	struct observations observed = {0};
	const struct st_rip3_host_sink sink = {&observed,reply,diagnostic};

	st_rip3_host_variables_process(host, &sink, (const uint8_t *)command, strlen(command));
	if (error >= 0) {
		CHECK(observed.replies == 0 && observed.diagnostics == 1);
		CHECK((int)observed.last == error);
	}
	else {
		CHECK(observed.diagnostics == 0 && observed.replies == (expected[0] != 0 ? 1u : 0u));
		CHECK(observed.length == strlen(expected) && strcmp(observed.text, expected) == 0);
	}
	return 0;
}

static int
canvas(enum st_rip3_canvas canvas, const char *width, const char *height,
    const char *world_width, const char *world_height,
    const char *default_x, const char *default_y,
    const char *changed_x, const char *changed_y, const char *changed_size)
{
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_environments *environments = NULL, *wrong_canvas = NULL;
	struct st_rip3_table_host tables = {0};
	struct st_rip3_frame frame;
	struct st_rip3_host_variables host = {.tables=&tables,.frame=&frame};
	struct st_rip3_port_info info;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	const struct st_rip3_rect screen = {11,23,30,42}, view = {3,5,11,16};
	const struct st_rip3_rect offscreen = {500,600,6,8}, offview = {1,2,5,7};
	const struct st_rip3_rect single = {50,60,50,60};
	static const char *const query[] = {"PORTW","PORTH","PORTX0","PORTY0","PORTX1","PORTY1"};
	static const char *const device[] = {"20","20","11","23","31","43"};
	static const char *const world[] = {"40","40","22","46","62","86"};
	static const char *const viewport[] = {"9","12","3","5","12","17"};
	static const char *const offport[] = {"7","9","0","0","7","9"};
	static const char *const offviewport[] = {"5","6","1","2","6","8"};
	char command[80];
	unsigned current;

	CHECK(st_rip3_frame_init(&frame, canvas) == 0);
	CHECK(st_rip3_frame_world(&frame, 2 * frame.device_width, 2 * frame.device_height) == 0);
	CHECK(st_rip3_ports_create(&ports, canvas) == 0);
	tables.tables[ST_RIP3_TABLE_PORT].live = ports;
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	CHECK(run(&host, "$PORTW(0,PORT,DEVICE)$", width, -1) == 0);
	CHECK(run(&host, "$PORTH(0,PORT,DEVICE)$", height, -1) == 0);
	CHECK(run(&host, "$PORTX1(0,PORT,DEVICE)$", width, -1) == 0);
	CHECK(run(&host, "$PORTY1(0,PORT,DEVICE)$", height, -1) == 0);
	CHECK(run(&host, "$PORTW$", world_width, -1) == 0);
	CHECK(run(&host, "$PORTH()$", world_height, -1) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &screen, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	CHECK(st_rip3_ports_viewport(ports, &view) == 0);
	CHECK(st_rip3_ports_plot(ports, 0, 0, 12, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_ports_define(ports, 2, &offscreen, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(st_rip3_ports_viewport(ports, &offview) == 0);
	CHECK(st_rip3_ports_define(ports, 3, &single, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0);
	for (unsigned i = 0; i < 6; i++) {
		snprintf(command, sizeof(command), "$%s(1,PORT,DEVICE)$", query[i]);
		CHECK(run(&host, command, device[i], -1) == 0);
		snprintf(command, sizeof(command), "$%s$", query[i]);
		CHECK(run(&host, command, world[i], -1) == 0);
		snprintf(command, sizeof(command), "$%s(cUr,pOrT,wOrLd)$", query[i]);
		CHECK(run(&host, command, world[i], -1) == 0);
		snprintf(command, sizeof(command), "$%s(1,VIEW,DEVICE)$", query[i]);
		CHECK(run(&host, command, viewport[i], -1) == 0);
		snprintf(command, sizeof(command), "$%s(2,PORT,DEVICE)$", query[i]);
		CHECK(run(&host, command, offport[i], -1) == 0);
		snprintf(command, sizeof(command), "$%s(2,VIEW,DEVICE)$", query[i]);
		CHECK(run(&host, command, offviewport[i], -1) == 0);
		snprintf(command, sizeof(command), "$%s(35,PORT,DEVICE)$", query[i]);
		CHECK(run(&host, command, i < 2 ? "0" : "-1", -1) == 0);
	}
	CHECK(run(&host, "$portw(1)$", "40", -1) == 0);
	CHECK(run(&host, "$porth(1,view)$", "24", -1) == 0);
	CHECK(run(&host, "$PORTW(3,VIEW,DEVICE)$", "1", -1) == 0);
	CHECK(run(&host, "$PORTH(3,VIEW,DEVICE)$", "1", -1) == 0);
	CHECK(run(&host, "$PORTX1(3,PORT,DEVICE)$", "51", -1) == 0);
	CHECK(run(&host, "$PORTY1(3,PORT,DEVICE)$", "61", -1) == 0);
	CHECK(run(&host, "$DVP(2)$", "", -1) == 0);
	for (unsigned i = 0; i < 6; i++) {
		snprintf(command, sizeof(command), "$%s(2,PORT,DEVICE)$", query[i]);
		CHECK(run(&host, command, i < 2 ? "0" : "-1", -1) == 0);
		snprintf(command, sizeof(command), "$%s(2,VIEW,WORLD)$", query[i]);
		CHECK(run(&host, command, i < 2 ? "0" : "-1", -1) == 0);
	}
	/* Syntax errors stay inside the public processor; a valid query follows each. */
	static const char *const invalid[] = {"$PORTW(36)$","$PORTW(-1)$","$PORTW(A)$",
	    "$PORTW(1,BAD)$","$PORTW(1,PORT,BAD)$","$PORTW(1,PORT,DEVICE,0)$",
	    "$PORTW(1,)$","$PORTW(,PORT,DEVICE)$","$PORTX0(1,DEVICE)$",
	    "$PORTW(1,PORT,\202)$","$PORTW(1,PORT,\303\251)$"};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK(run(&host, invalid[i], NULL, ST_RIP3_HOST_INVALID_REFERENCE) == 0);
		CHECK(run(&host, "$PORTW(CUR,PORT,DEVICE)$", "20", -1) == 0);
	}
	CHECK(run(&host, "$PORTW(1,DEVICE)$", NULL, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(run(&host, "$PORTH(1,WORLD)$", NULL, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	/* Device replies need no world binding. No binding means no invented world. */
	host.frame = NULL;
	CHECK(run(&host, "$PORTW(1,PORT,DEVICE)$", "20", -1) == 0);
	CHECK(run(&host, "$PORTW$", NULL, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	/* A bound current environment supplies live world dimensions, ahead of the
	 * older optional frame snapshot. Changing WORLD must affect the next query. */
	CHECK(st_rip3_environments_create(&environments, canvas) == 0);
	tables.tables[ST_RIP3_TABLE_ENV].live = environments;
	host.frame = &frame;
	CHECK(run(&host, "$PORTW(0)$", "640", -1) == 0);
	CHECK(run(&host, "$PORTH(0)$", "350", -1) == 0);
	CHECK(run(&host, "$PORTX0(1)$", default_x, -1) == 0);
	CHECK(run(&host, "$PORTY0(1)$", default_y, -1) == 0);
	CHECK(run(&host, "$WORLD(CUR,1280,960)$", "", -1) == 0);
	CHECK(run(&host, "$PORTW(0)$", "1280", -1) == 0);
	CHECK(run(&host, "$PORTH(0)$", "960", -1) == 0);
	CHECK(run(&host, "$PORTX0(1)$", changed_x, -1) == 0);
	CHECK(run(&host, "$PORTY0(1)$", changed_y, -1) == 0);
	CHECK(run(&host, "$PORTW(1)$", changed_size, -1) == 0);
	CHECK(run(&host, "$PORTH(1)$", changed_size, -1) == 0);
	struct st_rip3_builtin_reference reference;
	char result[8] = "KEEP";
	CHECK(st_rip3_builtin_reference_parse(&reference, (const uint8_t *)"$PORTW(0)$", 10) == 0);
	CHECK(st_rip3_host_variables_execute(&host, &reference, result, 4) == IO_ERROR_OUT_OF_RANGE);
	CHECK(strcmp(result, "KEEP") == 0);
	CHECK(st_rip3_host_variables_execute(&host, &reference, result, 5) == 0 && strcmp(result, "1280") == 0);
	CHECK(st_rip3_host_variables_execute(&host, &reference, result, 0) == IO_ERROR_OUT_OF_RANGE);
	CHECK(strcmp(result, "1280") == 0);
	CHECK(st_rip3_environments_create(&wrong_canvas, canvas == ST_RIP3_CANVAS_640_480
	    ? ST_RIP3_CANVAS_800_600 : ST_RIP3_CANVAS_640_480) == 0);
	tables.tables[ST_RIP3_TABLE_ENV].live = wrong_canvas;
	CHECK(run(&host, "$PORTW$", NULL, ST_RIP3_HOST_OPERATION_FAILED) == 0);
	CHECK(run(&host, "$PORTW(1,PORT,DEVICE)$", "20", -1) == 0);
	tables.tables[ST_RIP3_TABLE_ENV].live = environments;
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 1);
	CHECK(st_rip3_ports_info(ports, 1, &info) == 0 && info.protected
	    && info.viewport_active && info.viewport.x0 == 3 && info.viewport.y0 == 5
	    && info.viewport.x1 == 11 && info.viewport.y1 == 16);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < frame.device_height; y++) {
		for (uint32_t x = 0; x < frame.device_width; x++)
			CHECK(capture->primary[(size_t)y * frame.device_width + x]
			    == (ST_PIXEL_COLOR_RGB_TAG | (x == 14 && y == 28 ? 0xff5555 : 0x0000aa)));
	}
	st_pixel_buffer_destroy(capture);
	st_rip3_environments_destroy(wrong_canvas);
	st_rip3_environments_destroy(environments);
	st_rip3_ports_destroy(ports);
	return 0;
}

int
main(void)
{
	const struct st_rip3_host_variables absent = {0};
	CHECK(run(&absent, "$PORTW$", NULL, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(run(&absent, "$PORTX1(CUR,VIEW,DEVICE)$", NULL, ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
	CHECK(run(&absent, "$PORTW(36)$", NULL, ST_RIP3_HOST_INVALID_REFERENCE) == 0);
	CHECK(canvas(ST_RIP3_CANVAS_640_480, "640","480","1280","960",
	    "11","16","22","46","40") == 0);
	CHECK(canvas(ST_RIP3_CANVAS_800_600, "800","600","1600","1200",
	    "8","13","17","36","32") == 0);
	CHECK(canvas(ST_RIP3_CANVAS_1024_768, "1024","768","2048","1536",
	    "6","10","13","28","25") == 0);
	return 0;
}
