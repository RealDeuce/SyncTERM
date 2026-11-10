/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "cterm_skypix_parser.h"
#include <string.h>
enum { PLAIN, ESCAPE, CSI, STRING, DISCARD_STRING, OPAQUE };
static int emit(struct cterm_skypix_parser *p, const struct cterm_skypix_sink *s)
{
	int status = s->text(s->context, p->bytes, p->length);
	p->length = 0;
	return status;
}
static bool parse_numbers(struct cterm_skypix_parser *p)
{
	size_t digits = 0;
	p->number_count = 1;
	p->numbers[0] = 0;
	for (size_t i = 2; i < p->length; ++i) {
		uint8_t c = p->bytes[i];
		if (c == ';') {
			if (p->number_count == CTERM_SKYPIX_MAX_NUMBERS)
				return false;
			p->numbers[p->number_count++] = 0;
			digits = 0;
		} else if (c >= '0' && c <= '9') {
			if (digits == CTERM_SKYPIX_MAX_NUMBER_DIGITS)
				return false;
			++digits;
			unsigned *value = &p->numbers[p->number_count - 1];
			*value = (*value * 10 + c - '0') & 65535u;
		} else
			return false;
	}
	return true;
}
static bool has_string(const struct cterm_skypix_parser *p)
{
	/* The first bang ends the numeric set, not the whole command. Extra
	 * numeric fields do not change string[0]'s position or framing. */
	return p->numbers[0] == 16 || (p->numbers[0] == 10
		&& p->number_count >= 2 && p->numbers[1] != 0);
}
int cterm_skypix_parser_feed(struct cterm_skypix_parser *p, const struct cterm_skypix_sink *s,
							 const uint8_t *bytes, size_t size, size_t *used)
{
	int status = 0;
	*used = 0;
	while (*used < size && status == 0 && (s->paused == NULL || !s->paused(s->context))) {
		uint8_t c = bytes[(*used)++];
		if (p->state == OPAQUE) {
			status = s->text(s->context, &c, 1);
			if (c == 7 || (p->opaque_escape && c == '\\'))
				p->state = PLAIN;
			p->opaque_escape = c == 0x1b;
			continue;
		}
		if (p->state == STRING || p->state == DISCARD_STRING) {
			if (c == '!') {
				bool valid = p->state == STRING;
				p->state = PLAIN;
				if (valid) {
					const struct cterm_skypix_string strings[] = {{p->bytes, p->length}};
					status = s->command(s->context, p->numbers, p->number_count, strings, 1);
				}
				p->length = 0;
			} else if (c == 0x1b) {
				p->length = 0;
				p->state = ESCAPE;
				p->bytes[p->length++] = c;
			} else if (p->state == STRING) {
				if (p->length < sizeof(p->bytes))
					p->bytes[p->length++] = c;
				else
					p->state = DISCARD_STRING;
			}
			continue;
		}
		if (p->state == PLAIN) {
			if (c == 0x1b) {
				p->state = ESCAPE;
				p->bytes[p->length++] = c;
			} else
				status = s->text(s->context, &c, 1);
			continue;
		}
		if (p->state == ESCAPE) {
			p->bytes[p->length++] = c;
			if (c == '[') {
				p->state = CSI;
				p->invalid = false;
			} else {
				p->state = c == ']' || c == 'P' || c == '_' || c == '^' ? OPAQUE : PLAIN;
				p->opaque_escape = false;
				status = emit(p, s);
			}
			continue;
		}
		if (c == '!') {
			bool valid = parse_numbers(p) && !p->invalid;
			if (s->begin_command != NULL)
				status = s->begin_command(s->context);
			bool string = has_string(p);
			p->length = 0;
			p->state = string ? (valid ? STRING : DISCARD_STRING) : PLAIN;
			if (status == 0 && valid && !string)
				status = s->command(s->context, p->numbers, p->number_count, NULL, 0);
		} else if (c == 0x1b) {
			status = emit(p, s);
			p->state = ESCAPE;
			p->bytes[p->length++] = c;
		} else if (c >= 0x40 && c <= 0x7e) {
			if (p->length < sizeof(p->bytes))
				p->bytes[p->length++] = c;
			p->state = PLAIN;
			status = emit(p, s);
		} else if (p->length < sizeof(p->bytes))
			p->bytes[p->length++] = c;
		else
			p->invalid = true;
	}
	return status;
}
int cterm_skypix_parser_finish(struct cterm_skypix_parser *p, const struct cterm_skypix_sink *s)
{
	int status = p->state == ESCAPE || p->state == CSI ? emit(p, s) : 0;
	memset(p, 0, sizeof(*p));
	return status;
}
