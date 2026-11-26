/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP_HOST_H
#define ST_RIP_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Called synchronously for RIP_DEFINE. Return zero and a NULL result to
 * cancel, or a malloc-owned string to accept. A nonzero return is an error. */
typedef int (*st_cterm_rip_prompt_callback_t)(void *context, const char *question, const char *initial,
    size_t field_width, bool reject_blank, char **result);

struct st_cterm_rip_choice {
	const char *label;
	uint32_t    hotkey; /* Unicode scalar, zero when no hotkey is specified. */
};

/* Synchronous, borrowed UTF-8 labels. Do not reenter the terminal. Set
 * selected to -1 for optional cancellation, or an index in [0,count).
 * Required lists ignore ordinary dismissal; close/error returns nonzero. */
typedef int (*st_cterm_rip_choice_callback_t)(void *context, const char *question,
    const struct st_cterm_rip_choice *choices, size_t count, bool required, int *selected);

struct st_cterm_rip_transfer_request {
	bool        upload;
	unsigned    protocol, file_type;
	const char *directory, *filename;
	/* RIPv3 supplies the BBS RIP root and hands off the optional LF.
	 * RIPv1 always hands off at CR. */
	const char *cache_root;
	bool        skip_lf;
};

/* Runs synchronously on the terminal owner at the terminating CR. Accepting
 * must claim the connection before returning; borrowed strings cannot escape
 * this callback. Protocol I/O and UI run outside the RIP parser. */
typedef int (
    *st_cterm_rip_transfer_callback_t)(void *context, const struct st_cterm_rip_transfer_request *request);

struct st_rip154_services {
	const char *rip_directory;
	/* Without a callback, interactive RIP_DEFINE leaves the variable unchanged. */
	st_cterm_rip_prompt_callback_t   rip_prompt;
	void                            *rip_prompt_context;
	st_cterm_rip_choice_callback_t   rip_choice;
	void                            *rip_choice_context;
	st_cterm_rip_transfer_callback_t rip_transfer;
	void                            *rip_transfer_context;
};
#endif
