/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_styles.h"
#include "st_rip3_backup.h"
#include "st_rip3_ports.h"
#include "st_rip3_host_variables.h"
#include "st_rip3_table_host.h"
#include "st_pixels.h"
#include "io_error.h"

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

static struct st_rip3_style
value(unsigned number)
{
	static const unsigned vertical[] = {0,4,8,16};
	struct st_rip3_style style = {
		.drawing_color=0x112200+number, .background_color=0x445500+number,
		.fill_color=0x330000+number, .pen={0},
		.font={number%11,number%2,number%10+1,number%3 | vertical[number%4]},
		.outline={.compatibility_s=number,.compatibility_f=35-number,.flags=number*73,.size=number,.shadow=number+1,.string_rotation=(number%4)*900,.character_rotation=((number+1)%4)*900,.spacing=100+number,.baseline=number*3},
		.write_mode=(enum st_rip3_write_mode)(number%5), .line_type=number%5, .borders=number%2!=0, .font_mode=number%2!=0 ? ST_RIP3_FONT_OUTLINE : ST_RIP3_FONT_CLASSIC,
	};
	(void)snprintf(style.outline.name,sizeof(style.outline.name),"font-%u",number);
	style.pen.pattern = (uint16_t)(0xa500+number);
	style.pen.thickness = number+1;
	style.pen.draw_background = (number%2) != 0;
	for (unsigned i = 0; i < 8; i++) style.fill.rows[i] = (uint8_t)(number+i*17);
	style.fill.origin = (struct st_rip3_point){(int32_t)number-20,17-(int32_t)number};
	return style;
}

static bool
equal(const struct st_rip3_style *a, const struct st_rip3_style *b)
{
	return a->drawing_color == b->drawing_color && a->background_color == b->background_color
	    && a->fill_color == b->fill_color && a->pen.pattern == b->pen.pattern && a->line_type == b->line_type
	    && a->pen.thickness == b->pen.thickness && a->pen.draw_background == b->pen.draw_background
	    && memcmp(a->fill.rows, b->fill.rows, 8) == 0
	    && a->fill.origin.x == b->fill.origin.x && a->fill.origin.y == b->fill.origin.y
	    && a->font.font == b->font.font && a->font.direction == b->font.direction
	    && a->font.magnification == b->font.magnification && a->font.flags == b->font.flags
	    && a->write_mode == b->write_mode && a->borders == b->borders && a->font_mode == b->font_mode
	    && a->outline.compatibility_s == b->outline.compatibility_s && a->outline.compatibility_f == b->outline.compatibility_f
	    && a->outline.flags == b->outline.flags && a->outline.size == b->outline.size && a->outline.shadow == b->outline.shadow
	    && a->outline.string_rotation == b->outline.string_rotation && a->outline.character_rotation == b->outline.character_rotation
	    && a->outline.spacing == b->outline.spacing && a->outline.baseline == b->outline.baseline
	    && strcmp(a->outline.name,b->outline.name) == 0;
}

static int
expect(struct st_rip3_styles *styles, unsigned number, const struct st_rip3_style *expected)
{
	struct st_rip3_style actual;
	CHECK(st_rip3_styles_get(styles, number, &actual) == 0 && equal(&actual, expected));
	return 0;
}

static struct st_rip3_style
defaults(void)
{
	struct st_rip3_style result = {.drawing_color=15,.fill_color=15,
	    .pen={.pattern=0xffff,.thickness=1},.font={0,0,1,0},
	    .write_mode=ST_RIP3_COPY,.borders=true,.outline={.spacing=100}};
	memset(result.fill.rows,255,8);
	return result;
}

