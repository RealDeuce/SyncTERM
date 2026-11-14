/* Incremental RIP wire recognizer. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef CTERM_RIP_PARSER_H_
#define CTERM_RIP_PARSER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct cterm_rip_parser {
	uint8_t *command;
	size_t length;
	size_t capacity;
	uint8_t window[5];
	/* RIPv1 can assemble a SOH/STX command while retaining an ANSI prefix. */
	uint8_t escape[32];
	size_t escape_length;
	uint8_t escape_utf8_remaining;
	bool ebc;
	bool music_pending;
	bool enabled;
	bool suspended;
	bool utf8_input;
	bool ansi_interleaving;
	bool command_terminated_cr;
};

struct cterm_rip_parser_sink {
	void *context;
	/* These callbacks run in wire order and must not retain borrowed data. */
	/* Commit pending terminal text before a graphics command or control. */
	int (*boundary)(void *context);
	int (*passthrough)(void *context, const uint8_t *bytes, size_t size);
	int (*command)(void *context, int level, int sublevel, uint8_t code,
	    const char *arguments);
	int (*control)(void *context, const uint8_t *bytes, size_t size);
	bool (*at_carriage_return_column)(void *context);
	bool (*text_disabled)(void *context);
	bool (*music_introducer)(void *context, uint8_t final);
	/* Rejected commands are diagnostics, not connection failures. */
	void (*warning)(void *context, const char *message);
};

void cterm_rip_parser_init(struct cterm_rip_parser *parser, bool enabled);
void cterm_rip_parser_destroy(struct cterm_rip_parser *parser);
int cterm_rip_parser_feed(struct cterm_rip_parser *parser,
    const struct cterm_rip_parser_sink *sink, const uint8_t *bytes,
    size_t size, size_t *consumed);
/* Local scene EOF dispatches an unterminated final command. */
int cterm_rip_parser_flush(struct cterm_rip_parser *parser,
    const struct cterm_rip_parser_sink *sink);

#endif
