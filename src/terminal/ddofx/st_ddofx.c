/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <string.h>

#include "st_ddofx.h"

static bool
matches(const struct st_ddofx *ddo, const char *command)
{
	if (ddo->length != strlen(command) || ddo->body[0] != (uint8_t)command[0])
		return false;
	/* The dispatch character is case-sensitive. Named verbs use ASCII case. */
	for (size_t i = 1; i < ddo->length; ++i) {
		uint8_t c = ddo->body[i];
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		if (c != (uint8_t)command[i])
			return false;
	}
	return true;
}

static void
execute(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	if (matches(ddo, "~HEARCMD"))
		ddo->ignoring = false;
	else if (matches(ddo, "~IGNORCMD"))
		ddo->ignoring = true;
	else if (!ddo->ignoring && matches(ddo, "dLITERM")) {
		static const uint8_t version[] = "LITERM Ver. 2.00\xff";
		sink->reply(sink->context, version, sizeof(version) - 1);
	}
}

int
st_ddofx_feed(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const uint8_t *bytes, size_t size,
    size_t *consumed)
{
	*consumed = 0;
	while (*consumed < size) {
		const uint8_t *here = bytes + *consumed;
		if (ddo->phase == ST_DDO_TEXT && *here != 0x1d) {
			size_t         remaining = size - *consumed;
			const uint8_t *gs        = memchr(here, 0x1d, remaining);
			size_t         length    = gs == NULL ? remaining : (size_t)(gs - here);
			size_t         used      = 0;
			int            status    = sink->text(sink->context, here, length, &used);
			*consumed += used;
			if (status != 0 || used < length)
				return status;
			continue;
		}
		uint8_t byte = *here;
		switch (ddo->phase) {
			case ST_DDO_TEXT:
				ddo->phase = ST_DDO_PREFIX;
				break;
			case ST_DDO_PREFIX:
				if (byte == '{') {
					ddo->phase  = ST_DDO_BODY;
					ddo->length = 0;
				}
				else if (byte != 0x1d && byte != 0) {
					ddo->phase = ST_DDO_TEXT;
					continue;
				}
				break;
			case ST_DDO_BODY:
				if (byte == '}') {
					ddo->phase = ST_DDO_TEXT;
					execute(ddo, sink);
				}
				else if (byte == '\r' || byte == '\n') {
					ddo->phase = ST_DDO_TEXT;
					continue;
				}
				else if (byte != 0) {
					if (ddo->length == sizeof(ddo->body))
						ddo->phase = ST_DDO_DISCARD;
					else
						ddo->body[ddo->length++] = byte;
				}
				break;
			case ST_DDO_DISCARD:
				if (byte == '}')
					ddo->phase = ST_DDO_TEXT;
				else if (byte == '\r' || byte == '\n') {
					ddo->phase = ST_DDO_TEXT;
					continue;
				}
				else if (byte == 0x1d)
					ddo->phase = ST_DDO_PREFIX;
				break;
		}
		++*consumed;
	}
	return 0;
}
