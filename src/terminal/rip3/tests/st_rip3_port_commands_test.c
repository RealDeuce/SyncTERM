/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_ports.h"
#include "st_rip3_stream.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_table_host.h"
#include "st_pixels.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct observation { unsigned errors; enum st_rip3_command_diagnostic kind; };

static void
diagnostic(void *context, enum st_rip3_command_diagnostic kind)
{
	struct observation *seen = context;
	seen->errors++;
	seen->kind = kind;
}

static int
process(const struct st_rip3_command_host *host, const char *command, int error)
{
	struct observation seen = {0};
	const struct st_rip3_command_sink sink = {&seen,diagnostic};
	st_rip3_commands_process(host, &sink, (const uint8_t *)command, strlen(command));
	if (seen.errors != (error < 0 ? 0u : 1u) || (error >= 0 && (int)seen.kind != error))
		fprintf(stderr, "command %s: %u diagnostics, kind %d, expected %d\n", command, seen.errors, (int)seen.kind, error);
	CHECK(seen.errors == (error < 0 ? 0u : 1u));
	if (error >= 0) CHECK((int)seen.kind == error);
	return 0;
}

static bool
rect_equal(const struct st_rip3_rect *a, const struct st_rip3_rect *b)
{
	return a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1;
}

static bool
info_equal(const struct st_rip3_port_info *a, const struct st_rip3_port_info *b)
{
	return a->defined == b->defined && a->offscreen == b->offscreen
	    && a->protected == b->protected && a->viewport_active == b->viewport_active
	    && rect_equal(&a->bounds, &b->bounds) && rect_equal(&a->viewport, &b->viewport)
	    && a->image.automatic == b->image.automatic && a->image.enabled == b->image.enabled
	    && a->image.flags == b->image.flags && rect_equal(&a->image.area, &b->image.area);
}

static int
unchanged(struct st_rip3_ports *ports, const struct st_rip3_port_info before[36], unsigned current)
{
	struct st_rip3_port_info after;
	unsigned number;
	CHECK(st_rip3_ports_current(ports, &number) == 0 && number == current);
	for (unsigned i = 0; i < 36; i++) {
		CHECK(st_rip3_ports_info(ports, i, &after) == 0);
		CHECK(info_equal(before+i, &after));
	}
	return 0;
}

struct answer { const char *expected; unsigned replies, errors; bool mismatch; };

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct answer *seen = context;
	seen->replies++;
	seen->mismatch = length != strlen(seen->expected)
	    || memcmp(bytes, seen->expected, length) != 0;
}

static void
query_error(void *context, enum st_rip3_host_diagnostic kind)
{
	struct answer *seen = context;
	(void)kind;
	seen->errors++;
}

static int
query(const struct st_rip3_command_host *bindings, const char *reference, const char *expected)
{
	struct st_rip3_table_host tables = {0};
	const struct st_rip3_host_variables host = {.tables=&tables};
	struct answer seen = {.expected=expected};
	const struct st_rip3_host_sink sink = {&seen,reply,query_error};
	tables.tables[ST_RIP3_TABLE_PORT].live = bindings->ports;
	tables.tables[ST_RIP3_TABLE_ENV].live = bindings->environments;
	st_rip3_host_variables_process(&host, &sink, (const uint8_t *)reference, strlen(reference));
	CHECK(seen.replies == 1 && seen.errors == 0 && !seen.mismatch);
	return 0;
}

struct stream_scene { const struct st_rip3_command_host *host; unsigned errors; };

static void
scene_error(void *context, enum st_rip3_command_diagnostic kind)
{
	struct stream_scene *scene = context;
	(void)kind;
	scene->errors++;
}

static void
scene_command(void *context, const uint8_t *bytes, size_t length)
{
	struct stream_scene *scene = context;
	const struct st_rip3_command_sink sink = {scene,scene_error};
	st_rip3_commands_process(scene->host, &sink, bytes, length);
}

