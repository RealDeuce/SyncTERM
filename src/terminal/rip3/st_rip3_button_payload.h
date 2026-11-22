/* Pure definition-time payload views, borrowed from the enclosing command. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_BUTTON_PAYLOAD_H_
#define ST_RIP3_BUTTON_PAYLOAD_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

struct st_rip3_button_text {const uint8_t *data;size_t length;};
struct st_rip3_button_payload {
	int identity;
	struct st_rip3_button_text icon,label,host;
};
/* One ID parse, leading ASCII-space removal, then first-two-delimiter split.
 * Raw slot bounds precede expansion. The host slot is entirely opaque.
 * Failure preserves result; the void command parser consumes this status. */
int st_rip3_button_payload_parse(struct st_rip3_button_payload *result,
	const uint8_t *text,size_t length);

int st_rip3_button_payload_parse_encoded(struct st_rip3_button_payload *result,
	const uint8_t *text,size_t length,bool converted_codepage);
#endif
