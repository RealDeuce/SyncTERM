/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_variables.h"
#include "st_rip3_table_host.h"
#include "st_rip3_ports.h"
#include "st_rip3_clipboard.h"
#include "st_rip3_resident_query.h"
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

struct observations {size_t replies, diagnostics; enum st_rip3_host_diagnostic last;};

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct observations *observed = context;
	(void)bytes; (void)length;
	observed->replies++;
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct observations *observed = context;
	observed->diagnostics++; observed->last = kind;
}

static int
run(const struct st_rip3_host_variables *host, const char *command, int expected)
{
	struct observations observed = {0};
	const struct st_rip3_host_sink sink = {&observed,reply,diagnostic};

	st_rip3_host_variables_process(host, &sink, (const uint8_t *)command, strlen(command));
	CHECK(observed.replies == 0 && observed.diagnostics == (expected >= 0 ? 1u : 0u));
	if (expected >= 0) CHECK((int)observed.last == expected);
	return 0;
}

static int
check_view(struct st_rip3_ports *ports, unsigned number, bool active,
    int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	struct st_rip3_port_info info;
	CHECK(st_rip3_ports_info(ports, number, &info) == 0 && info.defined);
	if (info.viewport_active!=active || info.viewport.x0!=x0 || info.viewport.y0!=y0 || info.viewport.x1!=x1 || info.viewport.y1!=y1)
		fprintf(stderr,"port %u: actual %d %d,%d,%d,%d; expected %d %d,%d,%d,%d\n",number,
		    info.viewport_active,info.viewport.x0,info.viewport.y0,info.viewport.x1,info.viewport.y1,active,x0,y0,x1,y1);
	CHECK(info.viewport_active == active && info.viewport.x0 == x0 && info.viewport.y0 == y0
	    && info.viewport.x1 == x1 && info.viewport.y1 == y1);
	return 0;
}

