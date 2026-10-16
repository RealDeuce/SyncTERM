/* Tests for incremental Unicode text processing. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "st_unicode_text.h"

#include "io_error.h"
#include "st_utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
		    #expression); \
		return EXIT_FAILURE; \
	} \
} while (0)

#ifndef ST_UNICODE_NORMALIZATION_TEST_PATH
#define ST_UNICODE_NORMALIZATION_TEST_PATH ""
#endif

#ifndef ST_UNICODE_GRAPHEME_TEST_PATH
#define ST_UNICODE_GRAPHEME_TEST_PATH ""
#endif

struct bytes {
	uint8_t *values;
	size_t count;
	size_t capacity;
};

static bool
append_bytes(struct bytes *bytes, const void *values, size_t count)
{
	size_t capacity;
	void *fresh;

	if (count > SIZE_MAX - bytes->count)
		return false;
	if (bytes->count + count > bytes->capacity) {
		capacity = bytes->capacity == 0 ? 32 : bytes->capacity;
		while (capacity < bytes->count + count) {
			if (capacity > SIZE_MAX / 2) {
				capacity = bytes->count + count;
				break;
			}
			capacity *= 2;
		}
		fresh = realloc(bytes->values, capacity);
		if (fresh == NULL)
			return false;
		bytes->values = fresh;
		bytes->capacity = capacity;
	}
	memcpy(bytes->values + bytes->count, values, count);
	bytes->count += count;
	return true;
}

static bool
append_scalar(struct bytes *bytes, uint32_t scalar)
{
	char encoded[4];
	ptrdiff_t count = st_utf8_encode_one(encoded, sizeof(encoded), scalar);

	return count > 0 && append_bytes(bytes, encoded, (size_t)count);
}

static bool
parse_sequence(char *field, struct bytes *bytes)
{
	char *current = field;

	while (*current != '\0') {
		char *end;
		unsigned long value;

		while (*current == ' ' || *current == '\t')
			++current;
		if (*current == '\0')
			break;
		value = strtoul(current, &end, 16);
		if (end == current || value > UINT32_C(0x10ffff)
		    || !append_scalar(bytes, (uint32_t)value))
			return false;
		current = end;
	}
	return true;
}

static bool
normalized_equals(const struct bytes *input, const struct bytes *expected)
{
	char *actual = NULL;
	size_t size = 0;
	bool changed;
	int status = st_unicode_text_normalize(&actual, &size, input->values,
	    input->count, &changed);
	bool equal = status == 0 && size == expected->count
	    && memcmp(actual, expected->values, size) == 0;

	free(actual);
	return equal;
}

static int
check_normalization_conformance(void)
{
	FILE *stream = fopen(ST_UNICODE_NORMALIZATION_TEST_PATH, "rb");
	char line[2048];
	size_t line_number = 0;

	CHECK(stream != NULL);
	while (fgets(line, sizeof(line), stream) != NULL) {
		char *fields[5];
		char *current;
		struct bytes sequences[5] = {0};

		++line_number;
		current = strchr(line, '#');
		if (current != NULL)
			*current = '\0';
		current = line;
		while (*current == ' ' || *current == '\t')
			++current;
		if (*current == '\0' || *current == '\r' || *current == '\n'
		    || *current == '@')
			continue;
		for (size_t i = 0; i < 5; ++i) {
			char *separator;

			fields[i] = current;
			separator = strchr(current, ';');
			CHECK(separator != NULL);
			*separator = '\0';
			current = separator + 1;
			CHECK(parse_sequence(fields[i], &sequences[i]));
		}
		if (!normalized_equals(&sequences[0], &sequences[1])
		    || !normalized_equals(&sequences[1], &sequences[1])
		    || !normalized_equals(&sequences[2], &sequences[1])
		    || !normalized_equals(&sequences[3], &sequences[3])
		    || !normalized_equals(&sequences[4], &sequences[3])) {
			fprintf(stderr, "normalization failure on test line %zu\n",
			    line_number);
			return EXIT_FAILURE;
		}
		for (size_t i = 0; i < 5; ++i)
			free(sequences[i].values);
	}
	CHECK(!ferror(stream));
	CHECK(line_number > 1000);
	CHECK(fclose(stream) == 0);
	return 0;
}

static int
check_grapheme_line(char *line)
{
	struct bytes input = {0};
	size_t *breaks = NULL;
	size_t break_count = 0;
	size_t break_capacity = 0;
	char *token = strtok(line, " \t\r\n");
	bool boundary = false;
	st_unicode_text_t *text = NULL;
	struct st_unicode_text_item item;
	size_t actual = 0;
	size_t expected_index = 1;
	int result = EXIT_FAILURE;

	while (token != NULL) {
		if (strcmp(token, "#") == 0)
			break;
		if (strcmp(token, "\xc3\xb7") == 0) {
			boundary = true;
			token = strtok(NULL, " \t\r\n");
			continue;
		}
		if (strcmp(token, "\xc3\x97") == 0) {
			boundary = false;
			token = strtok(NULL, " \t\r\n");
			continue;
		}
		if (token[0] == '#')
			break;
		if (boundary) {
			if (break_count == break_capacity) {
				size_t capacity = break_capacity == 0 ? 8
				    : break_capacity * 2;
				void *fresh = realloc(breaks,
				    capacity * sizeof(breaks[0]));

				if (fresh == NULL)
					goto finished;
				breaks = fresh;
				break_capacity = capacity;
			}
			breaks[break_count++] = input.count;
		}
		char *end;
		unsigned long scalar = strtoul(token, &end, 16);
		if (*end != '\0' || scalar > UINT32_C(0x10ffff)
		    || !append_scalar(&input, (uint32_t)scalar))
			goto finished;
		token = strtok(NULL, " \t\r\n");
	}
	if (boundary) {
		void *fresh = realloc(breaks, (break_count + 1) * sizeof(breaks[0]));

		if (fresh == NULL)
			goto finished;
		breaks = fresh;
		breaks[break_count++] = input.count;
	}
	if (break_count < 2 || breaks[0] != 0)
		goto finished;
	{
		size_t normalized_offset = 0;
		size_t source_begin = 0;

		for (size_t i = 1; i < break_count; ++i) {
			char *normalized = NULL;
			size_t normalized_size;
			bool changed;
			size_t source_end = breaks[i];

			if (st_unicode_text_normalize(&normalized, &normalized_size,
			    input.values + source_begin, source_end - source_begin,
			    &changed) != 0) {
				free(normalized);
				goto finished;
			}
			free(normalized);
			normalized_offset += normalized_size;
			breaks[i] = normalized_offset;
			source_begin = source_end;
		}
	}
	if (st_unicode_text_create(&text, ST_UNICODE_TEXT_GRAPHEMES) != 0)
		goto finished;
	for (size_t i = 0; i < input.count; ++i) {
		if (st_unicode_text_feed_byte(text, input.values[i]) != 0)
			goto finished;
	}
	if (st_unicode_text_finish(text) != 0)
		goto finished;
	while (st_unicode_text_peek(text, &item) == 0) {
		if (item.kind != ST_UNICODE_TEXT_ITEM_TEXT
		    || expected_index >= break_count)
			goto finished;
		actual += item.size;
		if (actual != breaks[expected_index++])
			goto finished;
		(void)st_unicode_text_consume(text);
	}
	if (expected_index != break_count || actual != breaks[break_count - 1])
		goto finished;
	result = EXIT_SUCCESS;
finished:
	st_unicode_text_destroy(&text);
	free(input.values);
	free(breaks);
	return result;
}

static int
check_grapheme_conformance(void)
{
	FILE *stream = fopen(ST_UNICODE_GRAPHEME_TEST_PATH, "rb");
	char line[4096];
	size_t tests = 0;
	size_t line_number = 0;

	CHECK(stream != NULL);
	while (fgets(line, sizeof(line), stream) != NULL) {
		char *current = line;

		++line_number;
		while (*current == ' ' || *current == '\t')
			++current;
		if (*current == '\0' || *current == '\r' || *current == '\n'
		    || *current == '#')
			continue;
		if (check_grapheme_line(current) != EXIT_SUCCESS) {
			fprintf(stderr, "grapheme failure on test line %zu\n",
			    line_number);
			return EXIT_FAILURE;
		}
		++tests;
	}
	CHECK(!ferror(stream));
	CHECK(tests > 500);
	CHECK(fclose(stream) == 0);
	return 0;
}

static int
check_normalize(void)
{
	static const char decomposed[] = "A\xcc\x8a\xcc\x81";
	static const char composed[] = "\xc7\xba";
	char *output = NULL;
	size_t size = 0;
	bool changed = false;

	CHECK(st_unicode_text_normalize(&output, &size, decomposed,
	    sizeof(decomposed) - 1, &changed) == 0);
	CHECK(changed && size == sizeof(composed) - 1
	    && memcmp(output, composed, size) == 0);
	free(output);
	output = NULL;
	changed = true;
	CHECK(st_unicode_text_normalize(&output, &size, composed,
	    sizeof(composed) - 1, &changed) == 0);
	CHECK(!changed && size == sizeof(composed) - 1
	    && memcmp(output, composed, size) == 0);
	free(output);
	output = NULL;
	CHECK(st_unicode_text_normalize(&output, &size, composed,
	    sizeof(composed) - 1, NULL) == 0);
	CHECK(size == sizeof(composed) - 1
	    && memcmp(output, composed, size) == 0);
	free(output);
	output = NULL;
	CHECK(st_unicode_text_normalize(&output, &size, "\xc0\x80", 2,
	    &changed) == IO_ERROR_INVALID_ENCODING);
	return 0;
}

static int
check_graphemes(void)
{
	static const uint8_t input[] = {
		0x1b, 'A', 0xcc, 0x81,
		0xf0, 0x9f, 0x91, 0x8d, 0xf0, 0x9f, 0x8f, 0xbd,
	};
	st_unicode_text_t *text = NULL;
	struct st_unicode_text_item item;
	size_t count = 0;
	size_t consumed;

	CHECK(st_unicode_text_create(&text, ST_UNICODE_TEXT_GRAPHEMES) == 0);
	for (size_t i = 0; i < sizeof(input); ++i)
		CHECK(st_unicode_text_feed_byte(text, input[i]) == 0);
	CHECK(st_unicode_text_finish(text) == 0);
	while (st_unicode_text_peek(text, &item) == 0) {
		if (count == 0)
			CHECK(item.kind == ST_UNICODE_TEXT_ITEM_TEXT
			    && item.size == 1 && item.bytes[0] == 0x1b);
		if (count == 1)
			CHECK(item.size == 2 && item.bytes[0] == 0xc3
			    && item.bytes[1] == 0x81 && !item.rgi_emoji);
		if (count == 2)
			CHECK(item.size == 8 && item.rgi_emoji && item.columns == 2);
		++count;
		CHECK(st_unicode_text_consume(text) == 0);
	}
	CHECK(count == 3);
	st_unicode_text_reset(text);
	CHECK(st_unicode_text_feed(text, input, sizeof(input), &consumed) == 0
	    && consumed == sizeof(input));
	CHECK(st_unicode_text_finish(text) == 0);
	st_unicode_text_destroy(&text);
	return 0;
}

static int
check_errors(void)
{
	static const uint8_t input[] = {'A', 0xe0, 0x80, 'B', 0xf0, 0x9f};
	st_unicode_text_t *text = NULL;
	struct st_unicode_text_item item;
	size_t text_items = 0;
	size_t errors = 0;

	CHECK(st_unicode_text_create(&text, ST_UNICODE_TEXT_GRAPHEMES) == 0);
	for (size_t i = 0; i < sizeof(input); ++i)
		CHECK(st_unicode_text_feed_byte(text, input[i]) == 0);
	CHECK(st_unicode_text_finish(text) == 0);
	CHECK((st_unicode_text_state(text)
	    & ST_UNICODE_TEXT_SAW_DECODE_ERROR) != 0);
	while (st_unicode_text_peek(text, &item) == 0) {
		if (item.kind == ST_UNICODE_TEXT_ITEM_DECODE_ERROR) {
			if (errors == 0)
				CHECK(item.size == 1 && item.bytes[0] == 0xe0);
			else if (errors == 1)
				CHECK(item.size == 1 && item.bytes[0] == 0x80);
			else if (errors == 2)
				CHECK(item.size == 2 && item.bytes[0] == 0xf0
				    && item.bytes[1] == 0x9f);
			++errors;
		}
		else
			++text_items;
		CHECK(st_unicode_text_consume(text) == 0);
	}
	CHECK(errors == 3 && text_items == 2);
	st_unicode_text_destroy(&text);
	return 0;
}

struct presentation_log {
	struct {
		enum st_unicode_presentation_event_kind kind;
		uint8_t bytes[8];
		uint8_t columns;
		size_t size;
	} events[8];
	size_t count;
};

static int
record_presentation(void *context,
	const struct st_unicode_presentation_event *event)
{
	struct presentation_log *log = context;

	if (log->count >= sizeof(log->events) / sizeof(log->events[0])
	    || event->size > sizeof(log->events[0].bytes))
		return IO_ERROR_OUT_OF_RANGE;
	log->events[log->count].kind = event->kind;
	log->events[log->count].columns = event->columns;
	log->events[log->count].size = event->size;
	memcpy(log->events[log->count].bytes, event->bytes, event->size);
	++log->count;
	return 0;
}

static int
check_zero_width(void)
{
	static const struct {
		const char *bytes;
		uint8_t columns;
	} cases[] = {
		{"\xe2\x81\xa0", 0}, {"\xef\xbb\xbf", 0},
		{"\xef\xb8\x8f", 0}, {"\xe2\x80\x8d", 0},
		{"\xcc\x81", 1}, {"\xe2\x83\x9d", 1},
		{"A\xcc\x81", 1},
		{"\xe1\x84\x80\xe1\x85\xa0", 2},
		{"\xd8\x80" "A", 1},
		{"\xf0\x9f\x8f\xbd", 2},
		{"\xe0\xa6\xbe", 1},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		st_unicode_text_t *text = NULL;
		st_unicode_presentation_t *presentation = NULL;
		struct st_unicode_text_item item;
		struct presentation_log log = {0};
		size_t size = strlen(cases[i].bytes);
		size_t consumed;

		CHECK(st_unicode_text_create(&text, ST_UNICODE_TEXT_GRAPHEMES) == 0);
		CHECK(st_unicode_text_feed(text, cases[i].bytes, size, &consumed) == 0);
		CHECK(consumed == size && st_unicode_text_finish(text) == 0);
		CHECK(st_unicode_text_peek(text, &item) == 0);
		CHECK(item.columns == cases[i].columns);
		CHECK(st_unicode_presentation_create(&presentation,
		    record_presentation, &log) == 0);
		for (size_t j = 0; j < size; ++j) {
			CHECK(st_unicode_presentation_feed(presentation,
			    cases[i].bytes + j, 1, &consumed) == 0 && consumed == 1);
		}
		CHECK(log.count > 0);
		CHECK(log.events[log.count - 1].columns == item.columns);
		CHECK(log.events[log.count - 1].size == item.size);
		CHECK(memcmp(log.events[log.count - 1].bytes, item.bytes, item.size) == 0);
		if (i == 8) {
			CHECK(log.count == 2 && log.events[0].columns == 0);
			CHECK(log.events[1].kind == ST_UNICODE_PRESENTATION_REVISE);
		}
		CHECK(st_unicode_text_consume(text) == 0);
		CHECK(st_unicode_text_peek(text, &item) != 0);
		CHECK(st_unicode_presentation_boundary(presentation) == 0);
		st_unicode_presentation_destroy(&presentation);
		st_unicode_text_destroy(&text);
	}
	return 0;
}

static int
check_provisional_presentation(void)
{
	static const uint8_t acute[] = {0xcc, 0x81};
	static const uint8_t composed[] = {0xc3, 0xa9};
	struct presentation_log log = {0};
	st_unicode_presentation_t *presentation = NULL;
	size_t consumed;

	CHECK(st_unicode_presentation_create(&presentation,
	    record_presentation, &log) == 0);
	CHECK(st_unicode_presentation_feed(presentation, "e", 1, &consumed) == 0
	    && consumed == 1);
	CHECK(log.count == 1
	    && log.events[0].kind == ST_UNICODE_PRESENTATION_BEGIN
	    && log.events[0].size == 1 && log.events[0].bytes[0] == 'e');
	CHECK(st_unicode_presentation_feed(presentation, acute, 1, &consumed) == 0
	    && consumed == 1 && log.count == 1);
	CHECK(st_unicode_presentation_feed(presentation, acute + 1, 1,
	    &consumed) == 0 && consumed == 1);
	CHECK(log.count == 2
	    && log.events[1].kind == ST_UNICODE_PRESENTATION_REVISE
	    && log.events[1].size == sizeof(composed)
	    && memcmp(log.events[1].bytes, composed, sizeof(composed)) == 0);
	CHECK(st_unicode_presentation_boundary(presentation) == 0);
	CHECK(log.count == 3
	    && log.events[2].kind == ST_UNICODE_PRESENTATION_COMMIT);
	CHECK(st_unicode_presentation_feed(presentation, "\xf0\x9f", 2,
	    &consumed) == 0 && consumed == 2);
	CHECK(st_unicode_presentation_boundary(presentation) == 0);
	CHECK(log.count == 4
	    && log.events[3].kind == ST_UNICODE_PRESENTATION_DECODE_ERROR
	    && log.events[3].size == 2);
	CHECK(st_unicode_presentation_boundary(presentation) == 0
	    && log.count == 4);
	st_unicode_presentation_destroy(&presentation);
	return 0;
}

int
main(void)
{
	CHECK(check_normalize() == 0);
	CHECK(check_graphemes() == 0);
	CHECK(check_errors() == 0);
	CHECK(check_provisional_presentation() == 0);
	CHECK(check_zero_width() == 0);
	CHECK(check_normalization_conformance() == 0);
	CHECK(check_grapheme_conformance() == 0);
	return EXIT_SUCCESS;
}
