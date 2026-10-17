/* CTerm-private SyncTERM application commands and per-terminal state. */
/* Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef CTERM_APC_H_
#define CTERM_APC_H_

#include "audio.h"
#include "st_pixels.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cterminal;
struct cterm_apc_patch {
	int16_t *frames;
	size_t count;
};
struct cterm_apc_state {
	const char *directory;
	const char *version;
	struct st_pixel_buffer *images[2];
	struct st_pixel_mask *mask;
	struct cterm_apc_patch patches[256];
	audio_stream_t *channels[14];
	bool notify[16];
	bool was_running[16];
};

bool cterm_apc_dispatch(struct cterminal *terminal, char *text, size_t size);
void cterm_apc_destroy(struct cterminal *terminal);
void cterm_apc_poll(struct cterminal *terminal);
void cterm_apc_audio_query(struct cterminal *terminal, int count, const uint64_t *params);
bool cterm_apc_audio_dispatch(struct cterminal *terminal, char *text, size_t size);
bool cterm_apc_feature_query(struct cterminal *terminal, const char *text);

/* APC uses the reference decoder's whitespace, optional-padding and
 * first-padding termination rules, not the strict general Base64 API. */
int cterm_apc_decode_base64(void **result, size_t *result_size,
	const char *text, size_t size);

#endif