static int
entry_copies(void)
{
	struct st_rip3_styles *styles=NULL;
	struct st_rip3_style initial=defaults(), source=value(5), changed=value(6);
	struct st_rip3_style_info info;
	unsigned current;
	const unsigned destinations[]={0,6,35,35};
	CHECK(st_rip3_styles_create(&styles,&initial)==0);
	CHECK(st_rip3_styles_define(styles,5,&source)==0);
	CHECK(st_rip3_styles_protect(styles,5,true)==0);
	CHECK(st_rip3_styles_switch(styles,6)==0);
	CHECK(st_rip3_styles_copy_entries(styles,5,destinations,4)==0);
	CHECK(st_rip3_styles_current(styles,&current)==0 && current==6);
	for (size_t i=0;i<4;i++) {
		CHECK(expect(styles,destinations[i],&source)==0);
		CHECK(st_rip3_styles_info(styles,destinations[i],&info)==0);
		CHECK(info.defined && info.protected==(destinations[i]!=0));
	}
	/* A late protected, self or invalid destination cannot partially define
	 * earlier entries. A protected source remains readable. */
	const unsigned protected[]={7,6,8}, self[]={7,5,8}, range[]={7,36,8};
	CHECK(st_rip3_styles_copy_entries(styles,5,protected,3)==IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_styles_copy_entries(styles,5,self,3)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_styles_copy_entries(styles,5,range,3)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_styles_copy_entries(styles,34,destinations,4)==IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_styles_copy_entries(styles,36,destinations,4)==IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_styles_copy_entries(styles,5,NULL,1)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_styles_copy_entries(styles,5,destinations,0)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_styles_copy_entries(NULL,5,destinations,4)==IO_ERROR_INVALID_ARGUMENT);
	for (unsigned i=7;i<=8;i++) {
		CHECK(st_rip3_styles_info(styles,i,&info)==0 && !info.defined);
	}
	CHECK(st_rip3_styles_protect(styles,6,false)==0);
	CHECK(st_rip3_styles_set(styles,6,&changed)==0);
	CHECK(expect(styles,5,&source)==0 && expect(styles,0,&source)==0);
	CHECK(expect(styles,35,&source)==0 && expect(styles,6,&changed)==0);
	CHECK(st_rip3_styles_info(styles,5,&info)==0 && info.protected);
	st_rip3_styles_destroy(styles);return 0;
}

static int
records(void)
{
	struct st_rip3_styles *styles = NULL;
	struct st_rip3_style initial = value(0), actual, changed;
	struct st_rip3_style_info info;
	unsigned current = 99;

	CHECK(st_rip3_styles_create(&styles, &initial) == 0);
	initial.drawing_color = 7; /* The caller's supplied record is never borrowed. */
	CHECK(st_rip3_styles_current(styles, &current) == 0 && current == 0);
	for (unsigned i = 0; i < 36; i++) {
		CHECK(st_rip3_styles_info(styles, i, &info) == 0);
		CHECK(info.defined == (i == 0) && !info.protected);
	}
	for (unsigned i = 1; i < 36; i++) {
		struct st_rip3_style previous = defaults(), expected = value(i);
		CHECK(st_rip3_styles_protect(styles, i, true) == 0); /* Undefined is a no-op. */
		CHECK(st_rip3_styles_switch(styles, i) == 0);
		CHECK(expect(styles, i, &previous) == 0);
		CHECK(st_rip3_styles_info(styles, i, &info) == 0 && !info.protected);
		CHECK(st_rip3_styles_set(styles, i, &expected) == 0);
	}
	for (unsigned i = 0; i < 36; i++) {
		struct st_rip3_style expected = value(i);
		CHECK(st_rip3_styles_switch(styles, i) == 0 && expect(styles, i, &expected) == 0);
	}
	CHECK(st_rip3_styles_switch(styles, 7) == 0);
	CHECK(st_rip3_styles_protect(styles, 7, true) == 0);
	changed = value(8);
	CHECK(st_rip3_styles_set(styles, 7, &changed) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_styles_define(styles, 7, &changed) == IO_ERROR_PERMISSION_DENIED);
	actual = value(7);
	CHECK(expect(styles, 7, &actual) == 0);
	CHECK(st_rip3_styles_switch(styles, 7) == 0); /* Protected selection remains legal. */
	CHECK(st_rip3_styles_current(styles, &current) == 0 && current == 7);
	CHECK(st_rip3_styles_protect(styles, 7, false) == 0);
	CHECK(st_rip3_styles_set(styles, 7, &changed) == 0);
	st_rip3_styles_destroy(styles);
	CHECK(st_rip3_styles_create(&styles, &initial) == 0);
	CHECK(st_rip3_styles_switch(styles, 1) == 0 && st_rip3_styles_protect(styles, 1, true) == 0);
	changed = defaults();
	CHECK(st_rip3_styles_switch(styles, 2) == 0 && expect(styles, 2, &changed) == 0);
	CHECK(st_rip3_styles_info(styles, 2, &info) == 0 && !info.protected);
	st_rip3_styles_destroy(styles);
	return 0;
}

