/* RetroArch Slang preset execution via librashader's versioned C API. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>

#include "console_config.h"
#include "console_slang.h"
#include "io_error.h"
#if ST_CONSOLE_HAVE_SLANG
 #define LIBRA_RUNTIME_OPENGL
 #include <stdlib.h>
 #include <string.h>

 #include <librashader.h>

 #include "dynamic_library.h"
 #include "st_threads.h"

struct st_slang {
	libra_gl_filter_chain_t handle;
};

 #define SLANG_API(X)                                                                                            \
	 X(instance_abi_version)                                                                                 \
	 X(instance_api_version) X(error_write) X(error_free) X(error_free_string) X(preset_create_with_options) \
	     X(preset_free) X(gl_filter_chain_create) X(gl_filter_chain_frame) X(gl_filter_chain_free)

 #define DECLARE(name) PFN_libra_##name name;

static struct {
	SLANG_API(DECLARE)
	bool ready;
} api;

 #undef DECLARE
static st_once_flag_t once = ST_ONCE_FLAG_INIT;

static void
load_runtime(void)
{
 #if ST_DEP_LIBRASHADER_LINKED
  #define LOAD(name) api.name = libra_##name;
 #else
	dynamic_library_t *library;
	const char        *names[] = {"rashader", "librashader"};
	if (dynamic_library_open(&library, names, 2, 2, DYNAMIC_LIBRARY_LOCAL) != 0)
		return;
  #define LOAD(name)                                                                           \
	  do {                                                                                 \
		  void *symbol = dynamic_library_symbol(library, "libra_" #name);              \
		  if (!symbol) {                                                               \
			  (void)dynamic_library_close(library);                                \
			  return;                                                              \
		  }                                                                            \
		  _Static_assert(sizeof(symbol) == sizeof(api.name), "function pointer size"); \
		  memcpy(&api.name, &symbol, sizeof(symbol));                                  \
	  } while (0);
 #endif
	SLANG_API(LOAD)
 #undef LOAD
	api.ready = api.instance_abi_version() == LIBRASHADER_CURRENT_ABI && api.instance_api_version() >= 2;
}

static int
check(libra_error_t error)
{
	if (!error)
		return 0;
	char *message = NULL;
	api.error_write(error, &message);
	fprintf(stderr, "Slang scaler: %s\n", message ? message : "unknown error");
	if (message)
		api.error_free_string(&message);
	api.error_free(&error);
	return IO_ERROR_IO;
}

int
st_slang_create(struct st_slang **result, const char *path, const void *(*symbol)(const char *))
{
	st_call_once(&once, load_runtime);
	if (!api.ready) {
		fprintf(stderr, "Slang presets require a compatible librashader OpenGL runtime (ABI %u).\n",
		    (unsigned)LIBRASHADER_CURRENT_ABI);
		return IO_ERROR_NOT_SUPPORTED;
	}
	struct st_slang *chain = calloc(1, sizeof(*chain));
	if (!chain)
		return IO_ERROR_OUT_OF_MEMORY;
	libra_shader_preset_t preset = NULL;
	libra_preset_opt_t options = {.version = 2, .original_aspect_uniforms = true, .frametime_uniforms = true};
	int                status  = check(api.preset_create_with_options(path, NULL, &options, &preset));
	if (!status) {
		filter_chain_gl_opt_t gl = {.version = 2};
		status                   = check(api.gl_filter_chain_create(&preset, symbol, &gl, &chain->handle));
	}
	if (preset)
		check(api.preset_free(&preset));
	if (status)
		free(chain);
	else
		*result = chain;
	return status;
}

void
st_slang_free(struct st_slang *chain)
{
	if (chain) {
		check(api.gl_filter_chain_free(&chain->handle));
		free(chain);
	}
}

int
st_slang_frame(struct st_slang *chain, size_t count, uint32_t source, uint32_t width, uint32_t height,
    uint32_t target, uint32_t output_width, uint32_t output_height, uint32_t elapsed)
{
	const uint32_t   rgba8   = 0x8058;
	libra_image_gl_t input   = {source, rgba8, width, height};
	libra_image_gl_t output  = {target, rgba8, output_width, output_height};
	frame_gl_opt_t   options = {.version = 2,
	    .clear_history                   = count == 0,
	    .frame_direction                 = 1,
	    .total_subframes                 = 1,
	    .current_subframe                = 1,
	    .aspect_ratio                    = (float)output_width / output_height,
	    .frames_per_second               = 60,
	    .frametime_delta                 = elapsed};
	return check(api.gl_filter_chain_frame(&chain->handle, count, input, output, NULL, NULL, &options));
}
#else
int
st_slang_create(struct st_slang **result, const char *path, const void *(*symbol)(const char *))
{
	(void)result;
	(void)path;
	(void)symbol;
	fprintf(stderr, "Slang presets require a build with librashader support (ST_CONSOLE_SLANG).\n");
	return IO_ERROR_NOT_SUPPORTED;
}

void
st_slang_free(struct st_slang *chain)
{
	(void)chain;
}

int
st_slang_frame(struct st_slang *chain, size_t count, uint32_t source, uint32_t width, uint32_t height,
    uint32_t target, uint32_t output_width, uint32_t output_height, uint32_t elapsed)
{
	(void)chain;
	(void)count;
	(void)source;
	(void)width;
	(void)height;
	(void)target;
	(void)output_width;
	(void)output_height;
	(void)elapsed;
	return IO_ERROR_NOT_SUPPORTED;
}
#endif
