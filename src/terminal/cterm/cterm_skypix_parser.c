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
static bool parameters(struct cterm_skypix_parser *p)
{
	p->count = 1;
	p->parameters[0] = 0;
	for (size_t i = 2; i < p->length; ++i) {
		uint8_t c = p->bytes[i];
		if (c == ';') {
			if (p->count == 17)
				return false;
			p->parameters[p->count++] = 0;
		} else if (c >= '0' && c <= '9') {
			unsigned *value = &p->parameters[p->count - 1];
			*value = (*value * 10 + c - '0') & 65535u;
		} else
			return false;
	}
	return true;
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
				if (valid)
					status = s->command(s->context, p->parameters, p->count, p->bytes, p->length);
				p->length = 0;
			} else if (c == 0x1b) {
				p->length = 0;
				p->state = ESCAPE;
				p->bytes[p->length++] = c;
			} else if (p->length < sizeof(p->bytes))
				p->bytes[p->length++] = c;
			else
				p->state = DISCARD_STRING;
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
			bool valid = !p->invalid && parameters(p);
			if (s->begin_command != NULL)
				status = s->begin_command(s->context);
			unsigned command = valid ? p->parameters[0] : 0;
			bool string = valid && (command == 16 ||
									(command == 10 && p->count == 2 && p->parameters[1] != 0));
			p->length = 0;
			p->state = string ? STRING : PLAIN;
			if (status == 0 && valid && !string)
				status = s->command(s->context, p->parameters, p->count, NULL, 0);
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
