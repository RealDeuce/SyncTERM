/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_DDOFX_GRAPHICS_H_
#define ST_DDOFX_GRAPHICS_H_

#include "st_cterm.h"
#include "st_ddofx.h"

void st_ddofx_load_vga_font(struct st_ddofx *, const struct st_ddofx_sink *, const char *);
const char *st_ddofx_vga_font_name(const struct st_ddofx *);
void st_ddofx_release_vga_font(struct st_ddofx *);
uint32_t st_ddofx_pointer_wait_ms(const struct st_ddofx *);
void st_ddofx_pointer_poll(struct st_ddofx *);
void st_ddofx_mouse_command(struct st_ddofx *, const struct st_ddofx_sink *, char *);
uint32_t st_ddofx_mouse_wait_ms(const struct st_ddofx *);
void st_ddofx_mouse_poll(struct st_ddofx *);
bool st_ddofx_fade_step(struct st_ddofx *, uint64_t now);
void st_ddofx_gadget_command(struct st_ddofx *, const struct st_ddofx_sink *, char *body);
uint32_t st_ddofx_marquee_wait_ms(const struct st_ddofx *);
void st_ddofx_marquee_poll(struct st_ddofx *);
uint32_t st_ddofx_grid_wait_ms(const struct st_ddofx *);
void st_ddofx_grid_poll(struct st_ddofx *);
bool st_ddofx_bar_command(struct st_ddofx *, char *body);
uint32_t st_ddofx_bar_wait_ms(const struct st_ddofx *);
void st_ddofx_bar_poll(struct st_ddofx *);

bool          st_ddofx_svga(const struct st_ddofx_sink *);
void          st_ddofx_graphics_reset(struct st_ddofx_graphics *);
void          st_ddofx_graphics_enter(struct st_ddofx *, const struct st_ddofx_sink *);
void          st_ddofx_graphics_exit(struct st_ddofx *);
void          st_ddofx_graphics_command(struct st_ddofx *, const struct st_ddofx_sink *, char *);
st_cterm_t   *st_ddofx_graphics_text(struct st_ddofx_graphics *);
st_console_t *st_ddofx_graphics_view(struct st_ddofx_graphics *);

#endif
