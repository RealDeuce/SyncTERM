/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_pixel_image.h"

#include "io_error.h"
#include "st_pixels.h"

#if defined(ST_PIXEL_IMAGE_HAVE_JXL)

#include "dynamic_library.h"
#include "st_threads.h"

#include <jxl/cms.h>
#include <jxl/decode.h>
#include <jxl/encode.h>
#if defined(ST_PIXEL_IMAGE_HAVE_JXL_THREADS)
#include <jxl/resizable_parallel_runner.h>
#endif

#include <stdint.h>
#include <stdlib.h>

#ifndef JPEGXL_MAJOR_VERSION
#define JPEGXL_MAJOR_VERSION 0
#endif

struct jxl_api {
	JxlDecoder *(*decoder_create)(const JxlMemoryManager *);
	void (*decoder_destroy)(JxlDecoder *);
	JxlDecoderStatus (*decoder_subscribe)(JxlDecoder *, int);
	JxlDecoderStatus (*decoder_set_input)(JxlDecoder *, const uint8_t *,
	    size_t);
	void (*decoder_close_input)(JxlDecoder *);
	JxlDecoderStatus (*decoder_process)(JxlDecoder *);
	JxlDecoderStatus (*decoder_basic_info)(const JxlDecoder *, JxlBasicInfo *);
	JxlDecoderStatus (*decoder_buffer_size)(const JxlDecoder *,
	    const JxlPixelFormat *, size_t *);
	JxlDecoderStatus (*decoder_set_buffer)(JxlDecoder *,
	    const JxlPixelFormat *, void *, size_t);
	JxlDecoderStatus (*decoder_set_cms)(JxlDecoder *, JxlCmsInterface);
	JxlDecoderStatus (*decoder_set_profile)(JxlDecoder *,
	    const JxlColorEncoding *, const uint8_t *, size_t);
	void (*colour_srgb)(JxlColorEncoding *, JXL_BOOL);
	const JxlCmsInterface *(*default_cms)(void);
#if defined(ST_PIXEL_IMAGE_HAVE_JXL_THREADS)
	JxlDecoderStatus (*decoder_set_runner)(JxlDecoder *, JxlParallelRunner,
	    void *);
	JxlParallelRunner parallel_runner;
	void *(*runner_create)(const JxlMemoryManager *);
	void (*runner_destroy)(void *);
	void (*runner_set_threads)(void *, size_t);
	uint32_t (*runner_suggest_threads)(uint64_t, uint64_t);
#endif
};

static st_once_flag_t jxl_once = ST_ONCE_FLAG_INIT;
static dynamic_library_t *jxl_library;
static dynamic_library_t *jxl_cms_library;
#if defined(ST_PIXEL_IMAGE_HAVE_JXL_THREADS)
static dynamic_library_t *jxl_threads_library;
#endif
static struct jxl_api jxl;
static bool jxl_available;

#define LOAD_REQUIRED(library, member, type, symbol) do { \
	jxl.member = (type)dynamic_library_symbol((library), #symbol); \
	if (jxl.member == NULL) \
		goto failed; \
} while (0)