static int
errors(void)
{
	struct st_rip3_styles *styles = NULL, *sentinel = NULL;
	struct st_rip3_style initial = value(0), invalid, output = value(6);
	struct st_rip3_style_info info = {true,true};
	unsigned current = 77;

	CHECK(st_rip3_style_defaults(NULL)==IO_ERROR_INVALID_ARGUMENT);
	invalid=defaults();CHECK(st_rip3_style_defaults(&output)==0 && equal(&output,&invalid));
	output=value(6);
	CHECK(st_rip3_styles_create(NULL, &initial) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_styles_create(&sentinel, NULL) == IO_ERROR_INVALID_ARGUMENT && sentinel == NULL);
	CHECK(st_rip3_styles_create(&styles, &initial) == 0);
	CHECK(st_rip3_styles_current(NULL, &current) == IO_ERROR_INVALID_ARGUMENT && current == 77);
	CHECK(st_rip3_styles_current(styles, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_styles_info(styles, 36, &info) == IO_ERROR_OUT_OF_RANGE && info.defined && info.protected);
	CHECK(st_rip3_styles_get(styles, 1, &output) == IO_ERROR_NOT_FOUND);
	invalid = value(6); CHECK(equal(&output, &invalid));
	CHECK(st_rip3_styles_set(styles, 1, &initial) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_styles_switch(styles, 36) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_styles_protect(styles, 0, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_styles_define(styles, 36, &initial) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_styles_define(styles, 1, NULL) == IO_ERROR_INVALID_ARGUMENT);
	for (unsigned which = 0; which < 25; which++) {
		invalid = initial;
		switch (which) {
			case 0: invalid.drawing_color = 0x1000000; break;
			case 1: invalid.background_color = 0x1000000; break;
			case 2: invalid.fill_color = 0x1000000; break;
			case 3: invalid.pen.pattern = 0; break;
			case 4: invalid.pen.thickness = 0; break;
			case 5: invalid.pen.thickness = UINT32_MAX; break;
			case 6: invalid.font.font = 11; break;
			case 7: invalid.font.direction = 2; break;
			case 8: invalid.font.magnification = 0; break;
			case 9: invalid.font.flags = 3; break;
			case 10: invalid.write_mode = (enum st_rip3_write_mode)5; break;
			case 11: invalid.line_type = 5; break;
			case 12: invalid.write_mode = (enum st_rip3_write_mode)-1; break;
			case 13: invalid.font_mode = (enum st_rip3_font_mode)2; break;
			case 14: invalid.font_mode = (enum st_rip3_font_mode)-1; break;
			case 15: invalid.outline.compatibility_s = 64; break;
			case 16: invalid.outline.compatibility_f = 64; break;
			case 17: invalid.outline.flags = 0x1000000; break;
			case 18: invalid.outline.size = 4096; break;
			case 19: invalid.outline.shadow = 4096; break;
			case 20: invalid.outline.string_rotation = 3600; break;
			case 21: invalid.outline.character_rotation = 1; break;
			case 22: invalid.outline.spacing = 4096; break;
			case 23: invalid.outline.baseline = UINT32_MAX; break;
			default: memset(invalid.outline.name,'A',sizeof(invalid.outline.name)); break;
		}
		CHECK(st_rip3_styles_create(&sentinel, &invalid) == IO_ERROR_OUT_OF_RANGE && sentinel == NULL);
		CHECK(st_rip3_styles_define(styles, 1, &invalid) == IO_ERROR_OUT_OF_RANGE);
		CHECK(st_rip3_styles_set(styles, 0, &invalid) == IO_ERROR_OUT_OF_RANGE);
		CHECK(expect(styles, 0, &initial) == 0);
		CHECK(st_rip3_styles_info(styles, 1, &info) == 0 && !info.defined && !info.protected);
	}
	invalid = initial;
	invalid.drawing_color = 0xffffff; invalid.background_color = 0; invalid.fill_color = 0xffffff;
	invalid.pen.pattern = 0xffff; invalid.pen.thickness = INT32_MAX;
	invalid.fill.origin = (struct st_rip3_point){INT32_MIN,INT32_MAX};
	invalid.font = (struct st_rip3_classic_text_style){10,1,10,50};
	invalid.write_mode = ST_RIP3_NOT;
	CHECK(st_rip3_styles_define(styles, 1, &invalid) == 0 && expect(styles, 1, &invalid) == 0);
	CHECK(st_rip3_styles_current(styles, &current) == 0 && current == 0);
	CHECK(st_rip3_styles_protect(styles, 0, false) == IO_ERROR_OUT_OF_RANGE);
	st_rip3_styles_destroy(styles);
	st_rip3_styles_destroy(NULL);
	return 0;
}

static int
snapshots(void)
{
	struct st_rip3_styles *original = NULL, *live = NULL;
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_style initial = value(0), changed = value(35);
	struct st_rip3_style_info info;
	unsigned current;

	CHECK(st_rip3_styles_create(&original, &initial) == 0);
	CHECK(st_rip3_styles_backup_create(&backup) == 0);
	/* Restore also removes later definitions, including protected ones. */
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, original) == 0);
	CHECK(st_rip3_styles_define(original, 35, &changed) == 0);
	CHECK(st_rip3_styles_protect(original, 35, true) == 0 && st_rip3_styles_switch(original, 35) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, original) == 0);
	CHECK(st_rip3_styles_info(original, 35, &info) == 0 && info.defined && !info.protected);
	CHECK(st_rip3_styles_current(original, &current) == 0 && current == 35);
	initial = defaults();CHECK(expect(original,35,&initial)==0);
	for (unsigned i = 1; i < 36; i++) {
		struct st_rip3_style expected = value(i);
		CHECK(st_rip3_styles_define(original, i, &expected) == 0);
		CHECK(st_rip3_styles_protect(original, i, (i%2) != 0) == 0);
	}
	CHECK(st_rip3_styles_switch(original, 17) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, original) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_SLOT, 3, original) == 0);
	CHECK(st_rip3_backup_protect(backup, 3, true) == 0);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_STACK, 0, original) == 0);
	CHECK(st_rip3_styles_protect(original, 17, false) == 0);
	CHECK(st_rip3_styles_set(original, 17, &changed) == 0);
	st_rip3_styles_destroy(original); /* Every saved value must outlive its owner. */
	CHECK(st_rip3_styles_create(&live, &changed) == 0);
	for (unsigned place = 0; place < 3; place++) {
		CHECK(st_rip3_backup_restore(backup, place == 0 ? ST_RIP3_BACKUP_STACK
		    : place == 1 ? ST_RIP3_BACKUP_SLOT : ST_RIP3_BACKUP_BASE, place == 1 ? 3 : 0, live) == 0);
		CHECK(st_rip3_styles_current(live, &current) == 0 && current == 0);
		for (unsigned i = 0; i < 36; i++) {
			struct st_rip3_style expected = value(i);
			CHECK(expect(live, i, &expected) == 0);
			CHECK(st_rip3_styles_info(live, i, &info) == 0 && info.defined && info.protected == ((i%2) != 0));
		}
		CHECK(st_rip3_styles_protect(live, 17, false) == 0);
		CHECK(st_rip3_styles_set(live, 17, &changed) == 0);
	}
	/* A protected numbered snapshot remains immutable through repeated restores. */
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 3, live) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_SLOT, 3, live) == 0);
	initial = value(17); CHECK(expect(live, 17, &initial) == 0);
	st_rip3_styles_destroy(live);
	st_rip3_backup_destroy(backup);
	return 0;
}