static int
wire(struct st_rip3_stream *stream, struct stream_scene *scene, const char *bytes)
{
	unsigned before = scene->errors;
	for (size_t i = 0; i < strlen(bytes); i++)
		st_rip3_stream_process(stream, (const uint8_t *)bytes+i, 1);
	CHECK(scene->errors == before);
	return 0;
}

static int
scene(enum st_rip3_canvas canvas, uint32_t width, uint32_t height,
    struct st_rip3_rect blue, struct st_rip3_rect red, const char *view_width)
{
	struct st_rip3_command_host host = {0};
	struct stream_scene scene = {&host,0};
	const struct st_rip3_stream_sink sink = {.context=&scene,.command=scene_command};
	struct st_rip3_stream *stream = NULL;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_palette palette;
	struct st_rip3_port_info info;
	struct st_rip3_environment environment;
	const struct st_rip3_rect fill = {0,0,32767,32767};

	CHECK(st_rip3_environments_create(&host.environments, canvas) == 0);
	CHECK(st_rip3_ports_create(&host.ports, canvas) == 0);
	CHECK(st_rip3_stream_create(&stream, &sink) == 0);
	CHECK(wire(stream, &scene, "!|J10|n2000|fZKQO|2P14G3CDB9Z00020000\r\n!|v0G0C4F3B\r") == 0);
	CHECK(st_rip3_ports_fill(host.ports, &fill, 1, ST_RIP3_COPY) == 0);
	CHECK(query(&host, "$CUR(PORT)$", "1") == 0);
	CHECK(query(&host, "$PORTW(1,VIEW,DEVICE)$", view_width) == 0);
	CHECK(wire(stream, &scene, "!|2P20W0O1R1B2|v0W0O1R1B\r") == 0);
	CHECK(st_rip3_ports_fill(host.ports, &fill, 4, ST_RIP3_COPY) == 0);
	CHECK(wire(stream, &scene, "!|2p20|v00000000\r") == 0);
	CHECK(st_rip3_ports_plot(host.ports, 10,10, 2, ST_RIP3_COPY) == 0); /* Inactive. */
	CHECK(wire(stream, &scene, "!|v02000200\r") == 0); /* Nonzero DEVICE point; converted zeros deactivate. */
	CHECK(st_rip3_ports_info(host.ports, 0, &info) == 0 && info.viewport_active);
	CHECK(info.viewport.x0 == 1 && info.viewport.y0 == 0 && info.viewport.x1 == 1 && info.viewport.y1 == 0);
	CHECK(st_rip3_ports_fill(host.ports, &fill, 15, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(host.ports, &palette, &capture) == 0);
	/* One independently specified scene: white DEVICE point, blue clipped port,
	 * red retained pixels from a deleted port, otherwise black. */
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			uint32_t rgb = x == 1 && y == 0 ? 0xffffff
			    : x >= (uint32_t)red.x0 && x <= (uint32_t)red.x1
			    && y >= (uint32_t)red.y0 && y <= (uint32_t)red.y1 ? 0xaa0000
			    : x >= (uint32_t)blue.x0 && x <= (uint32_t)blue.x1
			    && y >= (uint32_t)blue.y0 && y <= (uint32_t)blue.y1 ? 0x0000aa : 0;
			CHECK(capture->primary[(size_t)y * width+x] == (ST_PIXEL_COLOR_RGB_TAG | rgb));
		}
	}
	CHECK(st_rip3_ports_info(host.ports, 2, &info) == 0 && !info.defined);
	CHECK(query(&host, "$CUR(PORT)$", "0") == 0);
	CHECK(query(&host, "$PORTW(0,VIEW,DEVICE)$", "1") == 0);
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0 && environment.port == 0);
	st_pixel_buffer_destroy(capture);
	st_rip3_stream_destroy(stream);
	st_rip3_ports_destroy(host.ports);
	st_rip3_environments_destroy(host.environments);
	return 0;
}

