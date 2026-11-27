/* Native-resolution texture and final scaling on a window-owned GL context. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console_gl.h"
#include "console_gl_api.h"
#include "console_gl_shader.h"
#include "console_slang.h"
#include "io_error.h"
#include "string_compat.h"

struct st_gl_renderer {
	struct st_gl_api  api;
	struct st_gl_host host;
	GLuint            program, vao, texture, output;
	GLuint            pointy_program, framebuffer, intermediate[2];
	uint32_t          pass_width[2], pass_height[2];
	GLint             factor_uniform, repeat_uniform;
	bool              pointy;
	GLint             destination, origin, nearest;
	uint32_t          width, height, output_width, output_height;
	char             *preset;
	struct st_slang  *slang;
	size_t            frames;
	uint32_t          last_time;
};

static bool
load_api(struct st_gl_api *api, const struct st_gl_host *host)
{
	const void *symbol;
#define LOAD(name)                                                                               \
	do {                                                                                     \
		symbol = host->symbol("gl" #name);                                               \
		if (!symbol)                                                                     \
			return false;                                                            \
		_Static_assert(sizeof(api->name) == sizeof(symbol), "GL function pointer size"); \
		memcpy(&api->name, &symbol, sizeof(symbol));                                     \
	} while (0)
	LOAD(GetIntegerv);
	LOAD(GetError);
	LOAD(GenTextures);
	LOAD(DeleteTextures);
	LOAD(BindTexture);
	LOAD(ActiveTexture);
	LOAD(TexImage2D);
	LOAD(TexSubImage2D);
	LOAD(TexParameteri);
	LOAD(PixelStorei);
	LOAD(GenVertexArrays);
	LOAD(BindVertexArray);
	LOAD(DeleteVertexArrays);
	LOAD(CreateShader);
	LOAD(ShaderSource);
	LOAD(CompileShader);
	LOAD(GetShaderiv);
	LOAD(GetShaderInfoLog);
	LOAD(DeleteShader);
	LOAD(CreateProgram);
	LOAD(AttachShader);
	LOAD(LinkProgram);
	LOAD(GetProgramiv);
	LOAD(GetProgramInfoLog);
	LOAD(DeleteProgram);
	LOAD(UseProgram);
	LOAD(GetUniformLocation);
	LOAD(Uniform1i);
	LOAD(Uniform2i);
	LOAD(Viewport);
	LOAD(ClearColor);
	LOAD(Clear);
	LOAD(Disable);
	LOAD(DrawArrays);
	LOAD(GenFramebuffers);
	LOAD(DeleteFramebuffers);
	LOAD(FramebufferTexture2D);
	LOAD(CheckFramebufferStatus);
	LOAD(BindFramebuffer);
	LOAD(BindSampler);
#undef LOAD
	return true;
}

static GLuint
compile_shader(struct st_gl_api *api, GLenum type, const char *const *source, GLsizei count)
{
	GLuint shader   = api->CreateShader(type);
	GLint  compiled = 0;
	if (shader != 0) {
		api->ShaderSource(shader, count, source, NULL);
		api->CompileShader(shader);
		api->GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (!compiled) {
			char message[1024] = "";
			api->GetShaderInfoLog(shader, sizeof(message), NULL, message);
			fprintf(stderr, "OpenGL scaler shader: %s\n", message);
			api->DeleteShader(shader);
			return 0;
		}
	}
	return shader;
}

static void
bind_texture(struct st_gl_api *api, GLuint texture)
{
	api->ActiveTexture(GL_TEXTURE0);
	api->BindTexture(GL_TEXTURE_2D, texture);
	api->BindSampler(0, 0);
	api->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	api->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	api->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
}

static GLuint
link_program(struct st_gl_api *api, const char *const *source, GLsizei count)
{
	static const char *const vertex_source =
	    "#version 330 core\nvoid main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);"
	    " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }";
	GLuint vert    = compile_shader(api, GL_VERTEX_SHADER, &vertex_source, 1);
	GLuint frag    = compile_shader(api, GL_FRAGMENT_SHADER, source, count);
	GLuint program = 0;
	if (vert && frag) {
		GLint linked = 0;
		program      = api->CreateProgram();
		api->AttachShader(program, vert);
		api->AttachShader(program, frag);
		api->LinkProgram(program);
		api->GetProgramiv(program, GL_LINK_STATUS, &linked);
		if (!linked) {
			char message[1024] = "";
			api->GetProgramInfoLog(program, sizeof(message), NULL, message);
			fprintf(stderr, "OpenGL scaler link: %s\n", message);
			api->DeleteProgram(program);
			program = 0;
		}
	}
	api->DeleteShader(vert);
	api->DeleteShader(frag);
	return program;
}

int
st_gl_create(struct st_gl_renderer **result, const struct st_gl_host *host)
{
	struct st_gl_renderer *renderer = calloc(1, sizeof(*renderer));
	if (!renderer)
		return IO_ERROR_OUT_OF_MEMORY;
	struct st_gl_api *api = &renderer->api;
	if (!load_api(api, host)) {
		free(renderer);
		return IO_ERROR_NOT_SUPPORTED;
	}
	GLint major = 0, minor = 0;
	api->GetIntegerv(GL_MAJOR_VERSION, &major);
	api->GetIntegerv(GL_MINOR_VERSION, &minor);
	if (major < 3 || (major == 3 && minor < 3)) {
		free(renderer);
		return IO_ERROR_NOT_SUPPORTED;
	}
	renderer->host    = *host;
	renderer->program = link_program(api, fragment_source, sizeof(fragment_source) / sizeof(*fragment_source));
	if (!renderer->program) {
		free(renderer);
		return IO_ERROR_NOT_SUPPORTED;
	}
	renderer->destination = api->GetUniformLocation(renderer->program, "destination");
	renderer->origin      = api->GetUniformLocation(renderer->program, "origin");
	renderer->nearest     = api->GetUniformLocation(renderer->program, "nearest");
	api->GenVertexArrays(1, &renderer->vao);
	api->GenTextures(1, &renderer->texture);
	api->GenTextures(1, &renderer->output);
	*result = renderer;
	return 0;
}

void
st_gl_destroy(struct st_gl_renderer *renderer)
{
	if (!renderer)
		return;
	struct st_gl_api *api = &renderer->api;
	st_slang_free(renderer->slang);
	api->DeleteTextures(1, &renderer->texture);
	api->DeleteTextures(1, &renderer->output);
	api->DeleteTextures(2, renderer->intermediate);
	api->DeleteFramebuffers(1, &renderer->framebuffer);
	api->DeleteProgram(renderer->pointy_program);
	api->DeleteVertexArrays(1, &renderer->vao);
	api->DeleteProgram(renderer->program);
	free(renderer->preset);
	free(renderer);
}

int
st_gl_select(struct st_gl_renderer *renderer, const char *preset)
{
	if (!preset)
		preset = "";
	if (!strcmp(renderer->preset ? renderer->preset : "", preset))
		return 0;
	char *copy = strdup(preset);
	if (!copy)
		return IO_ERROR_OUT_OF_MEMORY;
	struct st_slang *replacement = NULL;
	bool             pointy      = !strcmp(preset, "Pointy");
	if (pointy && !renderer->pointy_program) {
		struct st_gl_api *api = &renderer->api;
		renderer->pointy_program =
		    link_program(api, pointy_source, sizeof(pointy_source) / sizeof(*pointy_source));
		if (!renderer->pointy_program) {
			free(copy);
			return IO_ERROR_NOT_SUPPORTED;
		}
		renderer->factor_uniform = api->GetUniformLocation(renderer->pointy_program, "multiplier");
		renderer->repeat_uniform = api->GetUniformLocation(renderer->pointy_program, "repeatOnly");
		api->GenFramebuffers(1, &renderer->framebuffer);
		api->GenTextures(2, renderer->intermediate);
	}
	if (*preset && !pointy && strcmp(preset, "Nearest") != 0) {
		int status = st_slang_create(&replacement, preset, renderer->host.symbol);
		if (status != 0) {
			free(copy);
			return status;
		}
	}
	st_slang_free(renderer->slang);
	free(renderer->preset);
	renderer->pointy = pointy;
	renderer->slang  = replacement;
	renderer->preset = copy;
	renderer->frames = 0;
	return 0;
}

bool
st_gl_animated(const struct st_gl_renderer *renderer)
{
	return renderer && renderer->slang;
}

int
st_gl_upload(void *context, const struct st_rgb_buffer *pixels, const struct st_console_rect *rect,
    const void *identity, uint64_t generation)
{
	struct st_gl_renderer *renderer = context;
	struct st_gl_api      *api      = &renderer->api;
	(void)identity;
	(void)generation;
	bind_texture(api, renderer->texture);
	struct st_console_rect full = {0, 0, pixels->width, pixels->height};
	if (renderer->width != pixels->width || renderer->height != pixels->height) {
		api->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels->width, pixels->height, 0, GL_BGRA,
		    GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		api->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
		rect = &full;
	}
	api->PixelStorei(GL_UNPACK_ALIGNMENT, 4);
	api->PixelStorei(GL_UNPACK_ROW_LENGTH, pixels->width);
	api->TexSubImage2D(GL_TEXTURE_2D, 0, rect->x, rect->y, rect->width, rect->height, GL_BGRA,
	    GL_UNSIGNED_INT_8_8_8_8_REV, pixels->pixels + (size_t)rect->y * pixels->width + rect->x);
	api->PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	if (api->GetError() != GL_NO_ERROR)
		return IO_ERROR_IO;
	renderer->width  = pixels->width;
	renderer->height = pixels->height;
	return 0;
}

static int
pointy_passes(struct st_gl_renderer *renderer, const struct st_console_rect *dest, GLuint *source)
{
	struct st_gl_api     *api = &renderer->api;
	uint32_t              w = renderer->width, h = renderer->height;
	struct st_pointy_plan plan = st_pixel_pointy_plan(dest->width / w, dest->height / h);
	/* Without a square edge pass the final Blocky shader can combine the
	 * integer repeats and fractional interpolation in a single pass. */
	if (!plan.count)
		return 0;
	bool repeat = plan.repeat_x != 1 || plan.repeat_y != 1;
	api->BindFramebuffer(GL_FRAMEBUFFER, renderer->framebuffer);
	api->BindVertexArray(renderer->vao);
	api->UseProgram(renderer->pointy_program);
	for (size_t pass = 0; pass < plan.count + repeat; ++pass) {
		bool     multiply = repeat && pass == 0;
		uint32_t x        = multiply ? plan.repeat_x : plan.factors[pass - repeat];
		uint32_t y        = multiply ? plan.repeat_y : x;
		w *= x;
		h *= y;
		size_t slot   = pass % 2;
		GLuint target = renderer->intermediate[slot];
		if (renderer->pass_width[slot] != w || renderer->pass_height[slot] != h) {
			bind_texture(api, target);
			api->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			if (api->GetError() != GL_NO_ERROR)
				return IO_ERROR_IO;
			renderer->pass_width[slot]  = w;
			renderer->pass_height[slot] = h;
		}
		api->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
		if (api->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			return IO_ERROR_IO;
		api->Viewport(0, 0, w, h);
		api->Uniform2i(renderer->factor_uniform, (GLint)x, (GLint)y);
		api->Uniform1i(renderer->repeat_uniform, multiply);
		bind_texture(api, *source);
		api->DrawArrays(GL_TRIANGLES, 0, 3);
		*source = target;
	}
	return 0;
}

