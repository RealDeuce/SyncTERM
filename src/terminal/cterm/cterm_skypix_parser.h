/* SkyPix wire grammar. No RIP state, commands, or framing are shared. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_SKYPIX_PARSER_H_
#define CTERM_SKYPIX_PARSER_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct cterm_skypix_parser {
	uint8_t bytes[1024];
	size_t length;
	unsigned parameters[17];
	size_t count;
	unsigned state;
	bool invalid, opaque_escape;
};
struct cterm_skypix_sink {
	void *context;
	int (*text)(void *, const uint8_t *, size_t);
	int (*command)(void *, const unsigned *, size_t, const uint8_t *, size_t);
	bool (*paused)(void *);
	int (*begin_command)(void *);
};
int cterm_skypix_parser_feed(struct cterm_skypix_parser *, const struct cterm_skypix_sink *,
							 const uint8_t *, size_t, size_t *);
int cterm_skypix_parser_finish(struct cterm_skypix_parser *, const struct cterm_skypix_sink *);
#endif
