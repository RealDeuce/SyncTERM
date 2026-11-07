/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "cterm_apc.h"
#include "cterm_apc_cache.h"
#include "cterm.h"
#include "io_error.h"
#include "st_hex.h"
#include "st_md5.h"
#include "st_pixel_image.h"
#include "string_compat.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

int
cterm_apc_decode_base64(void **result, size_t *result_size,
	const char *text, size_t size)
{
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
	uint8_t *data;
	size_t used = 0;
	uint32_t working = 0;
	unsigned bits = 0;

	if (size > SIZE_MAX - 4) return IO_ERROR_OUT_OF_RANGE;
	data = malloc(size / 4 * 3 + 4);
	if (data == NULL) return IO_ERROR_OUT_OF_MEMORY;
	for (size_t i = 0; i < size; ++i) {
		unsigned char ch = (unsigned char)text[i];
		const char *digit;

		if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\v' || ch == '\f')
			continue;
		digit = ch == 0 ? NULL : strchr(alphabet, ch);
		if (digit == NULL) { free(data); return IO_ERROR_INVALID_ENCODING; }
		working <<= 6;
		if (*digit == '=') {
			if ((working & 0xff) != 0) { free(data); return IO_ERROR_INVALID_ENCODING; }
			break;
		}
		working |= (uint32_t)(digit - alphabet);
		bits += 6;
		if (bits >= 8) {
			data[used++] = (uint8_t)(working >> (bits - 8));
			bits -= 8;
		}
	}
	*result = data;
	*result_size = used;
	return 0;
}

static void
report_version(struct cterminal *terminal)
{
	const unsigned char *next = (const unsigned char *)terminal->apc->version;
	char bytes[128];
	size_t used = 0;

	if (next == NULL) return;
	cterm_respond(terminal, "\x1b_SyncTERM:VER;", 15);
	for (; *next != 0; ++next) {
		unsigned char ch = *next;
		/* Keep the borrowed UI version intact; sanitize only this wire reply. */
		if (ch == 0xce && (next[1] == 0xb1 || next[1] == 0xb2)) {
			ch = next[1] == 0xb1 ? 'a' : 'b';
			++next;
		}
		if (!((ch >= 0x08 && ch <= 0x0d) || (ch >= 0x20 && ch <= 0x7e)))
			continue;
		bytes[used++] = (char)ch;
		if (used == sizeof(bytes)) {
			cterm_respond(terminal, bytes, used);
			used = 0;
		}
	}
	if (used != 0) cterm_respond(terminal, bytes, used);
	cterm_respond(terminal, "\x1b\\", 2);
}

static void
store_file(struct cterminal *terminal, char *text, size_t size)
{
	char *separator = strchr(text, ';');
	void *data = NULL;
	size_t decoded_size;
	FILE *stream;

	if (separator == NULL) return;
	if (cterm_apc_decode_base64(&data, &decoded_size, separator + 1,
	    size - (size_t)(separator + 1 - text)) != 0) return;
	*separator = 0;
	stream = cterm_apc_cache_open(terminal->apc->directory, text, true);
	*separator = ';';
	if (stream != NULL) {
		(void)fwrite(data, 1, decoded_size, stream);
		(void)fclose(stream);
	}
	free(data);
}

