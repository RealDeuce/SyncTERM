/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_graphics_parser.h"
#include "io_error.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum parser_state { OUTSIDE, COMMAND, CHAIN, FIELD, SUSPENDED };
enum number_state { DIGITS, CONTINUATION, SYMBOL_DELIMITER };

struct st_graphics_parser {
	struct st_graphics_parser_config config;
	enum parser_state state;
	uint8_t prefix[8];
	size_t prefix_size;
	struct st_graphics_command_view view;
	const struct st_graphics_command *command;
	struct st_graphics_argument *arguments;
	uint8_t *payload;
	size_t payload_size, field_start, field_size;
	struct st_graphics_parameter parameter;
	bool field_active, boundary_needed, number_requested, entry_needed;
	enum number_state number_state;
	unsigned continuation_left, digits;
	bool number_invalid;
	struct st_graphics_operand operand;
};

static bool
valid_parameter(const struct st_graphics_parameter *parameter)
{
	return parameter->kind >= ST_GRAPHICS_NUMBER
	    && parameter->kind <= ST_GRAPHICS_BYTES
	    && parameter->axis >= ST_GRAPHICS_UNSCALED
	    && parameter->axis <= ST_GRAPHICS_Y
	    && (parameter->kind != ST_GRAPHICS_TEXT || parameter->size != 0);
}

static int
synchronous_status(int status)
{
	return status == IO_ERROR_IN_PROGRESS ? IO_ERROR_PROTOCOL : status;
}

void
st_graphics_parser_cancel(st_graphics_parser_t *p)
{
	if (p == NULL)
		return;
	p->state = OUTSIDE;
	p->prefix_size = 0;
	p->view = (struct st_graphics_command_view){.arguments = p->arguments};
	p->command = NULL;
	p->payload_size = 0;
	p->field_active = false;
	p->boundary_needed = false;
	p->number_requested = false;
	p->entry_needed = false;
}

