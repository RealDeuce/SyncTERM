/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_overflow.h"

#include "st_rip3_resources.h"
#include "io_error.h"
#include "string_util.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct st_rip3_saved_page {
	struct st_rip3_saved_page *next;
	struct st_rip3_resource file;
	unsigned article;
	int32_t page;
	bool visible;
};

bool
st_rip3_overflow_writing_remainder(const struct st_rip3_overflow *overflow, unsigned article)
{
	return overflow->articles[article].remainder != NULL;
}

int32_t
st_rip3_overflow_frontier(const struct st_rip3_overflow *overflow, unsigned article)
{
	const struct st_rip3_overflow_article *state = &overflow->articles[article];
	return state->remainder != NULL ? state->remainder->page : state->frontier;
}

int
st_rip3_overflow_name(const struct st_rip3_overflow *overflow, unsigned article, int32_t page,
    char *result, size_t capacity)
{
	static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int length = snprintf(result, capacity, "%03u%c%04" PRId32 ".OVR", overflow->instance, digits[article], page);
	return length < 0 || (size_t)length >= capacity ? IO_ERROR_OUT_OF_RANGE : 0;
}

static void
discard(struct st_rip3_saved_page *page)
{
	if (page != NULL) {
		st_rip3_resource_remove_scratch(&page->file);
		free(page);
	}
}

static int
output(struct st_rip3_saved_page **current, const struct st_rip3_resource_paths *paths,
    unsigned article, int32_t page, const uint8_t *bytes, size_t length)
{
	if (*current == NULL) {
		struct st_rip3_saved_page *created = calloc(1, sizeof(*created));
		if (created == NULL)
			return IO_ERROR_OUT_OF_MEMORY;
		int status = st_rip3_resource_scratch(&created->file, paths);
		if (status != 0) {
			free(created);
			return status;
		}
		created->article = article;
		created->page = page;
		*current = created;
	}
	if (fwrite(bytes, 1, length, (*current)->file.stream) != length) {
		discard(*current);
		*current = NULL;
		return IO_ERROR_IO;
	}
	return 0;
}

int
st_rip3_overflow_source(struct st_rip3_overflow *overflow, const struct st_rip3_resource_paths *paths,
    unsigned article, uint8_t byte)
{
	struct st_rip3_overflow_article *state = &overflow->articles[article];
	if (!state->enabled || state->captured || state->page != 0 || state->frontier != 0)
		return 0;
	return output(&state->source, paths, article, 0, &byte, 1);
}

int
st_rip3_overflow_remainder(struct st_rip3_overflow *overflow, const struct st_rip3_resource_paths *paths,
    unsigned article, const uint8_t *bytes, size_t length)
{
	struct st_rip3_overflow_article *state = &overflow->articles[article];
	if (!state->enabled || length == 0)
		return 0;
	if (state->page == INT32_MAX)
		return IO_ERROR_OUT_OF_RANGE;
	return output(&state->remainder, paths, article, state->page + 1, bytes, length);
}

static int
publish(struct st_rip3_overflow *overflow, struct st_rip3_saved_page *page, bool source)
{
	if (page == NULL)
		return 0;
	int status = fclose(page->file.stream);
	page->file.stream = NULL;
	if (status != 0) {
		discard(page);
		return IO_ERROR_IO;
	}
	for (struct st_rip3_saved_page *old = overflow->pages; old != NULL; old = old->next)
		if (old->article == page->article && old->page == page->page)
			old->visible = false;
	page->visible = true;
	page->next = overflow->pages;
	overflow->pages = page;
	struct st_rip3_overflow_article *article = &overflow->articles[page->article];
	if (source)
		article->captured = true;
	else
		article->frontier = page->page;
	return 0;
}

int
st_rip3_overflow_close(struct st_rip3_overflow *overflow, unsigned article)
{
	struct st_rip3_overflow_article *state = &overflow->articles[article];
	int status = publish(overflow, state->source, true);
	int next_status = publish(overflow, state->remainder, false);
	state->source = NULL;
	state->remainder = NULL;
	return status == 0 ? next_status : status;
}

