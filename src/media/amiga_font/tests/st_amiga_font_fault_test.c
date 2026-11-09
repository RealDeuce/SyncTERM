/* Exercise the production transaction with failures at its filesystem boundary. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#define fs_copy_file fault_copy_file
#define fs_rename fault_rename
#define fs_remove_directory fault_remove_directory
#include "../st_amiga_fonts.c"
#undef fs_copy_file
#undef fs_rename
#undef fs_remove_directory
int fs_copy_file(const char *, const char *);
int fs_rename(const char *, const char *);
int fs_remove_directory(const char *);
static unsigned rename_count, fail_first, fail_second;
static const char *cleanup_suffix;
static bool corrupt_copy;
int fault_copy_file(const char *from, const char *to)
{
	int status = fs_copy_file(from, to);
	/* Model a source edit between validation and copying. Only bytes in the
	 * staged package can prove what would actually be published. */
	if (status == 0 && corrupt_copy)
		status = fs_replace_file(to, "bad", 3);
	return status;
}
int fault_rename(const char *from, const char *to)
{
	++rename_count;
	if (rename_count == fail_first || rename_count == fail_second)
		return IO_ERROR_NO_SPACE;
	return fs_rename(from, to);
}
int fault_remove_directory(const char *path)
{
	if (cleanup_suffix != NULL) {
		size_t a = strlen(path), b = strlen(cleanup_suffix);
		if (a >= b && strcmp(path + a - b, cleanup_suffix) == 0)
			return IO_ERROR_PERMISSION_DENIED;
	}
	return fs_remove_directory(path);
}
#define CHECK(e)                                                                                   \
	do {                                                                                           \
		if (!(e)) {                                                                                \
			fprintf(stderr, "%d: %s\n", __LINE__, #e);                                             \
			return 1;                                                                              \
		}                                                                                          \
	} while (false)
static void be16(uint8_t *p, unsigned n)
{
	p[0] = (uint8_t)(n >> 8);
	p[1] = (uint8_t)n;
}
static void be32(uint8_t *p, unsigned n)
{
	be16(p, n >> 16);
	be16(p + 2, n);
}
int main(void)
{
	uint8_t font[256] = {0}, descriptor[264] = {0};
	be32(font, 1011);
	be32(font + 8, 1);
	be32(font + 20, 55);
	be32(font + 24, 1001);
	be32(font + 28, 55);
	be32(font + 252, 1010);
	uint8_t *h = font + 32;
	be16(h + 18, 0x0f80);
	be16(h + 78, 3);
	be16(h + 82, 8);
	be16(h + 84, 2);
	h[90] = 'A';
	h[91] = 'A';
	be32(h + 92, 180);
	be16(h + 96, 2);
	be32(h + 98, 188);
	be16(h + 190, 8);
	be16(h + 192, 8);
	be16(h + 194, 8);
	be16(descriptor, 0x0f00);
	be16(descriptor + 2, 1);
	memcpy(descriptor + 4, "Fault/3", 8);
	be16(descriptor + 260, 3);
	const char *root = ST_AMIGA_TEST_DIRECTORY "/fonts/amiga";
	const char *file = ST_AMIGA_TEST_DIRECTORY "/Fault.font";
	const char *strike = ST_AMIGA_TEST_DIRECTORY "/Fault/3";
	CHECK(fs_create_parent_directories(strike) == 0);
	CHECK(fs_replace_file(strike, font, sizeof(font)) == 0 &&
		  fs_replace_file(file, descriptor, sizeof(descriptor)) == 0);
	(void)st_amiga_fonts_remove(root, "Fault.font");
	CHECK(st_amiga_fonts_install(root, file, false, NULL) == 0);
	corrupt_copy = true;
	CHECK(st_amiga_fonts_install(root, file, true, NULL) == IO_ERROR_INVALID_FORMAT);
	corrupt_copy = false;
	const st_amiga_font_t *retained = NULL;
	CHECK(st_amiga_fonts_lookup(&retained, ST_AMIGA_FONT_SET_2PLUS, root, "Fault.font", 3) == 0);
	CHECK(st_amiga_font_height(retained) == 3);
	st_amiga_font_release(retained);
	for (unsigned failure = 1; failure <= 3; ++failure) {
		rename_count = 0;
		fail_first = failure == 1 ? 1 : 2;
		fail_second = failure == 3 ? 3 : 0;
		CHECK(st_amiga_fonts_install(root, file, true, NULL) == IO_ERROR_NO_SPACE);
		fail_first = fail_second = 0;
		const st_amiga_font_t *selected = NULL;
		CHECK(st_amiga_fonts_lookup(&selected, ST_AMIGA_FONT_SET_2PLUS, root, "Fault.font", 3) == 0);
		CHECK(st_amiga_font_height(selected) == 3);
		st_amiga_font_release(selected);
	}
	cleanup_suffix = ".backup";
	rename_count = 0;
	CHECK(st_amiga_fonts_install(root, file, true, NULL) == 0);
	cleanup_suffix = NULL;
	struct st_amiga_fonts_catalog catalog = {0};
	CHECK(st_amiga_fonts_catalog_read(&catalog, root) == 0);
	st_amiga_fonts_catalog_dispose(&catalog);
	cleanup_suffix = ".removed";
	CHECK(st_amiga_fonts_remove(root, "Fault.font") == 0);
	const st_amiga_font_t *selected = NULL;
	CHECK(st_amiga_fonts_lookup(&selected, ST_AMIGA_FONT_SET_2PLUS, root, "Fault.font", 3) == IO_ERROR_NOT_FOUND);
	cleanup_suffix = NULL;
	CHECK(st_amiga_fonts_remove(root, "Fault.font") == IO_ERROR_NOT_FOUND);
	return 0;
}
