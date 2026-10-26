/* Operation Overkill ][ wire protocol and embedded terminal resources. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_OOII_H_
#define ST_OOII_H_

#include <stddef.h>
#include <stdint.h>

#define ST_OOII_MAX_MODE 3

typedef struct st_ooii st_ooii_t;

struct st_ooii_config {
	/* All callbacks run synchronously on the session owner. They may select
	 * a mode, but must not reenter write() or destroy this parser. */
	int (*pass)(void *context, const uint8_t *bytes, size_t size);
	/* Embedded artwork and expanded text are always ANSI/CP437. */
	int (*render)(void *context, const uint8_t *bytes, size_t size);
	int (*reply)(void *context, const uint8_t *bytes, size_t size);
	void (*sound)(void *context, const uint8_t *samples, size_t size);
	unsigned (*attributes)(void *context);
	void *context;
};

int st_ooii_create(st_ooii_t **result, const struct st_ooii_config *config);
void st_ooii_destroy(st_ooii_t *ooii);
int st_ooii_mode(const st_ooii_t *ooii);
/* Clamps to 0..3 and discards a partial command or detection prefix. */
void st_ooii_set_mode(st_ooii_t *ooii, int mode);
/* Matches the legacy automatic mode-1/mode-2 banners even across reads.
 * Once enabled, consumes AB...7C command frames and passes other wire bytes.
 * Oversized or truncated frames are discarded, never executed partially. */
int st_ooii_write(st_ooii_t *ooii, const void *bytes, size_t size);

#endif