static int
reset_views(void)
{
	struct st_rip3_ports *ports=NULL;struct st_rip3_table_host tables={0};
	struct st_rip3_host_variables host={.tables=&tables};
	struct st_rip3_clipboard_selection clipboard={true,2};
	const struct st_rip3_rect screen={4,2,9,6},screen_inner={5,3,6,4},bitmap={0,0,3,2},inner={1,1,2,2},image={1,2,8,9};
	const struct st_rip3_copy_extent from={2,1,4,2},to={1,1,3,2};
	const struct st_rip3_cursor cursor={33,44};
	CHECK(st_rip3_ports_create_device(&ports,16,8)==0 && st_rip3_ports_erase(ports,7)==0);
	tables.tables[ST_RIP3_TABLE_PORT].live=ports;st_rip3_ports_bind_clipboard(ports,&clipboard);
	CHECK(st_rip3_ports_define(ports,1,&screen,false)==0 && st_rip3_ports_switch(ports,1)==0);
	CHECK(st_rip3_ports_viewport_device(ports,&screen_inner)==0 && st_rip3_ports_activate(ports,false)==0);
	CHECK(st_rip3_ports_define_query(ports,false,1,(const uint8_t *)"fixed",5,false)==0);
	CHECK(st_rip3_ports_define_query(ports,true,0,(const uint8_t *)"floating",8,false)==0);
	CHECK(st_rip3_ports_define_query(ports,false,0,(const uint8_t *)"zero",4,false)==0);
	CHECK(st_rip3_ports_protect(ports,1,true)==0 && st_rip3_ports_define(ports,2,&bitmap,true)==0);
	CHECK(st_rip3_ports_switch(ports,2)==0 && st_rip3_ports_erase(ports,9)==0);
	CHECK(st_rip3_ports_copy_device(ports,0,&from,2,&to,ST_RIP3_TRANSFER_COPY)==0);
	CHECK(st_rip3_ports_cursor(ports,2,&cursor)==0 && st_rip3_ports_image_style(ports,2,&image,5)==0);
	CHECK(st_rip3_ports_viewport_device(ports,&inner)==0 && st_rip3_ports_activate(ports,false)==0);
	/* RESET activates a full viewport; MVP deliberately skips inactive ones. */
	CHECK(run(&host,"$MVP$",-1)==0 && check_view(ports,2,false,1,1,2,2)==0);
	CHECK(run(&host,"$RESET(VIEW,2,BOGUS)$",ST_RIP3_HOST_INVALID_REFERENCE)==0);
	CHECK(check_view(ports,2,false,1,1,2,2)==0);
	CHECK(run(&host,"$RESET(vIeW,,)$",-1)==0 && check_view(ports,2,true,0,0,3,2)==0);
	struct st_rip3_port_info info;struct st_rip3_port_copy_pair pair;unsigned current;
	CHECK(st_rip3_ports_info(ports,2,&info)==0 && info.cursor.x==33 && info.cursor.y==44
	    && info.image.area.x0==1 && info.image.area.y0==2 && info.image.area.x1==8 && info.image.area.y1==9 && info.image.flags==5);
	CHECK(st_rip3_ports_copy_pair(ports,2,&pair)==0 && pair.set && pair.source.x0==2 && pair.source.y0==1
	    && pair.source.x1==4 && pair.source.y1==2 && pair.destination.x0==1 && pair.destination.y0==1
	    && pair.destination.x1==3 && pair.destination.y1==2);
	for (int32_t y=0;y<3;y++) for (int32_t x=0;x<4;x++) {
		uint8_t color;CHECK(st_rip3_ports_read(ports,2,x,y,&color)==0 && color==(y==1 && x>=1 && x<3 ? 7 : 9));
	}
	CHECK(run(&host,"$RESET(VIEW,0002,CUR,2,35)$",-1)==0);
	CHECK(st_rip3_ports_info(ports,35,&info)==0 && !info.defined);
	const char *const invalid[]={"$RESET(VIEW,2,36)$","$RESET(VIEW,2,BASE)$","$RESET(VIEW,2,ALL)$",
	    "$RESET(VIEW,2,S0)$","$RESET(VIEW,2,STACK)$","$RESET(VIEW,2,-1)$"};
	for (size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
		CHECK(st_rip3_ports_viewport_device(ports,&inner)==0 && st_rip3_ports_activate(ports,false)==0);
		CHECK(run(&host,invalid[i],ST_RIP3_HOST_INVALID_REFERENCE)==0 && check_view(ports,2,false,1,1,2,2)==0);
	}
	CHECK(run(&host,"$RESET(VIEW,TBL)$",-1)==0 && check_view(ports,2,true,0,0,3,2)==0
	    && check_view(ports,1,false,1,1,2,2)==0 && check_view(ports,0,true,0,0,15,7)==0);
	CHECK(st_rip3_ports_protect(ports,1,false)==0 && run(&host,"$RESET(VIEW,1)$",-1)==0
	    && check_view(ports,1,true,0,0,5,4)==0);
	const struct st_rip3_resident_query *fixed=st_rip3_ports_query(ports,false,1),*floating=st_rip3_ports_query(ports,true,0);
	CHECK(fixed!=NULL && fixed->length==5 && memcmp(fixed->bytes,"fixed",5)==0
	    && floating!=NULL && floating->length==8 && memcmp(floating->bytes,"floating",8)==0);
	fixed=st_rip3_ports_query(ports,false,0);CHECK(fixed!=NULL && fixed->length==4 && memcmp(fixed->bytes,"zero",4)==0);
	CHECK(st_rip3_ports_current(ports,&current)==0 && current==2 && clipboard.set && clipboard.port==2);
	tables.tables[ST_RIP3_TABLE_PORT].live=NULL;
	CHECK(run(&host,"$RESET(VIEW,2)$",ST_RIP3_HOST_UNSUPPORTED_REFERENCE)==0);
	CHECK(run(&host,"$RESET(VIEW,2,BOGUS)$",ST_RIP3_HOST_INVALID_REFERENCE)==0);
	st_rip3_ports_destroy(ports);return 0;
}

