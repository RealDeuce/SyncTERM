/* Operation Overkill ][ wire protocol and embedded terminal resources. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_OOII_H_
#define ST_OOII_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ST_OOII_MAX_MODE 3

typedef struct st_ooii st_ooii_t;

struct st_ooii_config {
	/* All callbacks run synchronously on the session owner. They may select
	 * a mode, but must not reenter the parser or destroy this parser. */
	/* Embedded artwork and expanded text are always ANSI/CP437. */
	int      (*render)(void *context, const uint8_t *bytes, size_t size);
	int      (*reply)(void *context, const uint8_t *bytes, size_t size);
	void     (*sound)(void *context, const uint8_t *samples, size_t size);
	unsigned (*attributes)(void *context);
	void    *context;
};

int  st_ooii_create(st_ooii_t **result, const struct st_ooii_config *config);
void st_ooii_destroy(st_ooii_t *ooii);
int  st_ooii_mode(const st_ooii_t *ooii);
/* Clamps to 0..3 and discards a partial command or detection prefix. */
void st_ooii_set_mode(st_ooii_t *ooii, int mode);
/* Observe original wire bytes while disabled. Finish a matched banner after delivery. */
bool st_ooii_detect(st_ooii_t *ooii, uint8_t byte);
void st_ooii_finish_banner(st_ooii_t *ooii);
/* Consume a decoded U+00BD...U+007C command and pass ordinary characters through. */
int st_ooii_character(st_ooii_t *ooii, uint32_t scalar, bool *consumed);

#endif
