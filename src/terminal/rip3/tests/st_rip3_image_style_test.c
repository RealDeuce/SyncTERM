/* Independent stored-boundary, macro, inactive/protected and reset contracts. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_commands.h"
#include "st_rip3_environments.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_number.h"
#include "st_rip3_ports.h"
#include "st_rip3_table_host.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#expression); \
		return 1; \
	} \
} while (0)

struct fixture {
	struct st_rip3_command_host wire;
	struct st_rip3_table_host tables;
	struct st_rip3_host_variables host;
	unsigned errors;
	char reply[128]; size_t length;
};

static void
wire_error(void *context, enum st_rip3_command_diagnostic kind)
{
	struct fixture *f = context; (void)kind; f->errors++;
}

static void
macro_error(void *context, enum st_rip3_host_diagnostic kind)
{
	struct fixture *f = context; (void)kind; f->errors++;
}

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct fixture *f = context;
	if (length >= sizeof(f->reply)-f->length) { f->errors++; return; }
	memcpy(f->reply+f->length,bytes,length); f->length += length; f->reply[f->length] = 0;
}

static int
wire(struct fixture *f, const char *body, bool error)
{
	unsigned before = f->errors;
	const struct st_rip3_command_sink sink = {f,wire_error};
	st_rip3_commands_process(&f->wire,&sink,(const uint8_t *)body,strlen(body));
	CHECK(f->errors == before+(error ? 1u : 0u)); return 0;
}

static int
macro(struct fixture *f, const char *body, const char *expected, bool error)
{
	unsigned before = f->errors; f->length = 0; f->reply[0] = 0;
	const struct st_rip3_host_sink sink = {f,reply,macro_error};
	st_rip3_host_variables_process(&f->host,&sink,(const uint8_t *)body,strlen(body));
	CHECK(f->errors == before+(error ? 1u : 0u) && strcmp(f->reply,expected) == 0); return 0;
}

static int
style(struct fixture *f, unsigned number, bool automatic, int32_t x0, int32_t y0,
    int32_t x1, int32_t y1, unsigned flags)
{
	struct st_rip3_port_info info;
	CHECK(st_rip3_ports_info(f->wire.ports,number,&info) == 0 && info.defined);
	CHECK(info.image.automatic == automatic && info.image.enabled && info.image.flags == flags);
	CHECK(info.image.area.x0 == x0 && info.image.area.y0 == y0 && info.image.area.x1 == x1 && info.image.area.y1 == y1);
	return 0;
}

static int
create(struct fixture *f, enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	CHECK(st_rip3_ports_create(&f->wire.ports,canvas) == 0 && st_rip3_environments_create(&f->wire.environments,canvas) == 0);
	struct st_rip3_environment env;
	CHECK(st_rip3_environments_get(f->wire.environments,0,&env) == 0);
	env.world_width = width*2; env.world_height = height*2; env.port = 9; env.palette = 7;
	CHECK(st_rip3_environments_set(f->wire.environments,0,&env) == 0);
	f->tables.tables[ST_RIP3_TABLE_PORT].live = f->wire.ports;
	f->tables.tables[ST_RIP3_TABLE_ENV].live = f->wire.environments;
	f->host.tables = &f->tables;
	return 0;
}

static void
destroy(struct fixture *f)
{
	st_rip3_ports_destroy(f->wire.ports); st_rip3_environments_destroy(f->wire.environments);
}

static int
wire_matrix(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0}; char body[80];
	const struct st_rip3_rect bounds = {0,0,299,299}, view = {3,5,103,105};
	CHECK(create(&f,canvas,width,height) == 0);
	CHECK(st_rip3_ports_define(f.wire.ports,1,&bounds,true) == 0 && st_rip3_ports_switch(f.wire.ports,1) == 0);
	CHECK(st_rip3_ports_viewport(f.wire.ports,&view) == 0 && st_rip3_ports_activate(f.wire.ports,false) == 0);
	CHECK(st_rip3_ports_protect(f.wire.ports,1,true) == 0);
	for (unsigned radix = 36; radix <= 64; radix += 28) for (unsigned width_xy = 2; width_xy <= 5; width_xy++) {
		struct st_rip3_environment env;
		CHECK(st_rip3_environments_get(f.wire.environments,0,&env) == 0);
		env.base_math = radix; env.coordinate_size = width_xy;
		CHECK(st_rip3_environments_set(f.wire.environments,0,&env) == 0);
		for (unsigned flags = 0; flags <= 64; flags++) {
			memcpy(body,"1i",2);
			const uint64_t reversed[] = {220,240,20,40};
			for (size_t i = 0; i < 4; i++) CHECK(st_rip3_number_encode(body+2+i*width_xy,width_xy,reversed[i],width_xy,radix) == 0);
			CHECK(st_rip3_number_encode(body+2+4*width_xy,4,flags,4,radix) == 0);
			body[6+4*width_xy] = 0;
			CHECK(wire(&f,body,flags == 64) == 0 && style(&f,1,false,10,20,111,121,flags == 64 ? 63 : flags) == 0);
		}
		for (size_t reserved = 0; reserved <= 12; reserved++) {
			memset(body+6+4*width_xy,'0',reserved); body[6+4*width_xy+reserved] = 0;
			CHECK(wire(&f,body,true) == 0); /* Retained flags64 is invalid. */
		}
		CHECK(st_rip3_number_encode(body+2+4*width_xy,4,5,4,radix) == 0);
		for (size_t reserved = 0; reserved <= 12; reserved++) {
			memset(body+6+4*width_xy,radix == 64 ? '&' : 'Z',reserved); body[6+4*width_xy+reserved] = 0;
			CHECK(wire(&f,body,false) == 0 && style(&f,1,false,10,20,111,121,5) == 0);
		}
	}
	struct st_rip3_port_info info; unsigned current;
	CHECK(st_rip3_ports_info(f.wire.ports,1,&info) == 0 && info.protected && !info.viewport_active
	    && memcmp(&info.viewport,&view,sizeof(view)) == 0);
	CHECK(st_rip3_ports_current(f.wire.ports,&current) == 0 && current == 1);
	/* Restore ordinary fields; reserved omission and the one-digit last
	 * nonreserved flags field do not disable the wire setter. */
	struct st_rip3_environment env;
	CHECK(st_rip3_environments_get(f.wire.environments,0,&env) == 0);
	env.base_math = 36; env.coordinate_size = 2; CHECK(st_rip3_environments_set(f.wire.environments,0,&env) == 0);
	CHECK(wire(&f,"1i000000005",false) == 0 && style(&f,1,false,0,0,1,1,5) == 0);
	CHECK(wire(&f,"1i1E4I80940004000000000000",false) == 0);
	CHECK(style(&f,1,false,25,81,145,165,4) == 0);
	CHECK(wire(&f,"1i..00010005Z. invalid",true) == 0 && wire(&f,"1i00000000?",true) == 0);
	CHECK(style(&f,1,false,25,81,145,165,4) == 0);
	CHECK(wire(&f,"1i..... reserved omitted is separate",true) == 0);
	CHECK(wire(&f,"1i...... complete",false) == 0 && style(&f,1,false,0,0,1,1,0) == 0);
	destroy(&f); return 0;
}