static int
validation(void)
{
	const char *invalid[] = {"2P", "2P1", "2P100000202", "2P0000002020",
	    "2P100000202000000000", "2P1000002020000000?", "2P1000100000",
	    "v", "v000000", "v000000000", "v0000000?",
	    "2p", "2p1", "2p10000", "2p1?00"};
	const char *unsupported[] = {"2P100000202G", "2P10000020200000001",
	    "2P1000002020 comment", "v00000202 comment", "2p1001", "2p10 comment"};
	struct st_rip3_command_host host = {0}, missing = {0};
	struct st_rip3_port_info before[36];
	struct st_rip3_environment environment;
	const struct st_rip3_rect bounds = {0,0,2,2};

	CHECK(st_rip3_environments_create(&host.environments, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_create(&host.ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_define(host.ports, 1, &bounds, false) == 0);
	CHECK(st_rip3_ports_switch(host.ports, 1) == 0);
	for (unsigned i = 0; i < 36; i++) CHECK(st_rip3_ports_info(host.ports, i, before+i) == 0);
	for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
		CHECK(process(&host, invalid[i], ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(unchanged(host.ports, before, 1) == 0);
	}
	for (size_t i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++) {
		CHECK(process(&host, unsupported[i], ST_RIP3_COMMAND_UNSUPPORTED) == 0);
		CHECK(unchanged(host.ports, before, 1) == 0);
	}
	CHECK(process(&host, "2P1ZYZYZZZZ0", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(unchanged(host.ports, before, 1) == 0);
	CHECK(process(&missing, "2p10", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.ports = host.ports;
	CHECK(process(&missing, "v00000202", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	missing.ports = NULL; missing.environments = host.environments;
	CHECK(process(&missing, "2P1000002020", ST_RIP3_COMMAND_UNSUPPORTED) == 0);
	CHECK(process(&host, "v0000000", -1) == 0); /* Shortened zero sentinel. */
	CHECK(process(&host, "v00000202", -1) == 0);
	for (unsigned i = 0; i < 36; i++) CHECK(st_rip3_ports_info(host.ports, i, before+i) == 0);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(process(&host, "2pa0", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "2p0&", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(unchanged(host.ports, before, 1) == 0);
	CHECK(process(&host, "J10", -1) == 0);
	/* Protection of ENV does not prohibit configuring the bound PORT table. */
	CHECK(st_rip3_environments_get(host.environments, 0, &environment) == 0);
	environment.port = 7;
	CHECK(st_rip3_environments_define(host.environments, 1, &environment) == 0);
	CHECK(st_rip3_environments_select(host.environments, 1) == 0);
	CHECK(st_rip3_environments_protect(host.environments, 1, true) == 0);
	CHECK(process(&host, "v00000202", -1) == 0);
	CHECK(st_rip3_environments_get(host.environments, 1, &environment) == 0 && environment.port == 7);
	CHECK(process(&host, "2p1000", -1) == 0);
	CHECK(query(&host, "$CUR(PORT)$", "0") == 0);
	st_rip3_ports_destroy(host.ports);
	st_rip3_environments_destroy(host.environments);
	return 0;
}

static int
flags_and_delete(void)
{
	static const char *flags[] = {"0","2","4","6","8","A","C","E"};
	struct st_rip3_command_host host = {0};
	struct st_rip3_port_info info, before;
	unsigned current = 0, observed;
	char command[32];

	CHECK(st_rip3_environments_create(&host.environments, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_create(&host.ports, ST_RIP3_CANVAS_640_480) == 0);
	for (unsigned i = 0; i < 8; i++) {
		unsigned bits = i*2;
		(void)snprintf(command, sizeof(command), "2P%u00000202%s", i+1, flags[i]);
		CHECK(process(&host, command, -1) == 0);
		if (bits & 2) current = i+1;
		CHECK(st_rip3_ports_current(host.ports, &observed) == 0 && observed == current);
		CHECK(st_rip3_ports_info(host.ports, i+1, &info) == 0);
		CHECK(info.defined && !info.offscreen && info.protected == ((bits & 8) != 0)
		    && info.viewport_active == ((bits & 4) == 0));
	}
	CHECK(st_rip3_ports_info(host.ports, 8, &before) == 0);
	CHECK(process(&host, "v00000000", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(process(&host, "2p80", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(query(&host, "$CUR(PORT)$", "8") == 0);
	CHECK(process(&host, "2P8000002022", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(query(&host, "$CUR(PORT)$", "0") == 0); /* Documented failed-redefine fallback. */
	CHECK(st_rip3_ports_info(host.ports, 8, &info) == 0 && info_equal(&before, &info));
	CHECK(process(&host, "2p080", -1) == 0); /* Delete unprotected, select protected survivor. */
	CHECK(query(&host, "$CUR(PORT)$", "8") == 0);
	CHECK(process(&host, "2p01", -1) == 0); /* Deleted destination falls back to zero. */
	CHECK(query(&host, "$CUR(PORT)$", "0") == 0);
	CHECK(process(&host, "2p18", -1) == 0); /* Already undefined source, still select destination. */
	CHECK(query(&host, "$CUR(PORT)$", "8") == 0);
	CHECK(st_rip3_ports_protect(host.ports, 8, false) == 0);
	CHECK(process(&host, "2p80", -1) == 0);
	CHECK(st_rip3_ports_info(host.ports, 8, &info) == 0 && !info.defined);
	CHECK(process(&host, "2p00", -1) == 0);
	CHECK(query(&host, "$CUR(PORT)$", "0") == 0);
	st_rip3_ports_destroy(host.ports);
	st_rip3_environments_destroy(host.environments);
	return 0;
}

static int
field_widths(void)
{
	const char *sizes[] = {"n2","n3","n4","n5"};
	const char *definitions[] = {"2P10000020200020", "2P10000000020022", "2P100000000000200022", "2P1000000000000002000022"};
	const char *views[] = {"v0101022", "v0010010022", "v0001000100022", "v0000100001000022"};
	struct st_rip3_command_host host = {0};
	struct st_rip3_port_info info;

	CHECK(st_rip3_environments_create(&host.environments, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(st_rip3_ports_create(&host.ports, ST_RIP3_CANVAS_640_480) == 0);
	CHECK(process(&host, "fHSDC", -1) == 0); /* World and device both 640x480. */
	for (size_t i = 0; i < 4; i++) {
		CHECK(process(&host, sizes[i], -1) == 0);
		CHECK(process(&host, definitions[i], -1) == 0);
		CHECK(st_rip3_ports_info(host.ports, 1, &info) == 0
		    && info.viewport.x0 == 0 && info.viewport.y0 == 0
		    && info.viewport.x1 == 1 && info.viewport.y1 == 1);
		CHECK(process(&host, views[i], -1) == 0);
		CHECK(st_rip3_ports_info(host.ports, 1, &info) == 0);
		CHECK(info.bounds.x0 == 0 && info.bounds.y0 == 0 && info.bounds.x1 == 1 && info.bounds.y1 == 1);
		CHECK(info.viewport.x0 == 1 && info.viewport.y0 == 1 && info.viewport.x1 == 2 && info.viewport.y1 == 2);
	}
	CHECK(process(&host, "n2", -1) == 0);
	CHECK(process(&host, "2p10", -1) == 0);
	CHECK(process(&host, "v00002E1M", -1) == 0);
	CHECK(st_rip3_ports_info(host.ports, 0, &info) == 0
	    && info.viewport.x1 == 86 && info.viewport.y1 == 58);
	CHECK(process(&host, "2P10000ZZZZ0", -1) == 0);
	CHECK(st_rip3_ports_info(host.ports, 1, &info) == 0
	    && info.bounds.x1 == 639 && info.bounds.y1 == 479);
	CHECK(process(&host, "2P1----02022", -1) == 0); /* MegaNum dash-as-zero. */
	CHECK(process(&host, "2p10", -1) == 0);
	CHECK(process(&host, "J1S", -1) == 0);
	CHECK(process(&host, "v00000z0&", -1) == 0);
	CHECK(st_rip3_ports_info(host.ports, 0, &info) == 0
	    && info.viewport.x1 == 61 && info.viewport.y1 == 63);
	CHECK(process(&host, "2PZ00000z0&2", -1) == 0);
	CHECK(st_rip3_ports_info(host.ports, 35, &info) == 0
	    && info.bounds.x1 == 60 && info.bounds.y1 == 62);
	CHECK(process(&host, "2P#00000z0&2", ST_RIP3_COMMAND_INVALID) == 0);
	CHECK(process(&host, "n5", -1) == 0);
	CHECK(process(&host, "f0000100001", -1) == 0);
	CHECK(process(&host, "v0000000000&&&&&&&&&&", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	CHECK(st_rip3_ports_info(host.ports, 35, &info) == 0
	    && info.viewport.x0 == 0 && info.viewport.y0 == 0
	    && info.viewport.x1 == 60 && info.viewport.y1 == 62);
	CHECK(process(&host, "2P30000000000&&&&&&&&&&0", -1) == 0);
	CHECK(st_rip3_ports_info(host.ports, 3, &info) == 0
	    && info.bounds.x0 == 0 && info.bounds.y0 == 0
	    && info.bounds.x1 == 639 && info.bounds.y1 == 479);
	CHECK(process(&host, "v&&&&&&&&&&&&&&&&&&&&", ST_RIP3_COMMAND_OPERATION_FAILED) == 0);
	/* This device-level fixture keeps v independent of the wire 2P producer. */
	{
		const struct st_rip3_rect offscreen = {0,0,3,3};
		uint8_t index = 0;
		CHECK(process(&host, "J10", -1) == 0);
		CHECK(process(&host, "n2", -1) == 0);
		CHECK(process(&host, "fHSDC", -1) == 0);
		CHECK(st_rip3_ports_define(host.ports, 2, &offscreen, true) == 0);
		CHECK(st_rip3_ports_switch(host.ports, 2) == 0);
		CHECK(process(&host, "v01010202", -1) == 0);
		CHECK(st_rip3_ports_plot(host.ports, 0,0, 15, ST_RIP3_COPY) == 0);
		CHECK(st_rip3_ports_read(host.ports, 2, 1,1, &index) == 0 && index == 15);
		CHECK(process(&host, "v00000000", -1) == 0);
		CHECK(st_rip3_ports_plot(host.ports, 0,0, 2, ST_RIP3_COPY) == 0);
		CHECK(st_rip3_ports_read(host.ports, 2, 1,1, &index) == 0 && index == 15);
		CHECK(process(&host, "J1S", -1) == 0);
	}
	CHECK(process(&host, "2pZ0", -1) == 0);
	CHECK(st_rip3_ports_info(host.ports, 35, &info) == 0 && !info.defined);
	{
		struct st_rip3_command_host wrong = host;
		struct st_rip3_port_info before;
		CHECK(st_rip3_ports_create(&wrong.ports, ST_RIP3_CANVAS_800_600) == 0);
		CHECK(st_rip3_ports_info(wrong.ports, 0, &before) == 0);
		CHECK(process(&host, "n2", -1) == 0);
		CHECK(process(&wrong, "v00000001", ST_RIP3_COMMAND_INVALID) == 0);
		CHECK(st_rip3_ports_info(wrong.ports, 0, &info) == 0 && info_equal(&before, &info));
		st_rip3_ports_destroy(wrong.ports);
	}
	st_rip3_ports_destroy(host.ports);
	st_rip3_environments_destroy(host.environments);
	return 0;
}

int
main(void)
{
	CHECK(scene(ST_RIP3_CANVAS_640_480, 640,480, (struct st_rip3_rect){8,6,79,59},
	    (struct st_rip3_rect){16,12,31,23}, "72") == 0);
	CHECK(scene(ST_RIP3_CANVAS_800_600, 800,600, (struct st_rip3_rect){10,7,99,74},
	    (struct st_rip3_rect){20,15,39,29}, "90") == 0);
	CHECK(scene(ST_RIP3_CANVAS_1024_768, 1024,768, (struct st_rip3_rect){12,9,127,95},
	    (struct st_rip3_rect){25,19,50,37}, "116") == 0);
	CHECK(validation() == 0);
	CHECK(flags_and_delete() == 0);
	CHECK(field_widths() == 0);
	return 0;
}
