/* Independent synthetic disk images and pinned native font metric expectations. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "filesystem.h"
#include "io_error.h"
#include "path.h"
#include "st_amiga_font.h"
#include "st_amiga_fonts.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e)                                                                                   \
	do {                                                                                           \
		if (!(e)) {                                                                                \
			fprintf(stderr, "%d: %s\n", __LINE__, #e);                                             \
			return EXIT_FAILURE;                                                                   \
		}                                                                                          \
	} while (false)
static void be16(uint8_t *p, unsigned x)
{
	p[0] = (uint8_t)(x >> 8);
	p[1] = (uint8_t)x;
}
static void be32(uint8_t *p, unsigned x)
{
	be16(p, x >> 16);
	be16(p + 2, x);
}
static uint64_t hash_word(uint64_t hash, int value)
{
	uint16_t word = (uint16_t)value;
	hash = (hash ^ (word >> 8)) * UINT64_C(1099511628211);
	return (hash ^ (word & 255)) * UINT64_C(1099511628211);
}
static bool ink(const struct st_amiga_glyph *g, unsigned x, unsigned y)
{
	unsigned bit = g->bit_offset + x;
	return (g->bits[(size_t)y * g->stride + bit / 8] & (128u >> (bit % 8))) != 0;
}
static void builtin_inventory(enum st_amiga_font_set set)
{
	for (size_t i = 0; i < st_amiga_font_builtin_count(set); ++i) {
		unsigned height;
		const char *name = st_amiga_font_builtin_name(set, i, &height);
		const st_amiga_font_t *f = st_amiga_font_builtin(set, name, height);
		uint64_t hash = UINT64_C(14695981039346656037);
		hash = hash_word(hash, (int)height);
		hash = hash_word(hash, (int)st_amiga_font_baseline(f));
		hash = hash_word(hash, (int)st_amiga_font_width(f));
		for (unsigned c = 0; c < 256; ++c) {
			const struct st_amiga_glyph *g = st_amiga_font_glyph(f, (uint8_t)c);
			int count = 0;
			for (unsigned y = 0; y < g->height; ++y)
				for (unsigned x = 0; x < g->width; ++x)
					if (ink(g, x, y)) ++count;
			hash = hash_word(hash, g->advance);
			hash = hash_word(hash, count);
			for (unsigned y = 0; y < g->height; ++y)
				for (unsigned x = 0; x < g->width; ++x) if (ink(g, x, y)) {
					hash = hash_word(hash, g->bearing + (int)x);
					hash = hash_word(hash, g->top + (int)y);
				}
		}
		printf("%s\t%u\t%u\t%u\t%016" PRIx64 "\n", name, height,
			st_amiga_font_width(f), st_amiga_font_baseline(f), hash);
	}
}
int main(int argc, char **argv)
{
	if (argc == 2 && (strcmp(argv[1], "--builtins") == 0 || strcmp(argv[1], "--builtins-13") == 0)) {
		builtin_inventory(strcmp(argv[1], "--builtins-13") == 0 ? ST_AMIGA_FONT_SET_13 : ST_AMIGA_FONT_SET_2PLUS);
		return 0;
	}
	if (argc > 1) {
		for (int i = 1; i < argc; ++i) {
			st_amiga_font_t *selected = NULL;
			int status = st_amiga_font_read(&selected, argv[i]);
			if (status != 0) {
				fprintf(stderr, "%s: %d\n", argv[i], status);
				return 1;
			}
			printf("%s: height=%u baseline=%u\n", argv[i], st_amiga_font_height(selected),
				   st_amiga_font_baseline(selected));
			st_amiga_font_release(selected);
		}
		return 0;
	}

	uint8_t image[256] = {0};
	/* HUNK_HEADER, one 55-long CODE hunk, then END. Font lives at hunk+8. */
	be32(image, 1011);
	be32(image + 8, 1);
	be32(image + 20, 55);
	be32(image + 24, 1001);
	be32(image + 28, 55);
	be32(image + 252, 1010);
	uint8_t *h = image + 32;
	be16(h + 18, 0x0f80);
	be16(h + 78, 3);
	be16(h + 82, 8);
	be16(h + 84, 2);
	h[81] = 0x20;
	h[90] = 'A';
	h[91] = 'A';
	be32(h + 92, 180);
	be16(h + 96, 2);
	be32(h + 98, 188);
	be32(h + 102, 196);
	be32(h + 106, 200);
	/* A is 9 bits wide, bearing -2, space 5; default is 1 bit, advance 8. */
	h[180] = 0x80;
	h[181] = 0x80;
	h[182] = 0xff;
	h[183] = 0x80;
	be16(h + 188, 0);
	be16(h + 190, 9);
	be16(h + 192, 15);
	be16(h + 194, 1);
	be16(h + 196, 5);
	be16(h + 198, 8);
	be16(h + 200, 65534);
	st_amiga_font_t *font = NULL;
	CHECK(st_amiga_font_decode(&font, image, sizeof(image)) == 0);
	CHECK(st_amiga_font_width(font) == 8 && st_amiga_font_height(font) == 3 &&
		  st_amiga_font_baseline(font) == 2);
	const struct st_amiga_glyph *g = st_amiga_font_glyph(font, 'A');
	CHECK(g->width == 9 && g->height == 3 && g->bearing == -2 && g->advance == 3);
	CHECK(g->stride == 2 && g->bits[0] == 0x80 && g->bits[1] == 0x80);
	CHECK(st_amiga_font_glyph(font, 'Z')->advance == 8);
	st_amiga_font_release(font);
	h[81] = 0;
	CHECK(st_amiga_font_decode(&font, image, sizeof(image)) == 0);
	CHECK(st_amiga_font_glyph(font, 'A')->bearing == -1 &&
		  st_amiga_font_glyph(font, 'A')->advance == 8);
	st_amiga_font_release(font);
	h[81] = 0x20;
	/* A relocated offset zero is a valid pointer into a second data hunk. */
	uint8_t relocated[300] = {0};
	be32(relocated, 1011);
	be32(relocated + 8, 2);
	be32(relocated + 16, 1);
	be32(relocated + 20, 55);
	be32(relocated + 24, 2);
	be32(relocated + 28, 1001);
	be32(relocated + 32, 55);
	memcpy(relocated + 36, h, 220);
	be32(relocated + 36 + 92, 0);
	be32(relocated + 256, 1004);
	be32(relocated + 260, 1);
	be32(relocated + 264, 1);
	be32(relocated + 268, 92);
	be32(relocated + 276, 1010);
	be32(relocated + 280, 1002);
	be32(relocated + 284, 2);
	memcpy(relocated + 288, h + 180, 6);
	be32(relocated + 296, 1010);
	CHECK(st_amiga_font_decode(&font, relocated, sizeof(relocated)) == 0);
	CHECK(st_amiga_font_glyph(font, 'A')->bits[0] == 0x80);
	st_amiga_font_release(font);
	be32(relocated + 264, 2);
	CHECK(st_amiga_font_decode(&font, relocated, sizeof(relocated)) == IO_ERROR_INVALID_FORMAT);
	h[80] = 0x40;
	CHECK(st_amiga_font_decode(&font, image, sizeof(image)) == IO_ERROR_NOT_SUPPORTED);
	h[80] = 0;
	for (size_t n = 0; n < 32; ++n)
		CHECK(st_amiga_font_decode(&font, image, n) == IO_ERROR_INVALID_FORMAT);
	be32(h + 92, UINT32_MAX);
	CHECK(st_amiga_font_decode(&font, image, sizeof(image)) == IO_ERROR_INVALID_FORMAT);
	uint8_t descriptor[264] = {0};
	be16(descriptor, 0x0f00);
	be16(descriptor + 2, 1);
	memcpy(descriptor + 4, "Test/3", 7);
	be16(descriptor + 260, 3);
	struct st_amiga_family family = {0};
	CHECK(st_amiga_family_decode(&family, "Test.font", descriptor, sizeof(descriptor)) == 0);
	CHECK(family.count == 1 && family.strikes[0].height == 3 &&
		  strcmp(family.strikes[0].path, "Test/3") == 0);
	st_amiga_family_dispose(&family);
	memcpy(descriptor + 4, "../bad", 7);
	CHECK(st_amiga_family_decode(&family, "Test.font", descriptor, sizeof(descriptor)) ==
		  IO_ERROR_INVALID_FORMAT);
	/* Atomic family lifecycle, including selected-data ownership and recovery. */
	char *descriptor_path = NULL, *strike_path = NULL, *root = NULL, *live = NULL, *backup = NULL;
	CHECK(path_join_alloc(&descriptor_path, ST_AMIGA_TEST_DIRECTORY, "Test.font") == 0);
	CHECK(path_join_alloc(&strike_path, ST_AMIGA_TEST_DIRECTORY, "Test/3") == 0);
	CHECK(path_join_alloc(&root, ST_AMIGA_TEST_DIRECTORY, "fonts/amiga") == 0);
	CHECK(fs_create_parent_directories(strike_path) == 0);
	be32(h + 92, 180);
	memset(descriptor + 4, 0, 256);
	memcpy(descriptor + 4, "Test/3", 7);
	CHECK(fs_replace_file(descriptor_path, descriptor, sizeof(descriptor)) == 0);
	CHECK(fs_replace_file(strike_path, image, sizeof(image)) == 0);
	(void)st_amiga_fonts_remove(root, "Test.font");
	CHECK(st_amiga_fonts_install(root, descriptor_path, false, NULL) == 0);
	CHECK(st_amiga_fonts_install(root, descriptor_path, false, NULL) == IO_ERROR_ALREADY_EXISTS);
	const st_amiga_font_t *selected = NULL;
	CHECK(st_amiga_fonts_lookup(&selected, ST_AMIGA_FONT_SET_2PLUS, root, "TEST.FONT", 3) == 0);
	CHECK(st_amiga_font_glyph(selected, 'A')->bearing == -2);
	struct st_amiga_fonts_catalog catalog = {0};
	CHECK(st_amiga_fonts_catalog_read(&catalog, root) == 0 && catalog.count == 12);
	st_amiga_fonts_catalog_dispose(&catalog);
	CHECK(fs_replace_file(strike_path, "bad", 3) == 0);
	CHECK(st_amiga_fonts_install(root, descriptor_path, true, NULL) == IO_ERROR_INVALID_FORMAT);
	const st_amiga_font_t *unchanged = NULL;
	CHECK(st_amiga_fonts_lookup(&unchanged, ST_AMIGA_FONT_SET_2PLUS, root, "Test.font", 3) == 0);
	st_amiga_font_release(unchanged);
	CHECK(st_amiga_fonts_remove(root, "Test.font") == 0);
	CHECK(st_amiga_font_glyph(selected, 'A')->bearing == -2);
	st_amiga_font_release(selected);
	CHECK(st_amiga_fonts_lookup(&selected, ST_AMIGA_FONT_SET_2PLUS, root, "Test.font", 3) == IO_ERROR_NOT_FOUND);
	CHECK(selected == st_amiga_font_builtin(ST_AMIGA_FONT_SET_2PLUS, "pearl.font", 8));
	CHECK(fs_replace_file(strike_path, image, sizeof(image)) == 0);
	CHECK(st_amiga_fonts_install(root, descriptor_path, false, NULL) == 0);
	CHECK(path_join_alloc(&live, root, "test.font") == 0);
	CHECK(path_join_alloc(&backup, root, "test.font.backup") == 0);
	CHECK(fs_rename(live, backup) == 0);
	CHECK(st_amiga_fonts_catalog_read(&catalog, root) == 0 && catalog.count == 12);
	st_amiga_fonts_catalog_dispose(&catalog);
	CHECK(st_amiga_fonts_remove(root, "Test.font") == 0);
	free(descriptor_path);
	free(strike_path);
	free(root);
	free(live);
	free(backup);
	CHECK(st_amiga_font_builtin_count(ST_AMIGA_FONT_SET_2PLUS) == 35);
	const st_amiga_font_t *sapphire = st_amiga_font_builtin(ST_AMIGA_FONT_SET_2PLUS, "sApPhIrE.FoNt", 19);
	CHECK(sapphire != NULL && st_amiga_font_builtin(ST_AMIGA_FONT_SET_2PLUS, "Sapphire.font", 20) == NULL);
	CHECK(st_amiga_font_glyph(sapphire, 'R')->advance == 16);
	CHECK(st_amiga_font_glyph(sapphire, 'e')->advance == 9);
	CHECK(st_amiga_font_glyph(sapphire, ' ')->advance == 13);
	const st_amiga_font_t *courier = st_amiga_font_builtin(ST_AMIGA_FONT_SET_2PLUS, "Courier.font", 36);
	CHECK(courier != NULL && st_amiga_font_glyph(courier, 'M')->width > 8);
	CHECK(st_amiga_font_glyph(courier, 'M')->advance == 22);
	const st_amiga_font_t *times = st_amiga_font_builtin(ST_AMIGA_FONT_SET_2PLUS, "Times.font", 15);
	CHECK(times != NULL && st_amiga_font_width(times) == 7);
	CHECK(st_amiga_font_height(times) == 15 && st_amiga_font_baseline(times) == 10);
	const st_amiga_font_t *old_diamond = st_amiga_font_builtin(ST_AMIGA_FONT_SET_13, "diamond.font", 20);
	const st_amiga_font_t *new_diamond = st_amiga_font_builtin(ST_AMIGA_FONT_SET_2PLUS, "diamond.font", 20);
	CHECK(old_diamond && new_diamond && old_diamond != new_diamond);
	CHECK(st_amiga_font_width(old_diamond) == 20 && st_amiga_font_baseline(old_diamond) == 14);
	CHECK(st_amiga_font_width(new_diamond) == 12 && st_amiga_font_baseline(new_diamond) == 13);
	CHECK(st_amiga_font_glyph(st_amiga_font_builtin(ST_AMIGA_FONT_SET_2PLUS, "courier.font", 11), '!')->advance == 6);
	CHECK(st_amiga_font_builtin(ST_AMIGA_FONT_SET_13, "sapphire.font", 15) == NULL);
	CHECK(st_amiga_font_builtin(ST_AMIGA_FONT_SET_2PLUS, "topaz.font", 8) == NULL);
	st_amiga_font_release(old_diamond);
	CHECK(st_amiga_font_builtin(ST_AMIGA_FONT_SET_13, "diamond.font", 20) == old_diamond);

	return EXIT_SUCCESS;
}