static int
macros(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0}; const struct st_rip3_rect bounds = {0,0,299,299};
	CHECK(create(&f,canvas,width,height) == 0);
	CHECK(st_rip3_ports_define(f.wire.ports,2,&bounds,true) == 0 && st_rip3_ports_switch(f.wire.ports,2) == 0);
	CHECK(wire(&f,"1i0K141E1O000F",false) == 0); /* (20,40)-(50,60), flags15. */
	CHECK(macro(&f,"$IMGSTYLE$","20:40:52:62:15",false) == 0);
	CHECK(macro(&f,"$IMGSTYLE(35)$","-1",false) == 0 && macro(&f,"$IMGSTYLE(35,20,40,220,240)$","-1",false) == 0);
	CHECK(st_rip3_ports_activate(f.wire.ports,false) == 0);
	CHECK(macro(&f,"$IMGSTYLE(CUR,20,40,220,240,ASPECT)$","-1",false) == 0);
	CHECK(macro(&f,"$IMGSTYLE$","-1",false) == 0 && style(&f,2,false,10,20,26,31,15) == 0);
	CHECK(st_rip3_ports_activate(f.wire.ports,true) == 0 && st_rip3_ports_protect(f.wire.ports,2,true) == 0);
	CHECK(macro(&f,"$IMGSTYLE(CUR,20,40,220,240)$","",false) == 0);
	CHECK(style(&f,2,false,10,20,26,31,15) == 0);
	CHECK(macro(&f,"$IMGSTYLE$","20:40:52:62:15",false) == 0);
	CHECK(st_rip3_ports_protect(f.wire.ports,2,false) == 0 && st_rip3_ports_switch(f.wire.ports,0) == 0);
	CHECK(macro(&f,"$IMGSTYLE(2,20,40,220,240,aspect,ASPECT,noclear)$","",false) == 0);
	CHECK(macro(&f,"$IMGSTYLE(2)$","20:40:222:242:5",false) == 0);
	CHECK(macro(&f,"$IMGSTYLE(2,220,240,20,40)$","",false) == 0 && style(&f,2,false,10,20,111,121,0) == 0);
	const char *const keywords[] = {"ASPECT","DELETE","NOCLEAR","USEPAL"};
	for (unsigned flags = 0; flags < 16; flags++) {
		char text[120] = "$IMGSTYLE(2,20,40,220,240";
		for (unsigned bit = 0; bit < 4; bit++) if ((flags&(1u<<bit)) != 0) {
			size_t at = strlen(text); (void)snprintf(text+at,sizeof(text)-at,",%s",keywords[bit]);
		}
		size_t at = strlen(text); memcpy(text+at,")$",3);
		CHECK(macro(&f,text,"",false) == 0 && style(&f,2,false,10,20,111,121,flags) == 0);
	}
	CHECK(macro(&f,"$IMGSTYLE(2,220,240,20,40)$","",false) == 0);
	const char *const invalid[] = {"$IMGSTYLE(36)$","$IMGSTYLE(2,20,40)$","$IMGSTYLE(2,20,40,220,240,3)$",
	    "$IMGSTYLE(2,20,40,no,240)$","$IMGSTYLE(2,20,40,220,240,WALLPAPER)$"};
	for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); i++) {
		CHECK(macro(&f,invalid[i],"",true) == 0 && style(&f,2,false,10,20,111,121,0) == 0);
	}
	/* A valid decimal endpoint whose far-edge increment cannot fit the
	 * stored record fails without leaving the temporary target selected. */
	struct st_rip3_environment env;
	CHECK(st_rip3_environments_get(f.wire.environments,0,&env) == 0);
	env.world_width = width; env.world_height = height;
	CHECK(st_rip3_environments_set(f.wire.environments,0,&env) == 0);
	CHECK(macro(&f,"$IMGSTYLE(2,0,0,2147483647,1)$","",true) == 0 && style(&f,2,false,10,20,111,121,0) == 0);
	unsigned current; CHECK(st_rip3_ports_current(f.wire.ports,&current) == 0 && current == 0);
	struct st_rip3_port_info info; CHECK(st_rip3_ports_info(f.wire.ports,35,&info) == 0 && !info.defined);
	destroy(&f); return 0;
}

