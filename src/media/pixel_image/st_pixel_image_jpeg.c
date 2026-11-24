/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_pixel_image.h"

#include "io_error.h"
#include "st_pixels.h"

bool
st_pixel_image_jpeg_compiled(void)
{
#if defined(ST_PIXEL_IMAGE_HAVE_JPEG)
	return true;
#else
	return false;
#endif
}

#if defined(ST_PIXEL_IMAGE_HAVE_JPEG)

#include "dynamic_library.h"
#include "st_threads.h"

#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <jerror.h>

_Static_assert(sizeof(JSAMPLE) == 1, "JPEG decoding requires 8-bit samples");

struct jpeg_api {
	struct jpeg_error_mgr *(*std_error)(struct jpeg_error_mgr *);
	void (*create)(j_decompress_ptr, int, size_t);
	void (*destroy)(j_decompress_ptr);
	void (*source)(j_decompress_ptr, const unsigned char *, unsigned long);
	int (*header)(j_decompress_ptr, boolean);
	boolean (*start)(j_decompress_ptr);
	JDIMENSION (*rows)(j_decompress_ptr, JSAMPARRAY, JDIMENSION);
	boolean (*finish)(j_decompress_ptr);
};

struct jpeg_errors {
	struct jpeg_error_mgr manager;
	jmp_buf jump;
	int status;
};

struct jpeg_decode {
	struct jpeg_decompress_struct decoder;
	struct jpeg_errors errors;
	bool created;
	struct st_pixel_buffer *pixels;
	JSAMPLE *row;
};

static st_once_flag_t jpeg_once = ST_ONCE_FLAG_INIT;
static struct jpeg_api jpeg;
static bool jpeg_available;

static void
jpeg_error(j_common_ptr decoder)
{
	struct jpeg_errors *errors = (struct jpeg_errors *)decoder->err;

	errors->status = decoder->err->msg_code == JERR_OUT_OF_MEMORY
	    ? IO_ERROR_OUT_OF_MEMORY : IO_ERROR_INVALID_FORMAT;
	longjmp(errors->jump, 1);
}

static void
jpeg_message(j_common_ptr decoder, int level)
{
	/* libjpeg otherwise repairs missing EOI/data and returns a partial image. */
	if (level < 0)
		jpeg_error(decoder);
}

static void
jpeg_cleanup(struct jpeg_decode *context)
{
	if (context->created)
		jpeg.destroy(&context->decoder);
	st_pixel_buffer_destroy(context->pixels);
	free(context->row);
	free(context);
}

static void
jpeg_prepare(struct jpeg_decode *context)
{
	context->decoder.err = jpeg.std_error(&context->errors.manager);
	context->errors.manager.error_exit = jpeg_error;
	context->errors.manager.emit_message = jpeg_message;
}

#define LOAD_JPEG(member, type, symbol) do { \
	jpeg.member = (type)dynamic_library_symbol(library, #symbol); \
	if (jpeg.member == NULL) \
		goto failed; \
} while (0)

static void
load_jpeg(void)
{
	struct jpeg_decode *context;

#if ST_DEP_JPEG_LINKED
	jpeg.std_error = jpeg_std_error;
	jpeg.create = jpeg_CreateDecompress;
	jpeg.destroy = jpeg_destroy_decompress;
	jpeg.source = jpeg_mem_src;
	jpeg.header = jpeg_read_header;
	jpeg.start = jpeg_start_decompress;
	jpeg.rows = jpeg_read_scanlines;
	jpeg.finish = jpeg_finish_decompress;
#else
	const char *names[] = {"jpeg"};
	dynamic_library_t *library = NULL;
	/* IJG 6b has SONAME 62; later compatible APIs use 7, 8, etc. */
	unsigned major = JPEG_LIB_VERSION == 62 ? 62 : JPEG_LIB_VERSION / 10;

	if (dynamic_library_open(&library, names, 1, major,
	    DYNAMIC_LIBRARY_LOCAL) != 0)
		return;
	LOAD_JPEG(std_error, struct jpeg_error_mgr *(*)(struct jpeg_error_mgr *),
	    jpeg_std_error);
	LOAD_JPEG(create, void (*)(j_decompress_ptr, int, size_t),
	    jpeg_CreateDecompress);
	LOAD_JPEG(destroy, void (*)(j_decompress_ptr), jpeg_destroy_decompress);
	LOAD_JPEG(source, void (*)(j_decompress_ptr, const unsigned char *,
	    unsigned long), jpeg_mem_src);
	LOAD_JPEG(header, int (*)(j_decompress_ptr, boolean), jpeg_read_header);
	LOAD_JPEG(start, boolean (*)(j_decompress_ptr), jpeg_start_decompress);
	LOAD_JPEG(rows, JDIMENSION (*)(j_decompress_ptr, JSAMPARRAY, JDIMENSION),
	    jpeg_read_scanlines);
	LOAD_JPEG(finish, boolean (*)(j_decompress_ptr), jpeg_finish_decompress);
#endif
	/* Validate the header/library ABI before advertising runtime support.
	 * The fully resolved library stays mapped even if initialization fails. */
	context = calloc(1, sizeof(*context));
	if (context == NULL)
		return;
	jpeg_prepare(context);
	if (setjmp(context->errors.jump) == 0) {
		context->created = true;
		jpeg.create(&context->decoder, JPEG_LIB_VERSION,
		    sizeof(context->decoder));
		jpeg_available = true;
	}
	jpeg_cleanup(context);
	return;
#if !ST_DEP_JPEG_LINKED
failed:
	(void)dynamic_library_close(library);
#endif
}

