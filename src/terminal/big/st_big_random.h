/* BIG 1.03 shared RNG and incremental rejection helper. */
/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_BIG_RANDOM_H_
#define ST_BIG_RANDOM_H_

#include <stddef.h>
#include <stdint.h>

/* One application-owned state is shared by numeric r, X0 and sound effects.
 * Seed conversion belongs to the caller; original srand takes only a WORD. */
void st_big_random_seed(uint32_t *state, uint16_t seed);
uint16_t st_big_random_next(uint32_t *state);

/* Try at most budget draws from the shared state with the current signed
 * WORD bounds. On acceptance, write value and return zero. IN_PROGRESS keeps
 * value unchanged and permits another poll with fresh bounds and the same
 * advanced state; there is no total retry cap or implicit reseeding. A zero
 * budget yields without drawing. attempts is set on every valid-pointer
 * call. Invalid arguments preserve state/value. No clock or key poll is
 * owned here; cancellation/shutdown belongs to the serialized source owner. */
int st_big_random_range(uint32_t *state, uint16_t from, uint16_t to,
	size_t budget, size_t *attempts, uint16_t *value);

#endif