static int
resets(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct fixture f = {0}; const struct st_rip3_rect bounds = {0,0,19,19}, area = {1,2,11,12};
	CHECK(create(&f,canvas,width,height) == 0);
	CHECK(style(&f,0,true,-1,-1,-1,-1,0) == 0);
	CHECK(st_rip3_ports_erase(f.wire.ports,9) == 0);
	for (unsigned i = 1; i <= 3; i++) {
		CHECK(st_rip3_ports_define(f.wire.ports,i,&bounds,i == 2) == 0);
		CHECK(st_rip3_ports_image_style(f.wire.ports,i,&area,i+4) == 0);
		if (i != 3) CHECK(st_rip3_ports_protect(f.wire.ports,i,true) == 0);
	}
	CHECK(st_rip3_ports_image_style(f.wire.ports,0,&area,15) == 0 && st_rip3_ports_switch(f.wire.ports,1) == 0);
	CHECK(st_rip3_ports_reset(f.wire.ports,false) == 0);
	CHECK(style(&f,0,true,-1,-1,-1,-1,0) == 0 && style(&f,1,true,-1,-1,-1,-1,0) == 0);
	CHECK(style(&f,2,false,1,2,11,12,6) == 0);
	struct st_rip3_port_info info; unsigned current; uint8_t value;
	CHECK(st_rip3_ports_info(f.wire.ports,3,&info) == 0 && !info.defined);
	CHECK(st_rip3_ports_info(f.wire.ports,1,&info) == 0 && info.protected && info.viewport.x1 == 19);
	CHECK(st_rip3_ports_current(f.wire.ports,&current) == 0 && current == 0);
	CHECK(st_rip3_ports_read(f.wire.ports,0,0,0,&value) == 0 && value == 9);
	CHECK(macro(&f,"$IMGSTYLE$","-2:-2:-2:-2:0",false) == 0);
	CHECK(st_rip3_ports_switch(f.wire.ports,2) == 0 && st_rip3_ports_reset(f.wire.ports,false) == 0);
	CHECK(style(&f,2,true,-1,-1,-1,-1,0) == 0);
	CHECK(st_rip3_ports_reset(f.wire.ports,true) == 0 && style(&f,0,true,-1,-1,-1,-1,0) == 0);
	CHECK(st_rip3_ports_info(f.wire.ports,1,&info) == 0 && !info.defined);
	CHECK(st_rip3_ports_info(f.wire.ports,2,&info) == 0 && !info.defined);
	destroy(&f); return 0;
}

int
main(void)
{
	const enum st_rip3_canvas canvases[] = {ST_RIP3_CANVAS_640_480,ST_RIP3_CANVAS_800_600,ST_RIP3_CANVAS_1024_768};
	const uint32_t widths[] = {640,800,1024}, heights[] = {480,600,768};
	for (size_t i = 0; i < 3; i++) CHECK(wire_matrix(canvases[i],widths[i],heights[i]) == 0
	    && macros(canvases[i],widths[i],heights[i]) == 0 && resets(canvases[i],widths[i],heights[i]) == 0);
	return 0;
}
