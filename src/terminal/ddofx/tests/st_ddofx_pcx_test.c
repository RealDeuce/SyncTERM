/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_graphics_internal.h"

static bool
check_file(FILE *file)
{
	if (!file || fseek(file, 0, SEEK_SET) != 0)
		return false;
	uint8_t header[128];
	if (fread(header, 1, sizeof(header), file) != sizeof(header)
	    || memcmp(header, "\x0a\x05\x01\x08\0\0\0\0\x7f\x02\xdf\x01", 12)
	    || memcmp(header + 65, "\x01\x80\x02", 3))
		return false;
	/* The first two values are singletons. The remaining 638 pixels
	 * split into ten maximum-length runs and a final eight. */
	if (fgetc(file) != 7 || fgetc(file) != 0xc1 || fgetc(file) != 0xfa)
		return false;
	for (unsigned y = 0; y < HEIGHT; ++y) {
		for (unsigned i = 0; i < 10; ++i)
			if (fgetc(file) != 0xff || fgetc(file) != 0)
				return false;
		if (fgetc(file) != (y == 0 ? 0xc8 : 0xca) || fgetc(file) != 0)
			return false;
	}
	if (fgetc(file) != 12)
		return false;
	uint8_t palette[768] = {0}, actual[768];
	palette[21]          = 194;
	palette[22]          = 149;
	palette[23]          = 60;
	return fread(actual, 1, sizeof(actual), file) == sizeof(actual) && !memcmp(actual, palette, sizeof(actual))
	       && fgetc(file) == EOF && !ferror(file);
}

int
main(void)
{
	struct st_ddofx ddo = {.graphics = calloc(1, sizeof(*ddo.graphics))};
	if (!ddo.graphics)
		return EXIT_FAILURE;
	ddo.graphics->page[WORK][0][0] = 7;
	ddo.graphics->page[WORK][0][1] = 250;
	ddo.graphics->palette[7]       = 0xc3953c;
	st_ddofx_page_file(&ddo, WORK, WORK, true);
	bool ok = check_file(ddo.page_files[0]);
	st_ddofx_reset(&ddo);
	if (!ok)
		fputs("Unexpected work-page PCX header, raster or palette.\n", stderr);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