int
st_graphics_parser_create(st_graphics_parser_t **result,
    const struct st_graphics_parser_config *config)
{
	st_graphics_parser_t *p;
	if (result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*result = NULL;
	if (config == NULL || config->dialect == NULL
	    || config->dialect->attention == NULL
	    || config->dialect->attention_size == 0
	    || config->dialect->attention_size > 8
	    || config->dialect->number_digits == 0
	    || config->dialect->number_digits > 9
	    || config->max_arguments == 0
	    || (config->command_count != 0 && config->commands == NULL))
		return IO_ERROR_INVALID_ARGUMENT;
	if (config->max_arguments > SIZE_MAX / sizeof(*p->arguments))
		return IO_ERROR_OUT_OF_RANGE;
	for (size_t i = 0; i < config->command_count; ++i) {
		const struct st_graphics_command *c = &config->commands[i];
		if (c->count != 0 && c->parameters == NULL)
			return IO_ERROR_INVALID_ARGUMENT;
		for (size_t j = 0; j < c->count; ++j) {
			if (!valid_parameter(&c->parameters[j]))
				return IO_ERROR_INVALID_ARGUMENT;
		}
		for (size_t j = 0; j < i; ++j) {
			if (config->commands[j].code == c->code)
				return IO_ERROR_INVALID_ARGUMENT;
		}
	}
	p = calloc(1, sizeof(*p));
	if (p == NULL)
		return IO_ERROR_OUT_OF_MEMORY;
	p->config = *config;
	p->arguments = calloc(config->max_arguments, sizeof(*p->arguments));
	if (config->max_payload != 0)
		p->payload = malloc(config->max_payload);
	if (p->arguments == NULL || (config->max_payload != 0 && p->payload == NULL)) {
		st_graphics_parser_destroy(p);
		return IO_ERROR_OUT_OF_MEMORY;
	}
	st_graphics_parser_cancel(p);
	*result = p;
	return 0;
}

void
st_graphics_parser_destroy(st_graphics_parser_t *p)
{
	if (p == NULL)
		return;
	free(p->arguments);
	free(p->payload);
	free(p);
}

bool
st_graphics_parser_in_command(const st_graphics_parser_t *p)
{
	return p != NULL && p->state != OUTSIDE;
}

static int
valid_sink(const struct st_graphics_parser_sink *sink)
{
	return sink != NULL && sink->text != NULL && sink->command != NULL;
}

int
st_graphics_parser_begin(st_graphics_parser_t *p,
    const struct st_graphics_parser_sink *sink)
{
	int status;
	if (p == NULL || !valid_sink(sink))
		return IO_ERROR_INVALID_ARGUMENT;
	if (p->state != OUTSIDE || p->prefix_size != 0)
		return IO_ERROR_BUSY;
	status = sink->boundary == NULL ? 0
	    : synchronous_status(sink->boundary(sink->context));
	if (status != 0)
		return status;
	p->state = COMMAND;
	return 0;
}

static const struct st_graphics_command *
find_command(const st_graphics_parser_t *p, uint8_t code)
{
	for (size_t i = 0; i < p->config.command_count; ++i) {
		if (p->config.commands[i].code == code)
			return &p->config.commands[i];
	}
	return NULL;
}

int
st_graphics_parser_start_command(st_graphics_parser_t *p,
    const struct st_graphics_parser_sink *sink, uint8_t code, bool chained)
{
	if (p == NULL || !valid_sink(sink))
		return IO_ERROR_INVALID_ARGUMENT;
	if ((p->state != OUTSIDE && p->state != SUSPENDED) || p->prefix_size != 0)
		return IO_ERROR_BUSY;
	const struct st_graphics_command *command = find_command(p, code);
	if (command == NULL && sink->unknown_command == NULL)
		return IO_ERROR_NOT_SUPPORTED;
	int status = sink->boundary == NULL ? 0
	    : synchronous_status(sink->boundary(sink->context));
	if (status != 0)
		return status;
	/* Commit selection only after validation/boundary. A failed selection
	 * preserves the suspended view and its borrowed payload. */
	st_graphics_parser_cancel(p);
	p->view.code = code;
	p->view.chained = chained;
	p->command = command;
	p->state = FIELD;
	p->entry_needed = true;
	return 0;
}

static void
after_command(st_graphics_parser_t *p)
{
	bool chained = p->view.chained;
	st_graphics_parser_cancel(p);
	if (chained) {
		p->state = COMMAND;
		p->boundary_needed = true;
	}
}

int
st_graphics_parser_resume(st_graphics_parser_t *p)
{
	if (p == NULL || p->state != SUSPENDED)
		return IO_ERROR_INVALID_ARGUMENT;
	after_command(p);
	return 0;
}

int
st_graphics_parser_take_continuation(st_graphics_parser_t *p,
    struct st_graphics_continuation *continuation)
{
	if (p == NULL || continuation == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (p->state != SUSPENDED) return IO_ERROR_BUSY;
	continuation->chained = p->view.chained;
	st_graphics_parser_cancel(p);
	return 0;
}

int
st_graphics_parser_restore_continuation(st_graphics_parser_t *p,
    const struct st_graphics_continuation *continuation)
{
	if (p == NULL || continuation == NULL) return IO_ERROR_INVALID_ARGUMENT;
	if (p->state != OUTSIDE || p->prefix_size != 0) return IO_ERROR_BUSY;
	p->view.chained = continuation->chained;
	p->state = SUSPENDED;
	return 0;
}

static void
append_field(st_graphics_parser_t *p)
{
	struct st_graphics_argument *a = &p->arguments[p->view.count++];
	*a = (struct st_graphics_argument){
		.kind = p->parameter.kind, .axis = p->parameter.axis,
		.operand = p->operand,
	};
	if (a->kind != ST_GRAPHICS_NUMBER) {
		a->bytes = p->payload == NULL ? NULL : p->payload + p->field_start;
		a->size = p->field_size;
	}
	p->field_active = false;
}

/* Exhaust input-free transitions before yielding. This makes empty fields,
 * command completion and suspension independent of network chunk boundaries. */
static int
advance(st_graphics_parser_t *p, const struct st_graphics_parser_sink *sink)
{
	int status;
	for (;;) {
		if (p->state == SUSPENDED)
			return IO_ERROR_IN_PROGRESS;
		if (p->state == COMMAND && p->boundary_needed) {
			p->boundary_needed = false;
			if (sink->boundary != NULL) {
				status = synchronous_status(sink->boundary(sink->context));
				if (status != 0)
					return status;
			}
		}
		if (p->state != FIELD)
			return 0;
		if (p->entry_needed) {
			status = sink->entry == NULL ? 0 : sink->entry(sink->context, &p->view);
			if (status != 0) return status;
			p->entry_needed = false;
		}
		if (p->field_active) {
			if (p->parameter.kind == ST_GRAPHICS_NUMBER && !p->number_requested) {
				bool supplied = false;
				int value = 0;
				status = sink->number == NULL ? 0 : sink->number(sink->context,
				    &p->view, &p->parameter, &value, &supplied);
				if (status == IO_ERROR_IN_PROGRESS && supplied)
					return IO_ERROR_PROTOCOL;
				if (status != 0)
					return status;
				if (supplied) {
					append_field(p);
					p->arguments[p->view.count-1].has_value = true;
					p->arguments[p->view.count-1].value = value;
					continue;
				}
				/* Once declined, this field belongs to the wire even if
				 * the owner changes its source during a partial token. */
				p->number_requested = true;
			}
			return 0;
		}
		if (p->command == NULL) {
			if (sink->unknown != NULL)
				sink->unknown(sink->context, p->view.code);
			if (sink->unknown_command != NULL) {
				status=sink->unknown_command(sink->context,&p->view);
				if (status==IO_ERROR_IN_PROGRESS) { p->state=SUSPENDED; return status; }
				if (status!=0) return status;
			}
			after_command(p);
			continue;
		}
		struct st_graphics_parameter parameter = {.kind = ST_GRAPHICS_END};
		if (p->view.count < p->command->count)
			parameter = p->command->parameters[p->view.count];
		else if (p->command->next != NULL) {
			status = synchronous_status(p->command->next(sink->context,
			    &p->view, &parameter));
			if (status != 0)
				return status;
		}
		if (parameter.kind == ST_GRAPHICS_END) {
			status = sink->command(sink->context, &p->view);
			if (status == IO_ERROR_IN_PROGRESS) {
				p->state = SUSPENDED;
				return status;
			}
			if (status != 0)
				return status;
			after_command(p);
			continue;
		}
		if (!valid_parameter(&parameter))
			return IO_ERROR_INVALID_ARGUMENT;
		if (p->view.count == p->config.max_arguments
		    || (parameter.kind != ST_GRAPHICS_NUMBER
		        && parameter.size > p->config.max_payload - p->payload_size))
			return IO_ERROR_OUT_OF_RANGE;
		p->parameter = parameter;
		p->field_active = true;
		p->number_requested = false;
		p->field_start = p->payload_size;
		p->field_size = 0;
		p->number_state = DIGITS;
		p->digits = 0;
		p->number_invalid = false;
		p->operand = (struct st_graphics_operand){0};
		if (sink->field != NULL) {
			status = synchronous_status(sink->field(sink->context, &p->view, &p->parameter));
			if (status != 0) return status;
		}
		if (parameter.kind == ST_GRAPHICS_BYTES && parameter.size == 0)
			append_field(p);
		/* Offer a NUMBER source before consuming even the first wire byte. */
	}
}

static int
raw_bytes(st_graphics_parser_t *p, const struct st_graphics_parser_sink *sink,
    const uint8_t *bytes, size_t size, size_t *accepted)
{
	size_t count = p->parameter.size-p->field_size;
	if (count > size) count = size;
	*accepted = count;
	bool end = false;
	int status = sink->raw == NULL ? 0 : synchronous_status(sink->raw(sink->context,
	    &p->view,&p->parameter,p->field_size,bytes,count,accepted,&end));
	if (*accepted > count || (status == 0 && *accepted == 0 && !end)) {
		*accepted = 0;
		return IO_ERROR_PROTOCOL;
	}
	if (*accepted != 0) memcpy(p->payload+p->payload_size,bytes,*accepted);
	p->payload_size += *accepted;
	p->field_size += *accepted;
	if (status == 0 && (end || p->field_size == p->parameter.size)) append_field(p);
	return status;
}

static int
read_number(st_graphics_parser_t *p, const struct st_graphics_parser_sink *sink,
    const uint8_t *bytes, size_t size, size_t *accepted, bool *complete)
{
	int value = 0;
	*accepted = 0;
	*complete = false;
	int status = sink->read_number(sink->context, &p->view, &p->parameter,
	    bytes, size, accepted, &value, complete);
	if (*accepted > size) {
		*accepted = 0;
		return IO_ERROR_PROTOCOL;
	}
	if ((*complete && status != 0)
	    || (status == 0 && size != 0 && *accepted == 0 && !*complete))
		return IO_ERROR_PROTOCOL;
	if (status == 0 && *complete) {
		append_field(p);
		p->arguments[p->view.count-1].has_value = true;
		p->arguments[p->view.count-1].value = value;
	}
	return status;
}

int
st_graphics_parser_end_bytes(st_graphics_parser_t *p,
    const struct st_graphics_parser_sink *sink)
{
	if (p == NULL || !valid_sink(sink) || p->state != FIELD || !p->field_active
	    || p->parameter.kind != ST_GRAPHICS_BYTES)
		return IO_ERROR_INVALID_ARGUMENT;
	append_field(p);
	int status = advance(p,sink);
	if (status != 0 && status != IO_ERROR_IN_PROGRESS) st_graphics_parser_cancel(p);
	return status;
}

int
st_graphics_parser_end_text(st_graphics_parser_t *p,
    const struct st_graphics_parser_sink *sink)
{
	if (p == NULL || !valid_sink(sink) || p->state != FIELD || !p->field_active
	    || p->parameter.kind != ST_GRAPHICS_TEXT)
		return IO_ERROR_INVALID_ARGUMENT;
	append_field(p);
	p->arguments[p->view.count-1].ended_early = true;
	int status = advance(p, sink);
	if (status != 0 && status != IO_ERROR_IN_PROGRESS) st_graphics_parser_cancel(p);
	return status;
}

static int
outside_byte(st_graphics_parser_t *p, const struct st_graphics_parser_sink *sink,
    uint8_t byte)
{
	const struct st_graphics_dialect *d = p->config.dialect;
	int status;
	p->prefix[p->prefix_size++] = byte;
	/* Keep the longest suffix which is a prefix of attention. This handles
	 * overlap without interpreting any byte twice or losing a held prefix. */
	while (memcmp(p->prefix, d->attention, p->prefix_size) != 0) {
		status = synchronous_status(sink->text(sink->context, p->prefix, 1));
		if (status != 0)
			return status;
		--p->prefix_size;
		memmove(p->prefix, p->prefix + 1, p->prefix_size);
	}
	if (p->prefix_size == d->attention_size) {
		p->prefix_size = 0;
		return st_graphics_parser_begin(p, sink);
	}
	return 0;
}

static int
number_byte(st_graphics_parser_t *p, uint8_t byte)
{
	const struct st_graphics_dialect *d = p->config.dialect;
	if (p->number_state == CONTINUATION) {
		if (--p->continuation_left == 0)
			p->number_state = DIGITS;
		return 0;
	}
	if (p->number_state == SYMBOL_DELIMITER) {
		append_field(p);
		return 0;
	}
	if (d->number_skip_crlf && (byte == '\r' || byte == '\n'))
		return 0;
	if (byte >= '0' && byte <= '9') {
		if (p->digits == d->number_digits
		    || p->operand.value > (UINT_MAX - (unsigned)(byte - '0')) / 10)
			p->number_invalid = true;
		else {
			++p->digits;
			p->operand.value = p->operand.value * 10 + (unsigned)(byte - '0');
		}
		return 0;
	}
	if (p->number_invalid)
		return IO_ERROR_OUT_OF_RANGE;
	if (byte == '_') {
		p->number_state = CONTINUATION;
		p->continuation_left = 2;
		return 0;
	}
	if (byte == '+' || byte == '-' || byte == '!') {
		/* Prefixes are typed loop expressions, not signed decimal. Reject
		 * embedded/repeated prefixes instead of the original digit-slot bug. */
		if (p->digits != 0 || p->operand.operation != ST_GRAPHICS_VALUE)
			return IO_ERROR_PROTOCOL;
		p->operand.operation = byte == '+' ? ST_GRAPHICS_STEP_PLUS
		    : byte == '-' ? ST_GRAPHICS_STEP_MINUS : ST_GRAPHICS_MINUS_STEP;
		return 0;
	}
	if (byte == 'x' || byte == 'y' || byte == 'r' || byte == 'R') {
		if (p->digits != 0 || p->operand.operation != ST_GRAPHICS_VALUE)
			return IO_ERROR_PROTOCOL;
		p->operand.source = byte == 'x' ? ST_GRAPHICS_LOOP_X
		    : byte == 'y' ? ST_GRAPHICS_LOOP_Y
		    : byte == 'r' ? ST_GRAPHICS_RANDOM_R : ST_GRAPHICS_RANDOM_R2;
		p->number_state = SYMBOL_DELIMITER;
		return 0;
	}
	if (byte == ']') {
		p->operand.source = ST_GRAPHICS_ARRAY;
		p->number_state = SYMBOL_DELIMITER;
		return 0;
	}
	append_field(p);
	return 0;
}

static int
field_byte(st_graphics_parser_t *p, uint8_t byte)
{
	if (p->parameter.kind == ST_GRAPHICS_NUMBER)
		return number_byte(p, byte);
	if (p->parameter.kind == ST_GRAPHICS_TEXT) {
		if (p->parameter.skip_crlf && (byte == '\r' || byte == '\n'))
			return 0;
		if (byte == p->parameter.terminator) {
			if (p->field_size == 0 && p->parameter.empty_is_terminator) {
				p->payload[p->payload_size++] = byte;
				++p->field_size;
			}
			append_field(p);
			return 0;
		}
	}
	p->payload[p->payload_size++] = byte;
	if (++p->field_size == p->parameter.size)
		append_field(p);
	return 0;
}

int
st_graphics_parser_feed(st_graphics_parser_t *p,
    const struct st_graphics_parser_sink *sink, const uint8_t *bytes,
    size_t size, size_t *consumed)
{
	int status;
	if (consumed == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	*consumed = 0;
	if (p == NULL || !valid_sink(sink) || (bytes == NULL && size != 0))
		return IO_ERROR_INVALID_ARGUMENT;
	for (;;) {
		status = advance(p, sink);
		if (status != 0)
			break;
		if (p->state == FIELD && p->field_active
		    && p->parameter.kind == ST_GRAPHICS_NUMBER && sink->read_number != NULL) {
			size_t accepted, available = size-*consumed;
			bool complete;
			status = read_number(p, sink, available == 0 ? NULL : bytes+*consumed,
			    available, &accepted, &complete);
			*consumed += accepted;
			if (status != 0 || (!complete && *consumed == size)) break;
			continue;
		}
		if (*consumed == size) break;
		if (p->state == OUTSIDE && p->prefix_size == 0
		    && bytes[*consumed] != p->config.dialect->attention[0]) {
			size_t start = *consumed;
			do {
				++*consumed;
			} while (*consumed < size
			    && bytes[*consumed] != p->config.dialect->attention[0]);
			status = synchronous_status(sink->text(sink->context,
			    bytes + start, *consumed - start));
			if (status != 0)
				break;
			continue;
		}
		if (p->state == FIELD && p->field_active && p->parameter.kind == ST_GRAPHICS_BYTES) {
			size_t accepted;
			status = raw_bytes(p,sink,bytes+*consumed,size-*consumed,&accepted);
			*consumed += accepted;
			if (status != 0) break;
			continue;
		}
		uint8_t byte = bytes[(*consumed)++];
		switch (p->state) {
		case OUTSIDE:
			status = outside_byte(p, sink, byte);
			break;
		case COMMAND:
			p->view.code = byte;
			p->command = find_command(p, byte);
			p->state = p->config.dialect->chain_indicator ? CHAIN : FIELD;
			p->entry_needed = true;
			break;
		case CHAIN:
			p->view.chained = byte == p->config.dialect->chain;
			p->state = FIELD;
			break;
		case FIELD:
			status = field_byte(p, byte);
			break;
		case SUSPENDED:
			/* advance() returns before consuming any suspended input. */
			status = IO_ERROR_IN_PROGRESS;
			break;
		}
		if (status != 0)
			break;
	}
	if (status != 0 && status != IO_ERROR_IN_PROGRESS)
		st_graphics_parser_cancel(p);
	return status;
}

int
st_graphics_parser_finish(st_graphics_parser_t *p,
    const struct st_graphics_parser_sink *sink)
{
	int status = 0;
	if (p == NULL || !valid_sink(sink))
		return IO_ERROR_INVALID_ARGUMENT;
	if (p->state == SUSPENDED)
		return IO_ERROR_IN_PROGRESS;
	if (p->state != OUTSIDE)
		status = IO_ERROR_PROTOCOL;
	else if (p->prefix_size != 0)
		status = synchronous_status(sink->text(sink->context,
		    p->prefix, p->prefix_size));
	st_graphics_parser_cancel(p);
	return status;
}

unsigned
st_graphics_parser_header_remaining(const st_graphics_parser_t *p)
{
	if (p == NULL) return 0;
	return p->state == COMMAND ? (p->config.dialect->chain_indicator ? 2u : 1u)
	    : p->state == CHAIN ? 1u : 0u;
}

int
st_graphics_parser_complete_header(st_graphics_parser_t *p,
    const struct st_graphics_parser_sink *sink, uint8_t missing_code, bool chained)
{
	if (p == NULL || !valid_sink(sink)) return IO_ERROR_INVALID_ARGUMENT;
	if (!st_graphics_parser_header_remaining(p)) return IO_ERROR_BUSY;
	/* A chained source may still owe its text boundary. Finish it before
	 * selecting any fallback, just as the ordinary byte path does. */
	int status = advance(p,sink);
	if (status == 0) {
		if (p->state == COMMAND) {
			p->view.code = missing_code;
			p->command = find_command(p,missing_code);
		}
		p->view.chained = chained;
		p->state = FIELD;
		p->entry_needed = true;
		size_t used;
		return st_graphics_parser_feed(p,sink,NULL,0,&used);
	}
	st_graphics_parser_cancel(p);
	return status;
}