struct answer { const char *expected; unsigned replies, errors; bool mismatch; };

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct answer *seen = context;
	seen->replies++;
	seen->mismatch = seen->expected == NULL || length != strlen(seen->expected) || memcmp(bytes, seen->expected, length) != 0;
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct answer *seen = context;
	(void)kind;
	seen->errors++;
}

static int
reference(const struct st_rip3_host_variables *host, const char *text, const char *expected)
{
	struct answer seen = {.expected=expected};
	const struct st_rip3_host_sink sink = {&seen,reply,diagnostic};
	st_rip3_host_variables_process(host, &sink, (const uint8_t *)text, strlen(text));
	if (seen.errors != (expected == NULL ? 1u : 0u) || seen.replies != (expected != NULL && expected[0] != 0 ? 1u : 0u))
		fprintf(stderr, "reference %s: errors %u, replies %u\n", text, seen.errors, seen.replies);
	CHECK(seen.errors == (expected == NULL ? 1u : 0u) && seen.replies == (expected != NULL && expected[0] != 0 ? 1u : 0u) && !seen.mismatch);
	return 0;
}

static int
host_metadata(void)
{
	struct st_rip3_styles *styles = NULL;
	struct st_rip3_backup *backup = NULL;
	struct st_rip3_table_host tables = {0};
	struct st_rip3_host_variables host = {.tables=&tables};
	struct st_rip3_style initial = value(0), changed = value(1);

	CHECK(st_rip3_styles_create(&styles, &initial) == 0 && st_rip3_styles_backup_create(&backup) == 0);
	tables.tables[ST_RIP3_TABLE_STYLE] = (struct st_rip3_table_binding){styles,backup};
	CHECK(reference(&host, "$CUR(STYLE)$", "0") == 0);
	CHECK(reference(&host, "$PROT(STYLE,CUR)$", "") == 0); /* Zero cannot be protected. */
	CHECK(reference(&host, "$ISPROT(STYLE,CUR)$", "0") == 0);
	CHECK(reference(&host, "$INUSE(STYLE,1)$", "0") == 0);
	CHECK(reference(&host, "$ISPROT(STYLE,1)$", "-1") == 0);
	CHECK(reference(&host, "$PROT(STYLE,1)$", "") == 0);
	CHECK(st_rip3_styles_switch(styles, 1) == 0 && st_rip3_styles_set(styles, 1, &changed) == 0);
	CHECK(reference(&host, "$INUSE(STYLE,1)$", "1") == 0);
	CHECK(reference(&host, "$PROT(STYLE,1)$", "") == 0);
	CHECK(reference(&host, "$ISPROT(STYLE,1)$", "1") == 0);
	CHECK(st_rip3_styles_set(styles, 1, &initial) == IO_ERROR_PERMISSION_DENIED);
	CHECK(reference(&host, "$UNPROT(STYLE,1,36)$", NULL) == 0);
	CHECK(reference(&host, "$ISPROT(STYLE,1)$", "1") == 0);
	CHECK(reference(&host, "$UNPROT(STYLE,1)$", "") == 0);
	CHECK(reference(&host, "$INUSE(STYLE,ANY)$", "1") == 0);
	CHECK(reference(&host, "$INUSE(STYLE,ALL)$", "0") == 0);
	CHECK(reference(&host, "$PROT(STYLE,ALL)$", "") == 0);
	CHECK(reference(&host, "$ISPROT(STYLE,1)$", "1") == 0);
	CHECK(reference(&host, "$UNPROT(STYLE,ALL)$", "") == 0);
	CHECK(reference(&host, "$ISPROT(STYLE,1)$", "0") == 0);
	CHECK(reference(&host, "$COPY(STYLE,TBL,BASE,S0,PUSH)$", "") == 0);
	CHECK(st_rip3_styles_switch(styles, 2) == 0 && st_rip3_styles_set(styles, 1, &initial) == 0);
	CHECK(reference(&host, "$CUR(STYLE,2)$", "") == 0);
	CHECK(reference(&host, "$CUR(STYLE)$", "2") == 0);
	CHECK(reference(&host, "$COPY(STYLE,BASE,TBL)$", "") == 0);
	CHECK(reference(&host, "$CUR(STYLE)$", "2") == 0 && expect(styles, 1, &changed) == 0);
	CHECK(reference(&host, "$PROT(STYLE,S0)$", "") == 0);
	CHECK(reference(&host, "$COPY(STYLE,S0,TBL)$", "") == 0 && expect(styles, 1, &changed) == 0);
	CHECK(reference(&host, "$INUSE(STYLE,S0)$", "1") == 0);
	CHECK(reference(&host, "$SGS$", "") == 0);
	CHECK(st_rip3_styles_set(styles,1,&initial)==0 && st_rip3_styles_switch(styles,3)==0);
	CHECK(reference(&host, "$RGS$", "") == 0 && expect(styles,1,&changed)==0);
	CHECK(reference(&host, "$CUR(STYLE)$", "3") == 0);
	st_rip3_styles_destroy(styles);
	st_rip3_backup_destroy(backup);
	return 0;
}

