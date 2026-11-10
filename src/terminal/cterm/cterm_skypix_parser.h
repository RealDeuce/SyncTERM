/* SkyPix wire grammar. No RIP state, commands, or framing are shared. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef CTERM_SKYPIX_PARSER_H_
#define CTERM_SKYPIX_PARSER_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum {
	CTERM_SKYPIX_MAX_NUMBERS = 17,
	CTERM_SKYPIX_MAX_NUMBER_DIGITS = 32,
	CTERM_SKYPIX_MAX_STRING_BYTES = 1024,
};
struct cterm_skypix_string {
	const uint8_t *bytes;
	size_t size;
};
struct cterm_skypix_parser {
	/* Unfinished CSI and string[0] share bounded staging storage. */
	uint8_t bytes[CTERM_SKYPIX_MAX_STRING_BYTES];
	size_t length;
	unsigned numbers[CTERM_SKYPIX_MAX_NUMBERS];
	size_t number_count;
	unsigned state;
	bool invalid, opaque_escape;
};
struct cterm_skypix_sink {
	void *context;
	int (*text)(void *, const uint8_t *, size_t);
	/* Numeric and string positions are independent. The defined commands
	 * supply at most one string, borrowed for the duration of this call. */
	int (*command)(void *, const unsigned *, size_t, const struct cterm_skypix_string *, size_t);
	bool (*paused)(void *);
	int (*begin_command)(void *);
};
int cterm_skypix_parser_feed(struct cterm_skypix_parser *, const struct cterm_skypix_sink *,
							 const uint8_t *, size_t, size_t *);
int cterm_skypix_parser_finish(struct cterm_skypix_parser *, const struct cterm_skypix_sink *);
#endif
