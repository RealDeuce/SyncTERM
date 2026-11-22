/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BUTTON_RENDER_H
#define ST_RIP3_BUTTON_RENDER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct st_rip3_command_host;
struct st_rip3_command_sink;
struct st_rip3_graphical;
struct st_rip3_button_renderer;
/* One session thread owns stages; providers run outside captured drawing
 * contexts. The worker and host bindings outlive the renderer. Destroy mouse
 * fields before destroying this renderer, after canceling its active job. */
int st_rip3_button_renderer_create(struct st_rip3_button_renderer **result,
	const struct st_rip3_command_host *host,const struct st_rip3_command_sink *sink,
	struct st_rip3_graphical *worker,bool converted_codepage);
void st_rip3_button_renderer_destroy(struct st_rip3_button_renderer *renderer);
void st_rip3_button_renderer_cancel(struct st_rip3_button_renderer *renderer);
void st_rip3_button_renderer_start(struct st_rip3_button_renderer *renderer,
	const uint8_t *command,size_t length);
void st_rip3_button_renderer_process(struct st_rip3_button_renderer *renderer,size_t budget);
bool st_rip3_button_renderer_pending(const struct st_rip3_button_renderer *renderer);
#endif