static int
render(enum st_rip3_canvas canvas, uint32_t width, uint32_t height)
{
	struct st_rip3_style initial = {.drawing_color=3,.background_color=1,.fill_color=4,
	    .pen={.pattern=0xffff,.thickness=1}, .font={0,0,1,0}, .write_mode=ST_RIP3_COPY};
	struct st_rip3_style selected;
	struct st_rip3_styles *styles = NULL;
	struct st_rip3_ports *ports = NULL;
	struct st_rip3_palette palette;
	struct st_pixel_buffer *capture = NULL;
	const struct st_rip3_rect rectangle = {1,1,4,3}, solid_bar = {12,1,14,2}, patterned_bar = {8,4,10,5};
	const struct st_rip3_point first = {1,6}, last = {8,6}, later_first = {1,8}, later_last = {8,8};

	CHECK(st_rip3_fill_solid(&initial.fill, true) == 0);
	CHECK(st_rip3_styles_create(&styles, &initial) == 0 && st_rip3_ports_create(&ports, canvas) == 0);
	CHECK(st_rip3_styles_get(styles, 0, &selected) == 0);
	CHECK(st_rip3_draw_rectangle(ports, &rectangle, &selected.pen, (uint8_t)selected.drawing_color,
	    (uint8_t)selected.background_color, selected.write_mode) == 0);
	CHECK(st_rip3_styles_define(styles,1,&initial)==0);
	CHECK(st_rip3_styles_switch(styles, 1) == 0 && st_rip3_styles_get(styles, 1, &selected) == 0);
	selected.drawing_color = 4; selected.fill_color = 2; selected.write_mode = ST_RIP3_XOR;
	selected.pen.pattern = 0xaaaa; selected.pen.draw_background = true;
	for (unsigned i = 0; i < 8; i++) selected.fill.rows[i] = 0xa0;
	CHECK(st_rip3_styles_set(styles, 1, &selected) == 0 && st_rip3_styles_protect(styles, 1, true) == 0);
	CHECK(st_rip3_draw_line(ports, &first, &last, &selected.pen, (uint8_t)selected.drawing_color,
	    (uint8_t)selected.background_color, selected.write_mode) == 0);
	CHECK(st_rip3_draw_bar(ports, &patterned_bar, &selected.fill, (uint8_t)selected.fill_color,
	    (uint8_t)selected.background_color, selected.write_mode) == 0);
	CHECK(st_rip3_styles_switch(styles, 0) == 0 && st_rip3_styles_get(styles, 0, &selected) == 0);
	CHECK(st_rip3_draw_line(ports, &later_first, &later_last, &selected.pen, (uint8_t)selected.drawing_color,
	    (uint8_t)selected.background_color, selected.write_mode) == 0);
	CHECK(st_rip3_draw_bar(ports, &solid_bar, &selected.fill, (uint8_t)selected.fill_color,
	    (uint8_t)selected.background_color, selected.write_mode) == 0);
	for (unsigned i = 0; i < 256; i++) palette.rgb[i] = i*UINT32_C(0x010101);
	CHECK(st_rip3_ports_snapshot(ports, &palette, &capture) == 0);
	for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++) {
		unsigned expected = 0;
		if (((x == 1 || x == 4) && y >= 1 && y <= 3)
		    || ((y == 1 || y == 3) && x >= 1 && x <= 4)) expected = 3;
		if (x >= 12 && x <= 14 && y >= 1 && y <= 2) expected = 4;
		if (x >= 8 && x <= 10 && y >= 4 && y <= 5) expected = x == 9 ? 1 : 2;
		if (x >= 1 && x <= 8 && y == 6) expected = x%2 ? 4 : 1;
		if (x >= 1 && x <= 8 && y == 8) expected = 3;
		CHECK(capture->primary[(size_t)y*width+x] == (ST_PIXEL_COLOR_RGB_TAG | expected*UINT32_C(0x010101)));
	}
	CHECK(expect(styles, 0, &initial) == 0);
	st_pixel_buffer_destroy(capture);
	st_rip3_ports_destroy(ports);
	st_rip3_styles_destroy(styles);
	return 0;
}

