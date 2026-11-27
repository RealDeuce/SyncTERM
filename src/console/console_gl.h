/* OpenGL window presentation. All callbacks run on the window owner thread. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_CONSOLE_GL_H_
#define ST_CONSOLE_GL_H_

#include "console_internal.h"

struct st_gl_host {
	void       *window;
	int         (*current)(void *window, uint32_t width, uint32_t height);
	const void *(*symbol)(const char *name);
	int         (*swap)(void *window);
	void        (*close)(void *window);
};

struct st_gl_renderer;
int  st_gl_create(struct st_gl_renderer **result, const struct st_gl_host *host);
void st_gl_destroy(struct st_gl_renderer *renderer);
int  st_gl_upload(void *context, const struct st_rgb_buffer *pixels, const struct st_console_rect *rect,
    const void *identity, uint64_t generation);
int  st_gl_select(struct st_gl_renderer *renderer, const char *preset);
int  st_gl_draw(struct st_gl_renderer *renderer, const struct st_console_rect *dest, uint32_t width,
    uint32_t height, uint32_t now, bool reset_history);
bool st_gl_animated(const struct st_gl_renderer *renderer);

void st_console_bitmap_gl_host(struct st_console_bitmap_presenter *presenter, struct st_gl_host host);
void st_console_bitmap_gl_close(struct st_console_bitmap_presenter *presenter);
#endif