static void
list_files(struct cterminal *terminal, const char *pattern)
{
	struct cterm_apc_cache_entries entries = {0};
	uint8_t buffer[32768];

	cterm_respond(terminal, "\x1b_SyncTERM:C;L\n", 15);
	if (cterm_apc_cache_list(&entries, terminal->apc->directory, pattern) == 0) {
		for (size_t i = 0; i < entries.count; ++i) {
			const char *name = strrchr(entries.names[i], '/');
			const char *backslash = strrchr(entries.names[i], '\\');
			FILE *stream = cterm_apc_cache_open(terminal->apc->directory, entries.names[i], false);

			/* getfname() in the reference recognizes both separators. */
			if (backslash != NULL && (name == NULL || backslash > name)) name = backslash;
			name = name == NULL ? entries.names[i] : name + 1;
			cterm_respond(terminal, name, strlen(name));
			cterm_respond(terminal, "\t", 1);
			if (stream != NULL) {
				struct st_md5 digest;
				uint8_t bytes[16];
				char hex[33];
				size_t count;

				st_md5_init(&digest);
				while ((count = fread(buffer, 1, sizeof(buffer), stream)) != 0)
					st_md5_update(&digest, buffer, count);
				st_md5_finish(&digest, bytes);
				(void)fclose(stream);
				if (st_hex_encode(hex, sizeof(hex), bytes, sizeof(bytes)))
					cterm_respond(terminal, hex, 32);
			}
			cterm_respond(terminal, "\n", 1);
		}
	}
	cterm_apc_cache_entries_free(&entries);
	cterm_respond(terminal, "\x1b\\", 2);
}

enum image_type {IMAGE_PPM, IMAGE_JXL};

static struct st_pixel_buffer *
read_image(struct cterminal *terminal, enum image_type type,
	const char *text, size_t size, bool blob)
{
	void *data = NULL;
	size_t data_size;
	struct st_pixel_buffer *image = NULL;
	int status = blob ? cterm_apc_decode_base64(&data, &data_size, text, size)
	    : cterm_apc_cache_read(&data, &data_size, terminal->apc->directory, text);

	if (status == 0) {
		if (type == IMAGE_PPM) (void)st_pixel_image_decode_cterm_ppm(&image, data, data_size);
		else (void)st_pixel_image_decode_jxl(&image, data, data_size);
	}
	free(data);
	return image;
}

static struct st_pixel_mask *
read_mask(struct cterminal *terminal, const char *text, size_t size, bool blob)
{
	void *data = NULL;
	size_t data_size;
	struct st_pixel_mask *mask = NULL;
	int status = blob ? cterm_apc_decode_base64(&data, &data_size, text, size)
	    : cterm_apc_cache_read(&data, &data_size, terminal->apc->directory, text);

	if (status == 0) (void)st_pixel_image_decode_cterm_pbm(&mask, data, data_size);
	free(data);
	return mask;
}

static void
load_image(struct cterminal *terminal, char *text, size_t size,
	enum image_type type, bool blob)
{
	char *p;
	unsigned long buffer = 0;

	for (p = text; p != NULL && *p == ';'; p = strchr(p + 1, ';')) {
		if (p[1] != 'B' || p[2] != '=') break;
		buffer = strtoul(p + 3, NULL, 10);
	}
	if (buffer >= 2) return;
	st_pixel_buffer_destroy(terminal->apc->images[buffer]);
	terminal->apc->images[buffer] = NULL;
	if (p != NULL && *p == ';' && size > (size_t)(p + 1 - text))
		terminal->apc->images[buffer] = read_image(terminal, type, p + 1,
		    size - (size_t)(p + 1 - text), blob);
}

static void
load_mask(struct cterminal *terminal, char *text, size_t size, bool blob)
{
	if (text[0] != ';' || size <= 1) return;
	st_pixel_mask_destroy(terminal->apc->mask);
	terminal->apc->mask = read_mask(terminal, text + 1, size - 1, blob);
}