static int
soft_reset(void)
{
	struct st_rip3_styles *styles=NULL;struct st_rip3_backup *backup=NULL;
	struct st_rip3_style boot=defaults();
	CHECK(st_rip3_styles_create(&styles,&boot)==0 && st_rip3_styles_backup_create(&backup)==0);
	for (unsigned i=1;i<36;i++) {
		struct st_rip3_style custom=value(i);
		CHECK(st_rip3_styles_define(styles,i,&custom)==0);
		if (i%2) CHECK(st_rip3_styles_protect(styles,i,true)==0);
	}
	CHECK(st_rip3_styles_switch(styles,5)==0);
	CHECK(st_rip3_backup_save(backup,ST_RIP3_BACKUP_BASE,0,styles)==0);
	CHECK(st_rip3_styles_soft_reset(NULL)==IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_styles_soft_reset(styles)==0);
	unsigned current=99;CHECK(st_rip3_styles_current(styles,&current)==0 && current==0);
	for (unsigned i=0;i<36;i++) {
		struct st_rip3_style_info info;
		CHECK(st_rip3_styles_info(styles,i,&info)==0 && info.defined==(i==0 || i%2) && info.protected==(i%2!=0));
		if (info.defined) {struct st_rip3_style expected=i==0 ? boot : value(i);CHECK(expect(styles,i,&expected)==0);}
	}
	CHECK(st_rip3_backup_restore(backup,ST_RIP3_BACKUP_BASE,0,styles)==0);
	struct st_rip3_style before=value(6);CHECK(expect(styles,6,&before)==0);
	st_rip3_backup_destroy(backup);st_rip3_styles_destroy(styles);return 0;
}

int
main(void)
{
	CHECK(records() == 0);
	CHECK(entry_copies() == 0);
	CHECK(errors() == 0);
	CHECK(snapshots() == 0);
	CHECK(host_metadata() == 0);
	CHECK(soft_reset() == 0);
	CHECK(render(ST_RIP3_CANVAS_640_480, 640,480) == 0);
	CHECK(render(ST_RIP3_CANVAS_800_600, 800,600) == 0);
	CHECK(render(ST_RIP3_CANVAS_1024_768, 1024,768) == 0);
	return 0;
}