#undef LOAD_JPEG

bool
st_pixel_image_jpeg_supported(void)
{
	st_call_once(&jpeg_once, load_jpeg);
	return jpeg_available;
}

static bool
jpeg_profile(const struct jpeg_decompress_struct *decoder)
{
	const jpeg_component_info *y;
	const jpeg_component_info *cb;
	const jpeg_component_info *cr;

	if (decoder->data_precision != 8 || decoder->arith_code
	    || decoder->image_width == 0 || decoder->image_height == 0)
		return false;
	if (decoder->jpeg_color_space == JCS_GRAYSCALE)
		return decoder->num_components == 1;
	if (decoder->jpeg_color_space != JCS_YCbCr || decoder->num_components != 3)
		return false;
	y = &decoder->comp_info[0];
	cb = &decoder->comp_info[1];
	cr = &decoder->comp_info[2];
	if (cb->h_samp_factor != cr->h_samp_factor
	    || cb->v_samp_factor != cr->v_samp_factor)
		return false;
	/* These are ratios: common factors in all components do not change
	 * the color sampling. libjpeg validates absolute factors/MCU limits. */
	if (!((y->h_samp_factor == cb->h_samp_factor
	    && y->v_samp_factor == cb->v_samp_factor)
	    || (y->h_samp_factor == 2 * cb->h_samp_factor
	    && (y->v_samp_factor == cb->v_samp_factor
	    || y->v_samp_factor == 2 * cb->v_samp_factor))))
		return false;
	return true;
}

int
st_pixel_image_decode_jpeg(struct st_pixel_buffer **result,
    const void *data, size_t length)
{
	struct jpeg_decode *context;
	int status;

	if (result == NULL || data == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (length == 0)
		return IO_ERROR_INVALID_FORMAT;
	if (length > ULONG_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	if (!st_pixel_image_jpeg_supported())
		return IO_ERROR_NOT_SUPPORTED;
	context = calloc(1, sizeof(*context));
	if (context == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	jpeg_prepare(context);
	/* Cleanup state lives on the heap: changes made after setjmp must remain
	 * defined when an error callback jumps out of any libjpeg operation. */
	if (setjmp(context->errors.jump) != 0) {
		status = context->errors.status;
		jpeg_cleanup(context);
		return status;
	}
	context->created = true;
	jpeg.create(&context->decoder, JPEG_LIB_VERSION, sizeof(context->decoder));
	jpeg.source(&context->decoder, data, (unsigned long)length);
	status = IO_ERROR_INVALID_FORMAT;
	if (jpeg.header(&context->decoder, TRUE) != JPEG_HEADER_OK)
		goto done;
	status = IO_ERROR_NOT_SUPPORTED;
	if (!jpeg_profile(&context->decoder))
		goto done;
	context->decoder.out_color_space = JCS_RGB;
	context->decoder.dct_method = JDCT_ISLOW;
	context->decoder.quantize_colors = FALSE;
	status = IO_ERROR_INVALID_FORMAT;
	if (!jpeg.start(&context->decoder)
	    || context->decoder.output_components != 3)
		goto done;
	if ((uint64_t)context->decoder.output_width * 3 > SIZE_MAX) {
		status = IO_ERROR_OUT_OF_RANGE;
		goto done;
	}
	status = st_pixel_buffer_create(&context->pixels,
	    context->decoder.output_width, context->decoder.output_height, false);
	if (status != 0)
		goto done;
	context->row = malloc((size_t)context->decoder.output_width * 3);
	if (context->row == NULL) {
		status = IO_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	status = IO_ERROR_INVALID_FORMAT;
	while (context->decoder.output_scanline < context->decoder.output_height) {
		size_t y = context->decoder.output_scanline;
		JDIMENSION x;

		if (jpeg.rows(&context->decoder, &context->row, 1) != 1)
			goto done;
		for (x = 0; x < context->decoder.output_width; x++) {
			size_t offset = (size_t)x * 3;

			context->pixels->primary[y * context->pixels->width + x] =
			    ST_PIXEL_COLOR_RGB_VALUE(context->row[offset],
			    context->row[offset + 1], context->row[offset + 2]);
		}
	}
	if (!jpeg.finish(&context->decoder))
		goto done;
	*result = context->pixels;
	context->pixels = NULL;
	status = 0;
done:
	jpeg_cleanup(context);
	return status;
}

#else

bool
st_pixel_image_jpeg_supported(void)
{
	return false;
}

int
st_pixel_image_decode_jpeg(struct st_pixel_buffer **result,
    const void *data, size_t length)
{
	if (result == NULL || data == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (length == 0)
		return IO_ERROR_INVALID_FORMAT;
	return IO_ERROR_NOT_SUPPORTED;
}

#endif