int
main(void)
{
	CHECK(reset_views()==0);
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_table_host tables = {0};
	struct st_rip3_host_variables host = {.tables=&tables,.background=8};
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	struct st_rip3_port_info info;
	const struct st_rip3_rect first = {10,20,14,24}, inner = {1,1,3,3};
	const struct st_rip3_rect offscreen = {0,0,3,3}, small_view = {1,1,2,2};
	const struct st_rip3_rect inactive = {30,40,33,43}, single = {50,60,50,60};
	const struct st_rip3_rect protected_offscreen = {0,0,1,1};
	unsigned current;
	uint8_t pixel;
	char command[150];
	size_t length;

	CHECK(st_rip3_ports_create(&ports, ST_RIP3_CANVAS_640_480) == 0);
	tables.tables[ST_RIP3_TABLE_PORT].live = ports;
	CHECK(st_rip3_ports_erase(ports, 1) == 0);
	CHECK(st_rip3_ports_define(ports, 1, &first, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 1) == 0 && st_rip3_ports_erase(ports, 3) == 0);
	CHECK(st_rip3_ports_viewport(ports, &inner) == 0);
	CHECK(st_rip3_ports_plot(ports, 1, 1, 15, ST_RIP3_COPY) == 0);
	CHECK(st_rip3_ports_protect(ports, 1, true) == 0);
	CHECK(st_rip3_ports_define(ports, 2, &offscreen, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0 && st_rip3_ports_erase(ports, 5) == 0);
	CHECK(st_rip3_ports_viewport(ports, &small_view) == 0);
	CHECK(st_rip3_ports_define(ports, 3, &inactive, false) == 0);
	CHECK(st_rip3_ports_switch(ports, 3) == 0 && st_rip3_ports_erase(ports, 6) == 0);
	CHECK(st_rip3_ports_viewport(ports, &small_view) == 0 && st_rip3_ports_activate(ports, false) == 0);
	CHECK(st_rip3_ports_define(ports, 4, &single, false) == 0);
	CHECK(st_rip3_ports_define(ports, 5, &protected_offscreen, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 5) == 0 && st_rip3_ports_erase(ports, 10) == 0);
	CHECK(st_rip3_ports_activate(ports, false) == 0 && st_rip3_ports_protect(ports, 5, true) == 0);
	CHECK(st_rip3_ports_switch(ports, 2) == 0);
	CHECK(run(&host, "$DVP(1,2,35)$", -1) == 0);
	CHECK(check_view(ports, 1, true, 1,1,3,3) == 0); /* Protected settings ignored. */
	CHECK(check_view(ports, 2, false, 1,1,2,2) == 0);
	CHECK(run(&host, "$AVP(2,BAD)$", ST_RIP3_HOST_INVALID_REFERENCE) == 0);
	CHECK(check_view(ports, 2, false, 1,1,2,2) == 0);
	CHECK(run(&host, "$AVP()$", -1) == 0);
	CHECK(check_view(ports, 2, true, 1,1,2,2) == 0);
	CHECK(run(&host, "$MVP(2,BAD)$", ST_RIP3_HOST_INVALID_REFERENCE) == 0);
	CHECK(check_view(ports, 2, true, 1,1,2,2) == 0);
	CHECK(run(&host, "$MVP$", -1) == 0);
	CHECK(check_view(ports, 2, true, 0,0,3,3) == 0);
	CHECK(st_rip3_ports_viewport(ports, &small_view) == 0);
	CHECK(run(&host, "$DVP(2,2)$", -1) == 0);
	CHECK(run(&host, "$DVP(CUR)$", -1) == 0);
	CHECK(run(&host, "$AVP(cUr)$", -1) == 0);
	CHECK(run(&host, "$dVp(aLl)$", -1) == 0);
	CHECK(run(&host, "$MVP(ALL)$", -1) == 0);
	CHECK(run(&host, "$MVP(5)$", -1) == 0);
	CHECK(check_view(ports, 1, true, 1,1,3,3) == 0);
	CHECK(check_view(ports, 2, false, 1,1,2,2) == 0);
	CHECK(check_view(ports, 3, false, 1,1,2,2) == 0);
	CHECK(run(&host, "$AVP(2,4,5)$", -1) == 0);
	CHECK(check_view(ports, 5, false, 0,0,1,1) == 0);
	CHECK(run(&host, "$MVP(2,4)$", -1) == 0);
	CHECK(check_view(ports, 2, true, 0,0,3,3) == 0);
	CHECK(check_view(ports, 4, true, 0,0,0,0) == 0); /* Maximize is not deactivation. */
	CHECK(run(&host, "$DVP(2,)$", -1) == 0);
	CHECK(check_view(ports, 2, false, 0,0,3,3) == 0);
	CHECK(run(&host, "$AVP(2)$", -1) == 0);
	CHECK(run(&host, "$DVP(ABCDEFGHIJKLMNOPQRST)$", ST_RIP3_HOST_INVALID_REFERENCE) == 0);
	CHECK(check_view(ports, 2, true, 0,0,3,3) == 0);
	CHECK(run(&host, "$EGW$", -1) == 0);
	CHECK(st_rip3_ports_read(ports, 2, 0, 0, &pixel) == 0 && pixel == 8);
	CHECK(st_rip3_ports_read(ports, 1, 2, 2, &pixel) == 0 && pixel == 15);
	CHECK(run(&host, "$EGW(5)$", -1) == 0);
	CHECK(run(&host, "$EGW(ALL)$", -1) == 0);
	host.background = 5;
	CHECK(run(&host, "$EGW(4,BAD)$", ST_RIP3_HOST_INVALID_REFERENCE) == 0);
	CHECK(st_rip3_ports_read(ports, 4, 0, 0, &pixel) == 0 && pixel == 8);
	host.background = 8;
	CHECK(run(&host, "$AVP(0)$", -1) == 0);
	for (int32_t y = 0; y < 4; y++) {
		for (int32_t x = 0; x < 4; x++)
			CHECK(st_rip3_ports_read(ports, 2, x, y, &pixel) == 0 && pixel == 8);
	}
	CHECK(st_rip3_ports_read(ports, 5, 0, 0, &pixel) == 0 && pixel == 10);
	CHECK(st_rip3_ports_info(ports, 35, &info) == 0 && !info.defined);
	CHECK(st_rip3_ports_info(ports, 1, &info) == 0 && info.protected
	    && info.bounds.x0 == 10 && info.bounds.y0 == 20 && info.bounds.x1 == 14 && info.bounds.y1 == 24);
	CHECK(st_rip3_ports_current(ports, &current) == 0 && current == 2);
	CHECK(st_rip3_palette_defaults(&palette) == 0);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	/* One owned coherent RGB scene capture: protected viewport erasure,
	 * inactive scene retention, one-pixel viewport and unaffected margins. */
	for (uint32_t y = 0; y < 480; y++) {
		for (uint32_t x = 0; x < 640; x++) {
			uint32_t expected = ST_PIXEL_COLOR_RGB_VALUE(0,0,170);
			if (x >= 10 && x <= 14 && y >= 20 && y <= 24) expected = ST_PIXEL_COLOR_RGB_VALUE(0,170,170);
			if (x >= 11 && x <= 13 && y >= 21 && y <= 23) expected = ST_PIXEL_COLOR_RGB_VALUE(85,85,85);
			if (x >= 30 && x <= 33 && y >= 40 && y <= 43) expected = ST_PIXEL_COLOR_RGB_VALUE(170,85,0);
			if (x == 50 && y == 60) expected = ST_PIXEL_COLOR_RGB_VALUE(85,85,85);
			CHECK(capture->primary[(size_t)y * 640 + x] == expected);
		}
	}
	st_pixel_buffer_destroy(capture);
	CHECK(run(&host, "$DVP$", -1) == 0);
	memcpy(command, "$AVP(", 5); length = 5;
	for (unsigned i = 0; i < 41; i++) {
		if (i != 0) command[length++] = ',';
		command[length++] = '2';
	}
	memcpy(command + length, ")$", 3);
	CHECK(run(&host, command, ST_RIP3_HOST_INVALID_REFERENCE) == 0);
	CHECK(check_view(ports, 2, false, 0,0,3,3) == 0);
	memcpy(command + length - 2, ")$", 3); /* Forty valid duplicate selectors. */
	CHECK(run(&host, command, -1) == 0);
	CHECK(check_view(ports, 2, true, 0,0,3,3) == 0);
	CHECK(run(&host, "$AVP$", -1) == 0);
	{
		const unsigned invalid[] = {2,36};
		CHECK(st_rip3_ports_operate_views(ports, invalid, 2, ST_RIP3_VIEW_DEACTIVATE, 0) == IO_ERROR_OUT_OF_RANGE);
		CHECK(check_view(ports, 2, true, 0,0,3,3) == 0);
		CHECK(st_rip3_ports_operate_views(ports, NULL, 0, ST_RIP3_VIEW_ERASE, 0) == 0);
		CHECK(st_rip3_ports_operate_views(ports, NULL, 1, ST_RIP3_VIEW_ERASE, 0) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_ports_operate_views(ports, invalid, 1, (enum st_rip3_view_operation)99, 0) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(st_rip3_ports_operate_views(NULL, invalid, 1, ST_RIP3_VIEW_ERASE, 0) == IO_ERROR_INVALID_ARGUMENT);
	}
	{
		struct st_rip3_host_variables absent = {0};
		CHECK(run(&absent, "$AVP(ALL)$", ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
		CHECK(run(&absent, "$EGW$", ST_RIP3_HOST_UNSUPPORTED_REFERENCE) == 0);
		CHECK(run(&absent, "$AVP(BAD)$", ST_RIP3_HOST_INVALID_REFERENCE) == 0);
	}
	st_rip3_ports_destroy(ports);
	return 0;
}
