/* Independent button defaults, metadata/reset defect and backup expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_backup.h"
#include "st_rip3_button_styles.h"

#define CHECK(x)                                                        \
	do {                                                            \
		if (!(x)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
			return 1;                                       \
		}                                                       \
	} while (0)

static bool
same(const struct st_rip3_button_style *a, const struct st_rip3_button_style *b)
{
	return a->width == b->width && a->height == b->height && a->flags == b->flags && a->bevel == b->bevel
	       && a->orientation == b->orientation && a->group == b->group && a->flags2 == b->flags2
	       && memcmp(a->colors, b->colors, sizeof(a->colors)) == 0;
}

static int
entry_copies(void)
{
	struct st_rip3_button_styles    *styles = NULL;
	struct st_rip3_button_style      source =
					     {
						 .width       = 23,
						 .height      = 13,
						 .flags       = 768 | 1024,
						 .bevel       = 9,
						 .orientation = 4,
						 .group       = 35,
						 .flags2      = 165,
						 .colors      = {1, 2, 3, 4, 5, 6, 7}
        },
					 actual;
	struct st_rip3_button_style_info info;
	unsigned                         current;
	const unsigned                   destinations[] = {0, 6, 35, 35};
	CHECK(st_rip3_button_styles_create(&styles) == 0);
	CHECK(st_rip3_button_styles_set(styles, 5, &source) == 0);
	CHECK(st_rip3_button_styles_protect(styles, 5, true) == 0);
	CHECK(st_rip3_button_styles_select(styles, 6) == 0);
	CHECK(st_rip3_button_styles_copy_entries(styles, 5, destinations, 4) == 0);
	CHECK(st_rip3_button_styles_current(styles, &current) == 0 && current == 6);
	for (size_t i = 0; i < 4; i++) {
		CHECK(st_rip3_button_styles_get(styles, destinations[i], &actual) == 0 && same(&actual, &source));
		CHECK(st_rip3_button_styles_info(styles, destinations[i], &info) == 0 && info.defined
		      && !info.protected);
	}
	const unsigned protected[] = {7, 5, 8}, self[] = {7, 6, 8}, range[] = {7, 36, 8};
	CHECK(st_rip3_button_styles_copy_entries(styles, 6, protected, 3) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_button_styles_copy_entries(styles, 6, self, 3) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_button_styles_copy_entries(styles, 5, range, 3) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_button_styles_copy_entries(styles, 34, destinations, 4) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_button_styles_copy_entries(styles, 36, destinations, 4) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_button_styles_copy_entries(styles, 5, NULL, 1) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_button_styles_copy_entries(styles, 5, destinations, 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_button_styles_copy_entries(NULL, 5, destinations, 4) == IO_ERROR_INVALID_ARGUMENT);
	for (unsigned i = 7; i <= 8; i++) {
		CHECK(st_rip3_button_styles_info(styles, i, &info) == 0 && !info.defined);
	}
	actual       = source;
	actual.width = 17;
	CHECK(st_rip3_button_styles_set(styles, 6, &actual) == 0);
	CHECK(st_rip3_button_styles_get(styles, 5, &actual) == 0 && same(&actual, &source));
	CHECK(st_rip3_button_styles_info(styles, 5, &info) == 0 && info.protected);
	/* The shipped CUR-reset defect can hide a protected destination. COPY
	 * checks that raw protection rather than implicitly selecting it. */
	CHECK(st_rip3_button_styles_set(styles, 7, &source) == 0);
	CHECK(st_rip3_button_styles_protect(styles, 7, true) == 0);
	CHECK(st_rip3_button_styles_reset_current(styles) == 0);
	CHECK(st_rip3_button_styles_info(styles, 7, &info) == 0 && !info.defined && info.protected);
	CHECK(st_rip3_button_styles_copy_entries(styles, 5, protected, 1) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_button_styles_info(styles, 7, &info) == 0 && !info.defined && info.protected);
	CHECK(st_rip3_button_styles_select(styles, 9) == 0);
	CHECK(st_rip3_button_styles_reset_entry(styles, 9) == 0);
	const unsigned destination = 10;
	CHECK(st_rip3_button_styles_copy_entries(styles, 9, &destination, 1) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_button_styles_current(styles, &current) == 0 && current == 9);
	CHECK(st_rip3_button_styles_info(styles, 9, &info) == 0 && !info.defined);
	CHECK(st_rip3_button_styles_info(styles, 10, &info) == 0 && !info.defined);
	st_rip3_button_styles_destroy(styles);
	return 0;
}