int
st_gl_draw(struct st_gl_renderer *renderer, const struct st_console_rect *dest, uint32_t width, uint32_t height,
    uint32_t now, bool reset_history)
{
	struct st_gl_api *api    = &renderer->api;
	GLuint            source = renderer->texture;
	if (renderer->slang) {
		if (dest->width != renderer->output_width || dest->height != renderer->output_height) {
			bind_texture(api, renderer->output);
			api->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dest->width, dest->height, 0, GL_RGBA,
			    GL_UNSIGNED_BYTE, NULL);
			if (api->GetError() != GL_NO_ERROR)
				return IO_ERROR_IO;
			renderer->output_width  = dest->width;
			renderer->output_height = dest->height;
		}
		if (reset_history && renderer->frames) {
			/* A new terminal must not inherit feedback or history textures. */
			struct st_slang *fresh  = NULL;
			int              status = st_slang_create(&fresh, renderer->preset, renderer->host.symbol);
			if (status)
				return status;
			st_slang_free(renderer->slang);
			renderer->slang  = fresh;
			renderer->frames = 0;
		}
		int status = st_slang_frame(renderer->slang, renderer->frames, renderer->texture, renderer->width,
		    renderer->height, renderer->output, dest->width, dest->height,
		    renderer->frames ? now - renderer->last_time : 0);
		if (status)
			return status;
		++renderer->frames;
		renderer->last_time = now;
		source              = renderer->output;
	}
	api->Disable(GL_SCISSOR_TEST);
	api->Disable(GL_DITHER);
	api->Disable(GL_BLEND);
	api->Disable(GL_DEPTH_TEST);
	api->Disable(GL_FRAMEBUFFER_SRGB);
	if (renderer->pointy) {
		int status = pointy_passes(renderer, dest, &source);
		if (status)
			return status;
	}
	api->BindFramebuffer(GL_FRAMEBUFFER, 0);
	api->Viewport(0, 0, width, height);
	api->ClearColor(0, 0, 0, 1);
	api->Clear(GL_COLOR_BUFFER_BIT);
	int bottom = (int)height - (int)dest->y - (int)dest->height;
	api->Viewport(dest->x, bottom, dest->width, dest->height);
	api->UseProgram(renderer->program);
	api->Uniform2i(renderer->origin, dest->x, bottom);
	api->Uniform2i(renderer->destination, dest->width, dest->height);
	api->Uniform1i(renderer->nearest, !renderer->pointy && renderer->preset && *renderer->preset);
	bind_texture(api, source);
	api->BindVertexArray(renderer->vao);
	api->DrawArrays(GL_TRIANGLES, 0, 3);
	if (api->GetError() != GL_NO_ERROR)
		return IO_ERROR_IO;
	return renderer->host.swap(renderer->host.window);
}