static void
load_jxl(void)
{
	const char *base_names[] = {"jxl"};
	const char *cms_names[] = {"jxl_cms"};

	if (dynamic_library_open(&jxl_library, base_names, 1,
	    JPEGXL_MAJOR_VERSION, DYNAMIC_LIBRARY_LOCAL) != 0
	    || dynamic_library_open(&jxl_cms_library, cms_names, 1,
	    JPEGXL_MAJOR_VERSION, DYNAMIC_LIBRARY_LOCAL) != 0)
		goto failed;
	LOAD_REQUIRED(jxl_library, decoder_create,
	    JxlDecoder *(*)(const JxlMemoryManager *), JxlDecoderCreate);
	LOAD_REQUIRED(jxl_library, decoder_destroy, void (*)(JxlDecoder *),
	    JxlDecoderDestroy);
	LOAD_REQUIRED(jxl_library, decoder_subscribe,
	    JxlDecoderStatus (*)(JxlDecoder *, int), JxlDecoderSubscribeEvents);
	LOAD_REQUIRED(jxl_library, decoder_set_input,
	    JxlDecoderStatus (*)(JxlDecoder *, const uint8_t *, size_t),
	    JxlDecoderSetInput);
	LOAD_REQUIRED(jxl_library, decoder_close_input, void (*)(JxlDecoder *),
	    JxlDecoderCloseInput);
	LOAD_REQUIRED(jxl_library, decoder_process,
	    JxlDecoderStatus (*)(JxlDecoder *), JxlDecoderProcessInput);
	LOAD_REQUIRED(jxl_library, decoder_basic_info,
	    JxlDecoderStatus (*)(const JxlDecoder *, JxlBasicInfo *),
	    JxlDecoderGetBasicInfo);
	LOAD_REQUIRED(jxl_library, decoder_buffer_size,
	    JxlDecoderStatus (*)(const JxlDecoder *, const JxlPixelFormat *,
	    size_t *), JxlDecoderImageOutBufferSize);
	LOAD_REQUIRED(jxl_library, decoder_set_buffer,
	    JxlDecoderStatus (*)(JxlDecoder *, const JxlPixelFormat *, void *,
	    size_t), JxlDecoderSetImageOutBuffer);
	LOAD_REQUIRED(jxl_library, decoder_set_cms,
	    JxlDecoderStatus (*)(JxlDecoder *, JxlCmsInterface), JxlDecoderSetCms);
	LOAD_REQUIRED(jxl_library, decoder_set_profile,
	    JxlDecoderStatus (*)(JxlDecoder *, const JxlColorEncoding *,
	    const uint8_t *, size_t), JxlDecoderSetOutputColorProfile);
	LOAD_REQUIRED(jxl_library, colour_srgb,
	    void (*)(JxlColorEncoding *, JXL_BOOL), JxlColorEncodingSetToSRGB);
	LOAD_REQUIRED(jxl_cms_library, default_cms,
	    const JxlCmsInterface *(*)(void), JxlGetDefaultCms);

#if defined(ST_PIXEL_IMAGE_HAVE_JXL_THREADS)
	{
		const char *thread_names[] = {"jxl_threads"};

		jxl.decoder_set_runner = (JxlDecoderStatus (*)(JxlDecoder *,
		    JxlParallelRunner, void *))dynamic_library_symbol(jxl_library,
		    "JxlDecoderSetParallelRunner");
		if (jxl.decoder_set_runner != NULL
		    && dynamic_library_open(&jxl_threads_library, thread_names, 1,
		    JPEGXL_MAJOR_VERSION, DYNAMIC_LIBRARY_LOCAL) == 0) {
			jxl.parallel_runner = (JxlParallelRunner)dynamic_library_symbol(
			    jxl_threads_library, "JxlResizableParallelRunner");
			jxl.runner_create = (void *(*)(const JxlMemoryManager *))
			    dynamic_library_symbol(jxl_threads_library,
			    "JxlResizableParallelRunnerCreate");
			jxl.runner_destroy = (void (*)(void *))dynamic_library_symbol(
			    jxl_threads_library, "JxlResizableParallelRunnerDestroy");
			jxl.runner_set_threads = (void (*)(void *, size_t))
			    dynamic_library_symbol(jxl_threads_library,
			    "JxlResizableParallelRunnerSetThreads");
			jxl.runner_suggest_threads = (uint32_t (*)(uint64_t, uint64_t))
			    dynamic_library_symbol(jxl_threads_library,
			    "JxlResizableParallelRunnerSuggestThreads");
			if (jxl.parallel_runner == NULL || jxl.runner_create == NULL
			    || jxl.runner_destroy == NULL
			    || jxl.runner_set_threads == NULL
			    || jxl.runner_suggest_threads == NULL) {
				(void)dynamic_library_close(jxl_threads_library);
				jxl_threads_library = NULL;
				jxl.parallel_runner = NULL;
			}
		}
	}
#endif
	jxl_available = true;
	return;

failed:
#if defined(ST_PIXEL_IMAGE_HAVE_JXL_THREADS)
	(void)dynamic_library_close(jxl_threads_library);
	jxl_threads_library = NULL;
#endif
	(void)dynamic_library_close(jxl_cms_library);
	(void)dynamic_library_close(jxl_library);
	jxl_cms_library = NULL;
	jxl_library = NULL;
}

bool
st_pixel_image_jxl_supported(void)
{
	st_call_once(&jxl_once, load_jxl);
	return jxl_available;
}

