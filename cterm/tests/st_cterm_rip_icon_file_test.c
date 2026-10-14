/* Verify RIP .ICN bitplanes against unmodified historical icon data. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_rip_icon_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	/* Four one-pixel planes in BGI's high-to-low storage order. */
	static const uint8_t tiny[] = {0, 0, 0, 0, 0x80, 0x80, 0, 0};
	uint8_t *pixels = NULL, *encoded = NULL, *copy = NULL;
	uint32_t width = 0, height = 0;
	size_t encoded_size;

	CHECK(cterm_rip_icon_decode(&pixels, &width, &height,
	    tiny, sizeof(tiny)) == 0);
	CHECK(width == 1 && height == 1 && pixels[0] == 12);
	free(pixels);
	pixels = NULL;
	CHECK(cterm_rip_icon_decode(&pixels, &width, &height,
	    tiny, sizeof(tiny) - 1) != 0);
	CHECK(pixels == NULL);
	CHECK(cterm_rip_icon_encode(&encoded, &encoded_size,
	    tiny, 640, 350) != 0);
	CHECK(encoded == NULL);
	{
		uint8_t wide[800] = {0};

		wide[799] = 15;
		CHECK(cterm_rip_icon_encode(&encoded, &encoded_size,
		    wide, 800, 1) == 0);
		CHECK(cterm_rip_icon_decode(&pixels, &width, &height,
		    encoded, encoded_size) == 0);
		CHECK(width == 800 && height == 1
		    && memcmp(pixels, wide, sizeof(wide)) == 0);
		free(pixels);
		pixels = NULL;
		free(encoded);
		encoded = NULL;
	}
#ifdef RIP_ICON_PATH
	{
		uint8_t source[4096];
		size_t size;
		FILE *stream = fopen(RIP_ICON_PATH, "rb");

		CHECK(stream != NULL);
		size = fread(source, 1, sizeof(source), stream);
		CHECK(ferror(stream) == 0 && feof(stream) != 0);
		CHECK(fclose(stream) == 0);
		CHECK(cterm_rip_icon_decode(&pixels, &width, &height,
		    source, size) == 0);
		CHECK(width == 51 && height == 40);
		CHECK(cterm_rip_icon_encode(&encoded, &encoded_size,
		    pixels, width, height) == 0);
		CHECK(encoded_size == 1125);
		CHECK(memcmp(source, encoded, encoded_size - 1) == 0);
		CHECK(encoded[encoded_size - 1] == 'F');
		CHECK(cterm_rip_icon_decode(&copy, &width, &height,
		    encoded, encoded_size) == 0);
		CHECK(memcmp(copy, pixels, (size_t)width * height) == 0);
		free(copy);
		free(encoded);
		free(pixels);
	}
#endif
	return 0;
}
