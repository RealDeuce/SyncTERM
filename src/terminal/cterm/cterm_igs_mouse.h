/* Atari pointer rendering and interactive query state, owned by CTerm. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_IGS_MOUSE_H
#define CTERM_IGS_MOUSE_H
#include <stdbool.h>
struct cterm_igs;
struct st_pixel_buffer;
struct cterm_igs_pointer {
	bool active, gem, drawn, host_visible, host_saved;
	int style, saved_mode, x, y, left, top;
	struct st_pixel_buffer *saved;
};
struct cterm_igs_query {
	bool configuring, active, release, ready;
	int selector, style, buttons, x, y, previous_x, previous_y, error;
	struct cterm_igs_pointer pointer;
};
int cterm_igs_pointer_begin(struct cterm_igs *, struct cterm_igs_pointer *, int style);
int cterm_igs_pointer_move(struct cterm_igs *, struct cterm_igs_pointer *, int x, int y);
int cterm_igs_pointer_end(struct cterm_igs *, struct cterm_igs_pointer *);
int cterm_igs_query_begin(struct cterm_igs *, int selector, int style);
int cterm_igs_query_sample(struct cterm_igs *, int x, int y, unsigned buttons);
int cterm_igs_query_finish(struct cterm_igs *);
int cterm_igs_query_cancel(struct cterm_igs *);
#endif