int
st_pixel_image_decode_jxl(struct st_pixel_buffer **result, const void *data,
	size_t length)
{
	struct st_pixel_buffer *pixels = NULL;
	JxlPixelFormat format = {3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 1};
	JxlDecoder *decoder;
	JxlBasicInfo info = {0};
	JxlColorEncoding srgb;
	const JxlCmsInterface *cms;
	uint8_t *bytes = NULL;
	size_t byte_count = 0;
	size_t pixel_count = 0;
	int error = IO_ERROR_INVALID_FORMAT;
#if defined(ST_PIXEL_IMAGE_HAVE_JXL_THREADS)
	void *runner = NULL;
#endif

	if (result == NULL || data == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (!st_pixel_image_jxl_supported())
		return IO_ERROR_NOT_SUPPORTED;
	decoder = jxl.decoder_create(NULL);
	if (decoder == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	cms = jxl.default_cms();
	if (cms == NULL || jxl.decoder_set_cms(decoder, *cms) != JXL_DEC_SUCCESS)
		goto done;
#if defined(ST_PIXEL_IMAGE_HAVE_JXL_THREADS)
	if (jxl.parallel_runner != NULL) {
		runner = jxl.runner_create(NULL);
		if (runner != NULL && jxl.decoder_set_runner(decoder,
		    jxl.parallel_runner, runner) != JXL_DEC_SUCCESS) {
			jxl.runner_destroy(runner);
			runner = NULL;
		}
	}
#endif
	if (jxl.decoder_subscribe(decoder, JXL_DEC_BASIC_INFO
	    | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS
	    || jxl.decoder_set_input(decoder, data, length) != JXL_DEC_SUCCESS)
		goto done;
	jxl.decoder_close_input(decoder);
	for (;;) {
		JxlDecoderStatus status = jxl.decoder_process(decoder);

		if (status == JXL_DEC_BASIC_INFO) {
			if (jxl.decoder_basic_info(decoder, &info) != JXL_DEC_SUCCESS
			    || info.xsize == 0 || info.ysize == 0
			    || (size_t)info.ysize > SIZE_MAX / (size_t)info.xsize)
				goto done;
			pixel_count = (size_t)info.xsize * (size_t)info.ysize;
			if (pixel_count > SIZE_MAX / 3)
				goto done;
#if defined(ST_PIXEL_IMAGE_HAVE_JXL_THREADS)
			if (runner != NULL)
				jxl.runner_set_threads(runner,
				    jxl.runner_suggest_threads(info.xsize, info.ysize));
#endif
		}
		else if (status == JXL_DEC_COLOR_ENCODING) {
			jxl.colour_srgb(&srgb, JXL_FALSE);
			if (jxl.decoder_set_profile(decoder, &srgb, NULL, 0)
			    != JXL_DEC_SUCCESS)
				goto done;
		}
		else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
			if (info.xsize == 0 || info.ysize == 0
			    || jxl.decoder_buffer_size(decoder, &format, &byte_count)
			    != JXL_DEC_SUCCESS || byte_count != pixel_count * 3)
				goto done;
			bytes = malloc(byte_count);
			if (bytes == NULL) {
				error = IO_ERROR_OUT_OF_MEMORY;
				goto done;
			}
			if (st_pixel_buffer_create(&pixels, info.xsize, info.ysize,
			    false) != 0) {
				error = IO_ERROR_OUT_OF_MEMORY;
				goto done;
			}
			if (jxl.decoder_set_buffer(decoder, &format, bytes, byte_count)
			    != JXL_DEC_SUCCESS)
				goto done;
		}
		else if (status == JXL_DEC_FULL_IMAGE) {
			if (pixels == NULL || bytes == NULL)
				goto done;
			for (size_t i = 0; i < pixel_count; ++i)
				pixels->primary[i] = ST_PIXEL_COLOR_RGB_VALUE(
				    bytes[i * 3], bytes[i * 3 + 1], bytes[i * 3 + 2]);
			*result = pixels;
			pixels = NULL;
			error = 0;
			goto done;
		}
		else if (status == JXL_DEC_SUCCESS || status == JXL_DEC_ERROR
		    || status == JXL_DEC_NEED_MORE_INPUT)
			goto done;
	}

done:
	free(bytes);
	st_pixel_buffer_destroy(pixels);
#if defined(ST_PIXEL_IMAGE_HAVE_JXL_THREADS)
	if (runner != NULL)
		jxl.runner_destroy(runner);
#endif
	jxl.decoder_destroy(decoder);
	return error;
}

#else

bool
st_pixel_image_jxl_supported(void)
{
	return false;
}

int
st_pixel_image_decode_jxl(struct st_pixel_buffer **result, const void *data,
	size_t length)
{
	if (result == NULL || data == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	(void)length;
	return IO_ERROR_NOT_SUPPORTED;
}

#endif