static void
copy_image(struct cterminal *terminal, char *text)
{
	unsigned long x = 0, y = 0, w = 0, h = 0, buffer = 0;
	uint32_t width, height;
	char *p;
	struct st_console_rect area;

	for (p = text; p != NULL && *p == ';'; p = strchr(p + 1, ';')) {
		unsigned long *value;

		switch (p[1]) {
		case 'X': value = &x; break; case 'Y': value = &y; break;
		case 'W': value = &w; break; case 'H': value = &h; break;
		case 'B': value = &buffer; break; default: return;
		}
		if (p[2] != '=') return;
		*value = (unsigned long)strtol(p + 3, NULL, 10);
	}
	if (buffer >= 2) return;
	st_pixel_buffer_destroy(terminal->apc->images[buffer]);
	terminal->apc->images[buffer] = NULL;
	if (st_console_pixel_size(terminal->screen.console, &width, &height) != 0) return;
	if (w == 0) w = width - x;
	if (h == 0) h = height - y;
	if (x > UINT32_MAX || y > UINT32_MAX || w > UINT32_MAX || h > UINT32_MAX) return;
	area = (struct st_console_rect){(uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h};
	(void)st_console_get_pixels(&terminal->apc->images[buffer], terminal->screen.console, &area, false);
}

struct image_options {
	unsigned long sx, sy, sw, sh, mx, my, mw, mh, zx, zy, buffer;
	long dx, dy;
	uint32_t flags;
	bool buffered_mask;
	struct st_pixel_mask *mask;
	size_t mask_size;
	char *mask_file;
};

static void
free_options(struct image_options *options)
{
	st_pixel_mask_destroy(options->mask);
	free(options->mask_file);
}

/* Returns the first non-option (the image source) for Draw commands.
 * Paste instead rejects it. Parsing order and duplicate options follow
 * the reference, including the differently terminated Paste mask fields. */
static char *
parse_options(char *text, struct image_options *options, bool paste)
{
	char *p;

	for (p = text; p != NULL && *p == ';'; p = strchr(p + 1, ';')) {
		unsigned long *value = NULL;
		long *signed_value = NULL;
		size_t offset = 3;

		switch (p[1]) {
		case 'B': if (paste) { value = &options->buffer; offset = 2; } break;
		case 'S':
			switch (p[2]) {
			case 'X': value = &options->sx; break; case 'Y': value = &options->sy; break;
			case 'W': value = &options->sw; break; case 'H': value = &options->sh; break;
			} break;
		case 'D':
			if (p[2] == 'X') signed_value = &options->dx;
			if (p[2] == 'Y') signed_value = &options->dy;
			break;
		case 'F':
			if (p[2] == 'X') { options->flags |= ST_PIXEL_BLIT_FLIP_X; continue; }
			if (p[2] == 'Y') { options->flags |= ST_PIXEL_BLIT_FLIP_Y; continue; }
			break;
		case 'Z':
			if (p[2] == 'X') value = &options->zx;
			if (p[2] == 'Y') value = &options->zy;
			break;
		case 'M':
			if (p[2] == 'X') value = &options->mx;
			else if (p[2] == 'Y') value = &options->my;
			else if (p[2] == 'W') value = &options->mw;
			else if (p[2] == 'H') value = &options->mh;
			else if (strncmp(p + 2, "BUF", 3) == 0) {
				st_pixel_mask_destroy(options->mask); options->mask = NULL;
				options->buffered_mask = true; continue;
			}
			else if (strncmp(p + 2, "FILE=", 5) == 0 || strncmp(p + 2, "ASK=", 4) == 0) {
				bool file = p[2] == 'F';
				char *start = p + (file ? 7 : 6);
				char *end = strchr(start, ';');
				void *bits = NULL;
				size_t count;

				if (end == NULL && paste) end = start + strlen(start);
				if (end == NULL) return NULL;
				st_pixel_mask_destroy(options->mask); options->mask = NULL;
				free(options->mask_file); options->mask_file = NULL;
				if (!paste) options->buffered_mask = false;
				if (file) {
					options->mask_file = strndup(start, (size_t)(end - start));
					if (options->mask_file == NULL) return NULL;
				}
				else {
					if (cterm_apc_decode_base64(&bits, &count, start, (size_t)(end - start)) != 0) return NULL;
					options->mask = calloc(1, sizeof(*options->mask));
					if (options->mask == NULL) { free(bits); return NULL; }
					options->mask->bits = bits;
					options->mask_size = count;
				}
				continue;
			} break;
		}
		if (value == NULL && signed_value == NULL) return paste ? NULL : p;
		if (p[offset] != '=') return paste ? NULL : p;
		if (value != NULL) *value = strtoul(p + offset + 1, NULL, 10);
		else *signed_value = strtol(p + offset + 1, NULL, 10);
	}
	return paste ? text + strlen(text) : p;
}

static void
draw_image(struct cterminal *terminal, char *text, size_t size,
	enum image_type type, bool blob, bool paste)
{
	struct image_options options = {.zx = 1, .zy = 1};
	struct st_pixel_buffer *image = NULL;
	struct st_pixel_mask *mask = NULL;
	struct st_pixel_blit blit;
	char *source = parse_options(text, &options, paste);

	if (source == NULL) goto done;
	if (paste) {
		if (options.buffer >= 2) goto done;
		image = terminal->apc->images[options.buffer];
	}
	else if (*source == ';' && size > (size_t)(source + 1 - text))
		image = read_image(terminal, type, source + 1, size - (size_t)(source + 1 - text), blob);
	if (image == NULL) goto done;
	if (options.sw == 0) options.sw = image->width - options.sx;
	if (options.sh == 0) options.sh = image->height - options.sy;
	if (options.sx > UINT32_MAX || options.sy > UINT32_MAX
	    || options.sw > UINT32_MAX || options.sh > UINT32_MAX
	    || options.dx < INT32_MIN || options.dx > INT32_MAX
	    || options.dy < INT32_MIN || options.dy > INT32_MAX
	    || options.mx > UINT32_MAX || options.my > UINT32_MAX
	    || options.zx == 0 || options.zx > UINT32_MAX
	    || options.zy == 0 || options.zy > UINT32_MAX) goto done;
	if (options.mask != NULL) {
		uint64_t count = (uint64_t)options.sw * options.sh;

		if (count / 8 + (count % 8 != 0) > options.mask_size) goto done;
		if (options.mw == 0) options.mw = options.sw;
		if (options.mh == 0) options.mh = options.sh;
		if (options.mw > UINT32_MAX || options.mh > UINT32_MAX) goto done;
		count = (uint64_t)options.mw * options.mh;
		if (count / 8 + (count % 8 != 0) > options.mask_size) goto done;
		options.mask->width = (uint32_t)options.mw;
		options.mask->height = (uint32_t)options.mh;
	}
	if (options.mask_file != NULL) {
		st_pixel_mask_destroy(options.mask);
		options.mask = read_mask(terminal, options.mask_file, strlen(options.mask_file), false);
		if (options.mask == NULL || (!paste
		    && (options.mask->width < options.sw || options.mask->height < options.sh))) goto done;
	}
	mask = options.buffered_mask ? terminal->apc->mask : options.mask;
	blit = (struct st_pixel_blit){
		.source_x = (uint32_t)options.sx, .source_y = (uint32_t)options.sy,
		.width = (uint32_t)options.sw, .height = (uint32_t)options.sh,
		.destination_x = (int32_t)options.dx, .destination_y = (int32_t)options.dy,
		.scale_x = (uint32_t)options.zx, .scale_y = (uint32_t)options.zy,
		.mask_x = (uint32_t)options.mx, .mask_y = (uint32_t)options.my,
		.flags = options.flags,
	};
	(void)st_console_blit_pixels(terminal->screen.console, image, mask, &blit);
done:
	free_options(&options);
	if (!paste) st_pixel_buffer_destroy(image);
}

static void
load_font(struct cterminal *terminal, const char *text)
{
	char *end;
	unsigned long slot = strtoul(text, &end, 10);
	void *data = NULL;
	size_t size;

	if (slot < CTERM_FIRST_DYNAMIC_FONT || slot > 255 || *end != ';') return;
	if (cterm_apc_cache_read(&data, &size, terminal->apc->directory, end + 1) != 0) return;
	if (size == 10240 || size == 4096 || size == 3584 || size == 2048) {
		/* This is deliberately font_slot, not the parsed slot: preserve
		 * the actual v1.10a target when the two values differ. */
		if (terminal->font_slot >= 0 && terminal->font_slot <= 255) {
			cterm_render_replace_font(terminal, (uint8_t)terminal->font_slot,
			    strdup("Cached Font"), data, size);
			return;
		}
	}
	free(data);
}

bool
cterm_apc_dispatch(struct cterminal *terminal, char *text, size_t size)
{
	if (terminal->apc == NULL) return false;
	/* v1.10a ensures the BBS cache before dispatching any APC, including
	 * blobs and queries. Cache failure silently consumes the command. */
	if (!cterm_apc_cache_ready(terminal->apc->directory)) return true;
	if (strncmp(text, "SyncTERM:C;S;", 13) == 0) store_file(terminal, text + 13, size - 13);
	else if (strncmp(text, "SyncTERM:C;L", 12) == 0 && (text[12] == 0 || text[12] == ';'))
		list_files(terminal, size == 12 ? "*" : text + 13);
	else if (strncmp(text, "SyncTERM:C;SetFont;", 19) == 0) load_font(terminal, text + 19);
	else if (strncmp(text, "SyncTERM:C;LoadPPMBlob", 22) == 0) load_image(terminal, text + 22, size - 22, IMAGE_PPM, true);
	else if (strncmp(text, "SyncTERM:C;LoadPPM", 18) == 0) load_image(terminal, text + 18, size - 18, IMAGE_PPM, false);
	else if (strncmp(text, "SyncTERM:C;LoadPBMBlob", 22) == 0) load_mask(terminal, text + 22, size - 22, true);
	else if (strncmp(text, "SyncTERM:C;LoadPBM", 18) == 0) load_mask(terminal, text + 18, size - 18, false);
	else if (strncmp(text, "SyncTERM:C;LoadJXLBlob", 22) == 0) load_image(terminal, text + 22, size - 22, IMAGE_JXL, true);
	else if (strncmp(text, "SyncTERM:C;LoadJXL", 18) == 0) load_image(terminal, text + 18, size - 18, IMAGE_JXL, false);
	else if (strncmp(text, "SyncTERM:C;DrawPPMBlob", 22) == 0) draw_image(terminal, text + 22, size - 22, IMAGE_PPM, true, false);
	else if (strncmp(text, "SyncTERM:C;DrawPPM", 18) == 0) draw_image(terminal, text + 18, size - 18, IMAGE_PPM, false, false);
	else if (strncmp(text, "SyncTERM:C;DrawJXLBlob", 22) == 0) draw_image(terminal, text + 22, size - 22, IMAGE_JXL, true, false);
	else if (strncmp(text, "SyncTERM:C;DrawJXL", 18) == 0) draw_image(terminal, text + 18, size - 18, IMAGE_JXL, false, false);
	else if (strncmp(text, "SyncTERM:P;Copy", 15) == 0) copy_image(terminal, text + 15);
	else if (strncmp(text, "SyncTERM:P;Paste", 16) == 0) draw_image(terminal, text + 16, size - 16, IMAGE_PPM, false, true);
	else if (strncmp(text, "SyncTERM:Q;JXL", 14) == 0)
		cterm_respond(terminal, (terminal->screen.capabilities & ST_CONSOLE_CAP_PIXELS) != 0
		    && st_pixel_image_jxl_supported() ? "\x1b[=1;1-n" : "\x1b[=1;0-n", 8);
	else if (strncmp(text, "SyncTERM:A;", 11) == 0)
		return cterm_apc_audio_dispatch(terminal, text + 11, size - 11);
	else if (strncmp(text, "SyncTERM:Q;", 11) == 0)
		return cterm_apc_feature_query(terminal, text + 11);
	else if (strcmp(text, "SyncTERM:VER") == 0) report_version(terminal);
	else return false;
	return true;
}

void
cterm_apc_destroy(struct cterminal *terminal)
{
	if (terminal->apc == NULL) return;
	for (size_t i = 0; i < 2; ++i) st_pixel_buffer_destroy(terminal->apc->images[i]);
	st_pixel_mask_destroy(terminal->apc->mask);
	for (size_t i = 0; i < 256; ++i) free(terminal->apc->patches[i].frames);
	for (size_t i = 0; i < 14; ++i) audio_stream_destroy(terminal->apc->channels[i]);
	free(terminal->apc);
	terminal->apc = NULL;
}
