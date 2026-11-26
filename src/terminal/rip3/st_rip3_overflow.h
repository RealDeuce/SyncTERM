/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_OVERFLOW_H_
#define ST_RIP3_OVERFLOW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_rip3_resource;
struct st_rip3_resource_paths;
struct st_rip3_saved_page;

enum {
	ST_RIP3_ARTICLE_COUNT = 36
};

struct st_rip3_overflow_article {
	struct st_rip3_saved_page *source, *remainder;
	int32_t                    page, frontier;
	bool                       enabled, captured;
};

struct st_rip3_overflow {
	struct st_rip3_overflow_article articles[ST_RIP3_ARTICLE_COUNT];
	struct st_rip3_saved_page      *pages;
	unsigned                        instance;
};

/* End, redefinition and navigation close the affected article's outputs.
 * An article switch leaves them open so source capture can resume. */
int st_rip3_overflow_close(struct st_rip3_overflow *, unsigned article);
int st_rip3_overflow_source(struct st_rip3_overflow *, const struct st_rip3_resource_paths *, unsigned article,
    uint8_t byte);
int st_rip3_overflow_remainder(struct st_rip3_overflow *, const struct st_rip3_resource_paths *, unsigned article,
    const uint8_t *bytes, size_t length);
/* Reset closes all output. A hard reset forgets names while keeping owned
 * files available for cleanup after the scene readers close. */
int     st_rip3_overflow_reset(struct st_rip3_overflow *, bool hard);
int     st_rip3_overflow_reset_article(struct st_rip3_overflow *, unsigned article);
void    st_rip3_overflow_dispose(struct st_rip3_overflow *);
bool    st_rip3_overflow_writing_remainder(const struct st_rip3_overflow *, unsigned article);
int32_t st_rip3_overflow_frontier(const struct st_rip3_overflow *, unsigned article);
int     st_rip3_overflow_name(const struct st_rip3_overflow *, unsigned article, int32_t page, char *result,
    size_t capacity);
int     st_rip3_overflow_open(struct st_rip3_overflow *, const char *name, struct st_rip3_resource *result,
    bool *recognized);

#endif
