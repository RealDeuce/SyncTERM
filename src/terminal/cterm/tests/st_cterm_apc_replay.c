/* Replay an unchanged APC test stream and export observable terminal state. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "cterm.h"
#include "cterm_apc.h"
#include "st_pixels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

static FILE *wire;
static void
response(void *context, enum st_cterm_output_origin origin, const uint8_t *bytes, size_t size)
{
	(void)context;
	CHECK(origin == ST_CTERM_OUTPUT_RESPONSE);
	CHECK(fwrite(bytes, 1, size, wire) == size);
}

static FILE *
output_file(const char *prefix, const char *suffix)
{
	char path[4096];
	int count = snprintf(path, sizeof(path), "%s%s", prefix, suffix);
	FILE *stream;
	CHECK(count >= 0 && (size_t)count < sizeof(path));
	stream = fopen(path, "wb");
	CHECK(stream != NULL);
	return stream;
}

static void
rgb_image(const char *prefix, const char *suffix,
	const struct st_pixel_buffer *pixels, const uint32_t *palette)
{
	FILE *stream = output_file(prefix, suffix);
	for (size_t i = 0; i < (size_t)pixels->width * pixels->height; ++i) {
		uint32_t value = pixels->primary[i];
		uint8_t rgb[3];
		if (!ST_PIXEL_COLOR_IS_RGB(value)) { CHECK(value < 65536); value = palette[value]; }
		rgb[0] = (uint8_t)(value >> 16); rgb[1] = (uint8_t)(value >> 8); rgb[2] = (uint8_t)value;
		CHECK(fwrite(rgb, 1, 3, stream) == 3);
	}
	CHECK(fclose(stream) == 0);
}

int
main(int argc, char **argv)
{
	const struct st_console_display_config display = {.backend = ST_CONSOLE_BACKEND_HEADLESS};
	const struct st_console_config geometry = {
		.columns = 80, .rows = 25, .cell_width = 8, .cell_height = 14,
		.pixel_width = 640, .pixel_height = 350,
	};
	st_console_t *console = NULL;
	st_font_catalog_t *fonts = NULL;
	st_cterm_t *terminal = NULL;
	struct st_pixel_buffer *frame = NULL;
	const struct st_console_rect full = {0,0,640,350};
	uint32_t palette[65536];
	uint8_t bytes[4096];
	size_t chunk = sizeof(bytes);
	FILE *input, *metadata;

	CHECK(argc == 5 || argc == 6); /* directory, version, stream, output prefix, optional chunk */
	if (argc == 6) { chunk = (size_t)strtoul(argv[5], NULL, 10); CHECK(chunk > 0 && chunk <= sizeof(bytes)); }
	CHECK(st_console_create(&console, &display, &geometry) == 0);
	CHECK(st_font_catalog_create(&fonts, console) == 0);
	wire = output_file(argv[4], ".wire");
	const struct st_cterm_config config = {
		.console = console, .fonts = fonts,
		.emulation = ST_CTERM_ANSI_BBS, .encoding = ST_CTERM_ENCODING_CODEPAGE,
		.codepage = ST_CODEPAGE_CP437, .apc_directory = argv[1],
		.application_version = argv[2], .output = response, .quiet = true,
	};
	CHECK(st_cterm_create(&terminal, &config) == 0);
	input = fopen(argv[3], "rb"); CHECK(input != NULL);
	for (;;) {
		size_t count = fread(bytes, 1, chunk, input), consumed;
		if (count == 0) break;
		CHECK(st_cterm_write(terminal, bytes, count, &consumed) == 0 && consumed == count);
	}
	CHECK(ferror(input) == 0 && fclose(input) == 0);
	/* The reference connection is still open at capture, so do not finish. */
	CHECK(st_console_get_pixels(&frame, console, &full, false) == 0);
	CHECK(st_console_get_palette(console, 0, palette, 65536) == 0);
	rgb_image(argv[4], ".rgb24", frame, palette);
	metadata = output_file(argv[4], ".state");
	for (unsigned i = 0; i < 2; ++i) {
		char suffix[32];
		struct st_pixel_buffer *image = terminal->apc->images[i];
		fprintf(metadata, "image%u=%u,%u\n", i, image == NULL ? 0 : image->width, image == NULL ? 0 : image->height);
		if (image != NULL) {
			(void)snprintf(suffix, sizeof(suffix), ".image%u.rgb24", i);
			rgb_image(argv[4], suffix, image, palette);
		}
	}
	struct st_pixel_mask *mask = terminal->apc->mask;
	fprintf(metadata, "mask=%u,%u\n", mask == NULL ? 0 : mask->width, mask == NULL ? 0 : mask->height);
	if (mask != NULL) {
		FILE *stream = output_file(argv[4], ".mask");
		size_t size = ((size_t)mask->width * mask->height + 7) / 8;
		CHECK(fwrite(mask->bits, 1, size, stream) == size && fclose(stream) == 0);
	}
	for (unsigned i = 0; i < 256; ++i) {
		struct cterm_apc_patch *patch = &terminal->apc->patches[i];
		fprintf(metadata, "patch%u=%zu\n", i, patch->count);
		if (patch->frames != NULL) {
			char suffix[32];
			(void)snprintf(suffix, sizeof(suffix), ".pcm%u", i);
			FILE *stream = output_file(argv[4], suffix);
			CHECK(fwrite(patch->frames, AUDIO_FRAME_SIZE, patch->count, stream) == patch->count && fclose(stream) == 0);
		}
	}
	CHECK(fclose(metadata) == 0 && fclose(wire) == 0);
	st_pixel_buffer_destroy(frame);
	st_cterm_destroy(&terminal); st_font_catalog_destroy(fonts); st_console_destroy(console);
	return 0;
}