int
main(void)
{
	CHECK(entry_copies() == 0);
	struct st_rip3_button_styles *styles = NULL;
	struct st_rip3_backup        *backup = NULL;
	CHECK(st_rip3_button_styles_create(&styles) == 0 && st_rip3_button_styles_backup_create(&backup) == 0);
	const struct st_rip3_button_style boot = {
	    .orientation = 2,
	    .flags       = 768,
	    .bevel       = 3,
	    .colors      = {15, 0, 15, 8, 7, 15, 7}
        };
	struct st_rip3_button_style value, actual;
	CHECK(st_rip3_button_style_defaults(&value) == 0 && same(&value, &boot));
	for (unsigned i = 0; i < 36; i++) {
		struct st_rip3_button_style_info info;
		CHECK(st_rip3_button_styles_info(styles, i, &info) == 0 && info.defined == (i == 0)
		      && !info.protected);
		CHECK(st_rip3_button_styles_select(styles, i) == 0);
		CHECK(st_rip3_button_styles_get(styles, i, &actual) == 0 && same(&actual, &boot));
		value        = boot;
		value.width  = (uint16_t)(i + 10);
		value.height = (uint16_t)(i + 20);
		value.group  = i;
		CHECK(st_rip3_button_styles_set(styles, i, &value) == 0);
	}
	/* Definition accepts all type combinations/inapplicable effects, and only
	 * normalizes bevel absence. No-type/orientation/group failures retain data. */
	for (unsigned flags = 0; flags <= 65535; flags++) {
		value       = boot;
		value.flags = (uint16_t)flags;
		int status  = st_rip3_button_styles_set(styles, 5, &value);
		CHECK(status == ((flags & 0x181) ? 0 : IO_ERROR_OUT_OF_RANGE));
		if (status == 0) {
			CHECK(st_rip3_button_styles_get(styles, 5, &actual) == 0);
			CHECK(actual.flags == flags && actual.bevel == ((flags & 512) ? 3 : 0));
		}
	}
	for (unsigned flags = 0; flags < 256; flags++) {
		value        = boot;
		value.flags  = 16640;
		value.flags2 = (uint8_t)flags;
		CHECK(st_rip3_button_styles_set(styles, 5, &value) == 0);
		CHECK(st_rip3_button_styles_get(styles, 5, &actual) == 0 && actual.flags == 16640
		      && actual.flags2 == flags);
	}
	value             = boot;
	value.orientation = 5;
	CHECK(st_rip3_button_styles_set(styles, 5, &value) == IO_ERROR_OUT_OF_RANGE);
	value       = boot;
	value.group = 36;
	CHECK(st_rip3_button_styles_set(styles, 5, &value) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_button_styles_protect(styles, 0, true) == IO_ERROR_OUT_OF_RANGE);
	/* A numeric reset differs from the shipped CUR reset. First selection
	 * defaults the undefined record, not the style of its previous neighbor. */
	CHECK(st_rip3_button_styles_select(styles, 7) == 0);
	CHECK(st_rip3_button_styles_reset_entry(styles, 7) == 0);
	unsigned current;
	CHECK(st_rip3_button_styles_current(styles, &current) == 0 && current == 7);
	CHECK(st_rip3_button_styles_get(styles, 7, &actual) == IO_ERROR_NOT_FOUND);
	CHECK(st_rip3_button_styles_set(styles, 7, &boot) == 0);
	CHECK(st_rip3_button_styles_select(styles, 7) == 0);
	CHECK(st_rip3_button_styles_get(styles, 7, &actual) == 0 && same(&actual, &boot));
	CHECK(st_rip3_button_styles_protect(styles, 7, true) == 0);
	CHECK(st_rip3_button_styles_reset_current(styles) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_button_styles_reset_entry(styles, 7) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_button_styles_set(styles, 7, &boot) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_backup_save(backup, ST_RIP3_BACKUP_BASE, 0, styles) == 0);
	CHECK(st_rip3_button_styles_reset_table(styles) == 0);
	CHECK(st_rip3_button_styles_current(styles, &current) == 0 && current == 7);
	struct st_rip3_button_style_info protected_info;
	CHECK(st_rip3_button_styles_info(styles, 7, &protected_info) == 0 && protected_info.defined
	      && protected_info.protected);
	CHECK(st_rip3_button_styles_select(styles, 3) == 0);
	CHECK(st_rip3_backup_restore(backup, ST_RIP3_BACKUP_BASE, 0, styles) == 0);
	CHECK(st_rip3_button_styles_current(styles, &current) == 0 && current == 7);
	CHECK(st_rip3_button_styles_get(styles, 3, &actual) == 0 && actual.width == 13);
	CHECK(st_rip3_button_styles_select(styles, 4) == 0 && st_rip3_button_styles_reset_current(styles) == 0);
	CHECK(st_rip3_button_styles_current(styles, &current) == 0 && current == 0);
	CHECK(st_rip3_button_styles_get(styles, 0, &actual) == 0 && actual.width == 10);
	/* Default-record bytes 2..27 overwrite in-use[0..25], with zero forced
	 * in use. Higher entries and all protection/record values are retained. */
	for (unsigned i = 0; i < 36; i++) {
		bool used = i == 0 || i == 2 || i == 5 || i == 6 || i == 8 || i == 12 || i == 14 || i == 16
		            || i == 22 || i == 24 || i >= 26;
		struct st_rip3_button_style_info info;
		CHECK(st_rip3_button_styles_info(styles, i, &info) == 0 && info.defined == used
		      && info.protected == (i == 7));
		if (used && i != 0 && i != 5) {
			CHECK(st_rip3_button_styles_get(styles, i, &actual) == 0 && actual.width == i + 10);
		}
	}
	/* PR114: selecting a protected record hidden by CUR reset defaults it
	 * and clears stale protection. A nonzero in-use byte retains protection. */
	CHECK(st_rip3_button_styles_info(styles, 7, &protected_info) == 0 && !protected_info.defined
	      && protected_info.protected);
	CHECK(st_rip3_button_styles_set(styles, 7, &boot) == IO_ERROR_PERMISSION_DENIED);
	CHECK(st_rip3_button_styles_select(styles, 7) == 0);
	CHECK(st_rip3_button_styles_info(styles, 7, &protected_info) == 0 && protected_info.defined
	      && !protected_info.protected);
	CHECK(st_rip3_button_styles_get(styles, 7, &actual) == 0 && same(&actual, &boot));
	CHECK(st_rip3_button_styles_reset_entry(styles, 7) == 0);
	value       = boot;
	value.flags = 0;
	CHECK(st_rip3_button_styles_set(styles, 7, &value) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_button_styles_info(styles, 7, &protected_info) == 0 && !protected_info.defined);
	value       = boot;
	value.width = 77;
	CHECK(st_rip3_button_styles_set(styles, 7, &value) == 0 && st_rip3_button_styles_select(styles, 7) == 0);
	CHECK(st_rip3_button_styles_get(styles, 7, &actual) == 0 && actual.width == 77);
	st_rip3_backup_destroy(backup);
	st_rip3_button_styles_destroy(styles);
	return 0;
}
