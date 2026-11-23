/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_COLUMNS_H_
#define ST_RIP3_COLUMNS_H_

#include "st_rip3_surface.h"
#include "st_rip3_overflow.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_command_host;

enum { ST_RIP3_COLUMN_COUNT = 36, ST_RIP3_COLUMN_SEARCH_WORDS = 8, ST_RIP3_COLUMN_SEARCH_LENGTH = 16 };

struct st_rip3_column_search {
	uint8_t words[ST_RIP3_COLUMN_SEARCH_WORDS][ST_RIP3_COLUMN_SEARCH_LENGTH];
	size_t lengths[ST_RIP3_COLUMN_SEARCH_WORDS];
	size_t count;
	uint8_t color;
};

struct st_rip3_column {
	struct st_rip3_rect area;
	struct st_rip3_column_search search;
	unsigned article;
	bool defined;
};

struct st_rip3_column_definition {
	struct st_rip3_rect area;
	unsigned slot, article, flags;
	uint8_t color;
	bool extended, suppressed;
};

struct st_rip3_article_flow;

struct st_rip3_columns {
	struct st_rip3_column slots[ST_RIP3_COLUMN_COUNT];
	struct st_rip3_overflow overflow;
	struct st_rip3_article_flow *flow;
	unsigned article, current;
	int64_t offset;
	bool active;
};

int st_rip3_columns_decode(struct st_rip3_column_definition *, const struct st_rip3_command_host *,
	const uint8_t *command, size_t length, size_t *tail);
int st_rip3_columns_define(struct st_rip3_columns *, const struct st_rip3_command_host *,
	const struct st_rip3_column_definition *, const uint8_t *words, size_t length, bool converted_codepage);
int st_rip3_columns_line(struct st_rip3_columns *, const struct st_rip3_command_host *,
	const uint8_t *text, size_t length, bool justify);
bool st_rip3_columns_raw_active(const struct st_rip3_columns *);
int st_rip3_columns_feed(struct st_rip3_columns *, const struct st_rip3_command_host *,
	const uint8_t *bytes, size_t length, bool *changed);
int st_rip3_columns_end(struct st_rip3_columns *, const struct st_rip3_command_host *);
void st_rip3_columns_invalidate(struct st_rip3_columns *);
void st_rip3_columns_reset(struct st_rip3_columns *, bool hard);
void st_rip3_columns_dispose(struct st_rip3_columns *);

#endif