int
st_rip3_overflow_reset_article(struct st_rip3_overflow *overflow, unsigned article)
{
	int status = st_rip3_overflow_close(overflow, article);
	overflow->articles[article] = (struct st_rip3_overflow_article){0};
	for (struct st_rip3_saved_page *page = overflow->pages; page != NULL; page = page->next)
		if (page->article == article)
			page->visible = false;
	return status;
}

int
st_rip3_overflow_reset(struct st_rip3_overflow *overflow, bool hard)
{
	int status = 0;
	for (unsigned n = 0; n < ST_RIP3_ARTICLE_COUNT; ++n) {
		int closed = st_rip3_overflow_close(overflow, n);
		if (status == 0)
			status = closed;
		if (hard)
			overflow->articles[n] = (struct st_rip3_overflow_article){0};
		else
			overflow->articles[n].enabled = false;
	}
	if (hard)
		for (struct st_rip3_saved_page *page = overflow->pages; page != NULL; page = page->next)
			page->visible = false;
	return status;
}

void
st_rip3_overflow_dispose(struct st_rip3_overflow *overflow)
{
	for (unsigned n = 0; n < ST_RIP3_ARTICLE_COUNT; ++n) {
		discard(overflow->articles[n].source);
		discard(overflow->articles[n].remainder);
	}
	while (overflow->pages != NULL) {
		struct st_rip3_saved_page *page = overflow->pages;
		overflow->pages = page->next;
		discard(page);
	}
}

static bool
page_named(const struct st_rip3_overflow *overflow, const struct st_rip3_saved_page *page,
    const char *name)
{
	char wanted[32];
	return page != NULL && st_rip3_overflow_name(overflow, page->article, page->page, wanted, sizeof(wanted)) == 0
	    && ascii_case_compare(name, wanted) == 0;
}

int
st_rip3_overflow_open(struct st_rip3_overflow *overflow, const char *name,
    struct st_rip3_resource *result, bool *recognized)
{
	*recognized = false;
	size_t length = strlen(name);
	if (length < 12 || ascii_case_compare(name + length - 4, ".OVR") != 0)
		return IO_ERROR_NOT_FOUND;
	/* A lookalike asset outside the private page store cannot satisfy a
	 * name issued by this session. Other ordinary scene names still resolve. */
	char prefix[sizeof("655350")];
	int written = snprintf(prefix, sizeof(prefix), "%03u", overflow->instance);
	if (written < 0 || (size_t)written >= sizeof(prefix) || length < (size_t)written + 9
	    || strncmp(name, prefix, (size_t)written) != 0)
		return IO_ERROR_NOT_FOUND;
	char article = name[written];
	if ((article < '1' || article > '9') && (article < 'A' || article > 'Z')
	    && (article < 'a' || article > 'z'))
		return IO_ERROR_NOT_FOUND;
	const char *index = name + written + 1;
	if (*index == '-')
		++index;
	if (strspn(index, "0123456789") != (size_t)(name + length - 4 - index))
		return IO_ERROR_NOT_FOUND;
	*recognized = true;
	unsigned number = article <= '9' ? (unsigned)(article - '0')
	    : (unsigned)(article >= 'a' ? article - 'a' : article - 'A') + 10;
	const struct st_rip3_overflow_article *state = &overflow->articles[number];
	if (page_named(overflow, state->source, name) || page_named(overflow, state->remainder, name)) {
		int status = st_rip3_overflow_close(overflow, number);
		if (status != 0)
			return status;
	}
	for (const struct st_rip3_saved_page *page = overflow->pages; page != NULL; page = page->next)
		if (page->visible && page_named(overflow, page, name))
			return st_rip3_resource_read_scratch(result, &page->file);
	return IO_ERROR_NOT_FOUND;
}
