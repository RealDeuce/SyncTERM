/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_resource.h"
#include "st_ddofx_script.h"
#include "st_time.h"
#include "string_util.h"

enum {
	VARIABLE_COUNT  = 1000,
	VARIABLE_LENGTH = 50,
	RETURN_COUNT    = 300,
	MACRO_COUNT     = 32,
	MACRO_LINES     = 10,
	LINE_LENGTH     = 200
};

struct variable {
	char text[VARIABLE_LENGTH + 1];
	size_t length;

	enum {
		UNDEFINED,
		TEXT,
		NUMERIC
	} type;
};

enum { LESS = 1, EQUAL = 2, GREATER = 4 };

struct predicate {
	char master[ST_DDOFX_BODY_MAX + 1], slave[ST_DDOFX_BODY_MAX + 1];
	unsigned relations;
};

struct macro_frame {
	struct macro_frame    *parent;
	struct st_ddofx_framer framer;
	char                   source[ST_DDOFX_BODY_MAX + 1];
	uint8_t               *resource;
	bool                   resource_text;
	size_t                 position, length;
	int                    macro, next_line, end_line;
	int                    remaining;
	bool                   until;
	struct predicate       condition;
	uint8_t                after_reply[ST_DDOFX_FIELD_REPLY_MAX];
	size_t                 after_reply_length;
	bool                   submitted_by_key;
	enum st_ddofx_callback_kind callback;
	bool saved_in_event;
	uint64_t               saved_wait;
	unsigned                    pressed_button;
	unsigned                    collision_animation;
	enum st_ddofx_group_kind   group_kind;
	unsigned group_anchor, group_next; /* Anchor is one-based, zero for ordinary macro frames. */
};

struct st_ddofx_script {
	struct variable variables[VARIABLE_COUNT];

	struct {
		uint8_t bytes[LINE_LENGTH];
		size_t  length;
	} returns[RETURN_COUNT];
	char                macros[MACRO_COUNT][MACRO_LINES][LINE_LENGTH + 1];
	bool                macro_resource_text[MACRO_COUNT][MACRO_LINES];
	struct macro_frame *top;
	unsigned            depth;
	int                 capture;
	uint32_t            random;
};

static void
copy(char *dest, const char *source, size_t limit)
{
	size_t length = strlen(source);
	if (length > limit)
		length = limit;
	memmove(dest, source, length);
	dest[length] = 0;
}

static void
set_value(struct variable *v, const void *data, size_t size)
{
	v->length = size < VARIABLE_LENGTH ? size : VARIABLE_LENGTH;
	memmove(v->text, data, v->length);
	v->text[v->length] = 0;
}

/* Interpret a DOS long's 32 bits without an out-of-range signed conversion. */
static int32_t
signed_long(uint32_t bits)
{
	return bits > INT32_MAX ? (int64_t)bits - INT64_C(4294967296) : (int64_t)bits;
}

static bool
integer(const char *s, int32_t *result)
{
	while (*s == ' ')
		++s;
	bool negative = *s == '-';
	if (negative || *s == '+')
		++s;
	bool hex = *s == '$';
	if (hex)
		++s;
	size_t digits = strspn(s, hex ? "0123456789abcdefABCDEF" : "0123456789");
	if (!digits || s[digits])
		return false;

	errno = 0;
	uintmax_t magnitude = strtoumax(s, NULL, hex ? 16 : 10);
	uintmax_t limit = hex ? UINT32_MAX : (uintmax_t)INT32_MAX + negative;
	if (errno == ERANGE || magnitude > limit)
		return false;
	uint32_t bits = (uint32_t)magnitude;
	*result = signed_long(negative ? 0u - bits : bits);
	return true;
}

static int32_t
variable_integer(const struct variable *v)
{
	int32_t n = 0;
	if (!memchr(v->text, 0, v->length))
		integer(v->text, &n);
	return n;
}

static struct variable *
variable(struct st_ddofx_script *script, int32_t id)
{
	return script != NULL && id >= 1 && id <= VARIABLE_COUNT ? &script->variables[id - 1] : NULL;
}

static bool
variable_id(const char *field, int32_t *id)
{
	return !ascii_case_ncompare(field, "DEFVAR", 6) && integer(field + 6, id);
}

static struct variable *
reference(const struct st_ddofx *ddo, const char *s)
{
	int32_t id;
	return variable_id(s, &id) ? variable(ddo->script, id) : NULL;
}

const char *
st_ddofx_value_bytes(const struct st_ddofx *ddo, const char *field, size_t *length)
{
	/* Only DEFVAR followed by a variable number is a reference. */
	int32_t id;
	if (!variable_id(field, &id)) {
		if (length)
			*length = strlen(field);
		return field;
	}
	const struct variable *v = variable(ddo->script, id);
	if (length)
		*length = v ? v->length : 0;
	return v ? v->text : "";
}

const char *
st_ddofx_value(const struct st_ddofx *ddo, const char *field)
{
	return st_ddofx_value_bytes(ddo, field, NULL);
}

size_t
st_ddofx_copy_value(const struct st_ddofx *ddo, const char *field, void *dest, size_t capacity)
{
	size_t      length;
	const char *bytes = st_ddofx_value_bytes(ddo, field, &length);
	if (length > capacity)
		length = capacity;
	memcpy(dest, bytes, length);
	return length;
}

bool
st_ddofx_number(const struct st_ddofx *ddo, const char *s, int32_t *value)
{
	if (ascii_case_ncompare(s, "DEFVAR", 6))
		return integer(s, value);
	const struct variable *v = reference(ddo, s);
	if (!v || (v->type == UNDEFINED && !v->length))
		return false;
	*value = variable_integer(v);
	return true;
}

bool
st_ddofx_capture(struct st_ddofx *ddo, const char *text, bool numeric)
{
	struct st_ddofx_script *script = ddo->script;
	if (script == NULL || !script->capture)
		return false;
	struct variable *v = variable(script, script->capture);
	v->type            = numeric ? NUMERIC : TEXT;
	set_value(v, text, strlen(text));
	return true;
}

static void
set_number(struct variable *v, int64_t n)
{
	v->length = (size_t)snprintf(v->text, sizeof(v->text), "%" PRId32, signed_long((uint32_t)n));
}

void
st_ddofx_set_numeric_variable(struct st_ddofx *ddo, unsigned id, int number)
{
	struct variable *v = variable(ddo->script, id);
	if (v) {
		v->type = NUMERIC;
		set_number(v, number);
	}
}

char *
st_ddofx_field(char **remaining)
{
	char *start = *remaining;
	if (start == NULL)
		return NULL;
	char *comma = strchr(start, ',');
	if (comma != NULL)
		*comma++ = 0;
	*remaining = comma;
	return start;
}

bool
st_ddofx_field_number(const struct st_ddofx *ddo, char **args, int32_t *n)
{
	const char *s = st_ddofx_field(args);
	return s != NULL && st_ddofx_number(ddo, s, n);
}

static void
macro_text(char *out, const char *in, size_t limit)
{
	size_t i = 0;
	for (; i < limit && in[i]; ++i) {
		switch (in[i]) {
			case '(':
				out[i] = '{';
				break;
			case ')':
				out[i] = '}';
				break;
			case '[':
				out[i] = '(';
				break;
			case ']':
				out[i] = ')';
				break;
			default:
				out[i] = in[i];
				break;
		}
	}
	out[i] = 0;
}

static struct macro_frame *
push(struct st_ddofx_script *s)
{
	/* The SDK permits declining a nested interpreter when its stack is full. */
	if (s->depth >= 64)
		return NULL;
	struct macro_frame *f = calloc(1, sizeof(*f));
	if (f != NULL) {
		f->parent = s->top;
		s->top    = f;
		++s->depth;
	}
	return f;
}

static void
pop(struct st_ddofx_script *s)
{
	struct macro_frame *f = s->top;
	s->top                = f->parent;
	--s->depth;
	free(f->resource);
	free(f);
}

bool
st_ddofx_script_active(const struct st_ddofx *ddo)
{
	return ddo->script != NULL && ddo->script->top != NULL;
}

void
st_ddofx_script_destroy(struct st_ddofx *ddo)
{
	while (st_ddofx_script_active(ddo))
		pop(ddo->script);
	free(ddo->script);
	ddo->script = NULL;
}

void
st_ddofx_script_clear(struct st_ddofx *ddo)
{
	if (!st_ddofx_script_active(ddo)) {
		st_ddofx_script_destroy(ddo);
		return;
	}
	/* The current line and any calling scripts own their instruction
	 * bytes. RESET can run inside them and must allow them to return. */
	struct st_ddofx_script *s = ddo->script;
	memset(s->variables, 0, sizeof(s->variables));
	memset(s->returns, 0, sizeof(s->returns));
	memset(s->macros, 0, sizeof(s->macros));
	memset(s->macro_resource_text, 0, sizeof(s->macro_resource_text));
	s->capture = 0;
}

static bool
script_ready(struct st_ddofx *ddo)
{
	if (ddo->script == NULL)
		ddo->script = calloc(1, sizeof(*ddo->script));
	return ddo->script != NULL;
}

void
st_ddofx_run_resource(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *member)
{
	char container[sizeof(ddo->resource_prefix) + 1];
	snprintf(container, sizeof(container), "%sM", ddo->resource_prefix);
	FILE *file = sink->open_asset(sink->context, "BBSTHEME", NULL, container);
	if (file == NULL)
		return;
	uint32_t size;
	if (st_ddofx_resource_find(file, member, -1, &size, NULL) >= 0 && size && size <= 40000 && script_ready(ddo)) {
		struct macro_frame *f = push(ddo->script);
		if (f != NULL) {
			f->resource = malloc(size);
			if (f->resource != NULL && fread(f->resource, 1, size, file) == size) {
				f->length        = size;
				f->resource_text = true;
			}
			else
				pop(ddo->script);
		}
	}
	fclose(file);
}

static bool
using_resource_text(const struct st_ddofx *ddo)
{
	return st_ddofx_script_active(ddo) && ddo->script->top->resource_text;
}

static void
named_macro(struct st_ddofx *ddo, const char *verb, char *args)
{
	struct st_ddofx_script *s = ddo->script;
	int32_t                 id, line = 1;
	if (!st_ddofx_field_number(ddo, &args, &id))
		return;
	if (id == 0 && !strcmp(verb, "DEFM")) {
		memset(s->macros, 0, sizeof(s->macros));
		return;
	}
	if (id < 1 || id > MACRO_COUNT)
		return;
	if (!strcmp(verb, "CLRM")) {
		memset(s->macros[id - 1], 0, sizeof(s->macros[0]));
		return;
	}
	int last = MACRO_LINES;
	if (strcmp(verb, "RUNM")) {
		if (!st_ddofx_field_number(ddo, &args, &line) || line < 1 || line > MACRO_LINES)
			return;
		if (!strcmp(verb, "DEFM")) {
			s->macro_resource_text[id - 1][line - 1] = using_resource_text(ddo);
			macro_text(s->macros[id - 1][line - 1], st_ddofx_value(ddo, args ? args : ""),
			    LINE_LENGTH);
			return;
		}
		if (strcmp(verb, "EXCM"))
			return;
		last = line;
	}
	struct macro_frame *f = push(s);
	if (f != NULL) {
		f->macro     = id;
		f->next_line = line;
		f->end_line  = last;
	}
}

static void
copy_comma_field(char *out, const char *source, size_t limit)
{
	copy(out, source, limit);
	for (; *out; ++out)
		if (*out == '~')
			*out = ',';
}

size_t
st_ddofx_decode_controls(uint8_t *out, const char *source, size_t length, bool keep_final_caret)
{
	size_t used = 0;
	for (size_t i = 0; i < length; ++i) {
		uint8_t c = (uint8_t)source[i];
		if (c == '^') {
			if (i + 1 < length)
				c = (uint8_t)(ascii_to_upper((uint8_t)source[++i]) - 64);
			else if (!keep_final_caret)
				break;
		}
		out[used++] = c;
	}
	return used;
}

void
st_ddofx_define_ping(struct st_ddofx *ddo, char *args)
{
	int32_t minutes;
	if (!st_ddofx_field_number(ddo, &args, &minutes) || !args)
		return;
	if (minutes <= 0) {
		ddo->ping.interval_ms = 0;
		return;
	}
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0)
		return;
	/* Decode in place. The SDK's ten-byte limit applies to the transmitted
	 * string, which can contain NUL and commas. */
	size_t length    = st_ddofx_decode_controls((uint8_t *)args, args, strlen(args), false);
	ddo->ping.length = length < sizeof(ddo->ping.bytes) ? length : sizeof(ddo->ping.bytes);
	memcpy(ddo->ping.bytes, args, ddo->ping.length);
	ddo->ping.interval_ms = (uint64_t)minutes * 60000;
	ddo->ping.due         = now + ddo->ping.interval_ms;
}

size_t
st_ddofx_control_value(const struct st_ddofx *ddo, const char *source, uint8_t *value, size_t limit)
{
	size_t length = strlen(source);
	char   decoded[ST_DDOFX_BODY_MAX + 1];
	length          = st_ddofx_decode_controls((uint8_t *)decoded, source, length < limit ? length : limit, false);
	decoded[length] = 0;
	const char *bytes = decoded;
	if (!memchr(decoded, 0, length))
		bytes = st_ddofx_value_bytes(ddo, decoded, &length);
	if (length > limit)
		length = limit;
	memcpy(value, bytes, length);
	return length;
}

void
st_ddofx_info_reply(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const uint8_t *data, size_t length,
    const char *error)
{
	if (error) {
		data   = (const uint8_t *)error;
		length = strlen(error);
	}
	struct variable *v = ddo->script ? variable(ddo->script, ddo->script->capture) : NULL;
	if (v)
		set_value(v, data, length);
	else {
		if (length)
			sink->reply(sink->context, data, length);
		if (error) {
			static const uint8_t end = 0xff;
			sink->reply(sink->context, &end, 1);
		}
	}
}

void
st_ddofx_callback_define(const struct st_ddofx *ddo, struct st_ddofx_callback *cb, const char *reply,
    const char *macro, size_t limit)
{
	if (limit > sizeof(cb->reply))
		limit = sizeof(cb->reply);
	cb->resource_text = using_resource_text(ddo);
	cb->stealth = ddo->define_stealth;
	size_t      length;
	const char *bytes = st_ddofx_value_bytes(ddo, reply, &length);
	cb->reply_length = length < limit ? length : limit;
	for (size_t i = 0; i < cb->reply_length; ++i)
		cb->reply[i] = bytes[i] == '~' ? ',' : bytes[i];
	macro_text(cb->macro, st_ddofx_value(ddo, macro), limit);
}

bool
st_ddofx_callback_enabled(const struct st_ddofx *ddo, const struct st_ddofx_callback *cb)
{
	return !ddo->background_download || cb->stealth;
}

bool
st_ddofx_callback_start(struct st_ddofx *ddo, const struct st_ddofx_callback *cb, enum st_ddofx_callback_kind kind)
{
	if (!st_ddofx_callback_enabled(ddo, cb) || !script_ready(ddo))
		return false;
	struct macro_frame *f = push(ddo->script);
	if (f != NULL) {
		f->saved_in_event = ddo->in_event;
		f->callback      = kind;
		f->saved_wait    = ddo->wait_until;
		ddo->wait_until  = 0;
		ddo->in_event    = true;
		f->resource_text = cb->resource_text;
		copy(f->source, cb->macro, sizeof(cb->macro) - 1);
		f->length = strlen(f->source);
		f->after_reply_length = st_ddofx_decode_controls(f->after_reply, cb->reply, cb->reply_length, true);
	}
	return f != NULL;
}

bool
st_ddofx_collision_callback(struct st_ddofx *ddo, const struct st_ddofx_callback *cb, unsigned animation)
{
	if (!st_ddofx_callback_start(ddo, cb, DDO_EVENT))
		return false;
	ddo->script->top->collision_animation = animation;
	return true;
}

bool
st_ddofx_stopping_collision(const struct st_ddofx *ddo, unsigned animation)
{
	/* A collision macro may call a named macro or a local resource script. */
	for (const struct macro_frame *f = ddo->script ? ddo->script->top : NULL; f; f = f->parent)
		if (f->collision_animation && (!animation || f->collision_animation == animation))
			return true;
	return false;
}

bool
st_ddofx_field_callback(struct st_ddofx *ddo, const struct st_ddofx_callback *cb, unsigned field)
{
	if (!st_ddofx_callback_start(ddo, cb, DDO_EVENT))
		return false;
	struct macro_frame *f = ddo->script->top;
	(void)st_ddofx_text_field_value(ddo, field, true, f->after_reply, &f->after_reply_length);
	return true;
}

void
st_ddofx_execute_group(struct st_ddofx *ddo, enum st_ddofx_group_kind kind, unsigned anchor, bool submitted_by_key)
{
	if (!script_ready(ddo))
		return;
	struct macro_frame *f = push(ddo->script);
	if (f) {
		f->group_kind   = kind;
		f->group_anchor = anchor;
		f->submitted_by_key = submitted_by_key;
		if (submitted_by_key)
			ddo->in_key = true;
	}
}

bool
st_ddofx_button_callback(struct st_ddofx *ddo, const struct st_ddofx_callback *cb,
    enum st_ddofx_callback_kind kind, unsigned index)
{
	uint64_t now;
	if (st_time_monotonic_ms(&now) != 0 || !st_ddofx_callback_start(ddo, cb, kind))
		return false;
	ddo->script->top->pressed_button = index + 1;
	ddo->wait_until                  = now + (3 * 1000 + 17) / 18;
	return true;
}

static void
named_return(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	struct st_ddofx_script *s = ddo->script;
	int32_t                 id;
	if (!st_ddofx_field_number(ddo, &args, &id) || id < 0 || id > RETURN_COUNT)
		return;
	if (!strcmp(verb, "DEFRET")) {
		if (id == 0)
			memset(s->returns, 0, sizeof(s->returns));
		else {
			size_t      length;
			const char *source = st_ddofx_value_bytes(ddo, args ? args : "", &length);
			char        encoded[LINE_LENGTH];
			if (length > sizeof(encoded))
				length = sizeof(encoded);
			for (size_t i = 0; i < length; ++i)
				encoded[i] = source[i] == '~' ? ',' : source[i];
			s->returns[id - 1].length =
			    st_ddofx_decode_controls(s->returns[id - 1].bytes, encoded, length, true);
		}
	}
	else if (id && !strcmp(verb, "SRET") && s->returns[id - 1].length)
		sink->reply(sink->context, s->returns[id - 1].bytes, s->returns[id - 1].length);
}

static void
arithmetic(struct st_ddofx_script *s, struct variable *v, const char *verb, int32_t amount)
{
	int32_t n = variable_integer(v);
	if (!strcmp(verb, "RNDVAR")) {
		if (amount > 0) {
			s->random = s->random * UINT32_C(1664525) + UINT32_C(1013904223);
			set_number(v, 1 + (((uint64_t)s->random * (unsigned)amount) >> 32));
		}
		return;
	}
	if (v->type != NUMERIC)
		return;
	if (!strcmp(verb, "VARADD") || !strcmp(verb, "INCVAR"))
		set_number(v, (int64_t)n + amount);
	else if (!strcmp(verb, "VARSUB") || !strcmp(verb, "DECVAR"))
		set_number(v, (int64_t)n - amount);
	else if (!strcmp(verb, "VARMLT"))
		set_number(v, (int64_t)n * amount);
	else if (!strcmp(verb, "VARDIV") && amount) {
		int64_t q               = (int64_t)n / amount;
		int64_t twice_remainder = 2 * llabs((int64_t)n % amount);
		int64_t divisor         = llabs((int64_t)amount);
		if (twice_remainder > divisor || (twice_remainder == divisor && q % 2))
			q += (n < 0) == (amount < 0) ? 1 : -1;
		set_number(v, q);
	}
}

static void
edit_variables(struct st_ddofx *ddo, struct variable *v, const char *verb, char *args, int32_t n)
{
	int32_t index, count;
	size_t  len = v->length;
	if (!strcmp(verb, "PADVARL") || !strcmp(verb, "PADVARR")) {
		const char *pad = args == NULL ? "" : st_ddofx_value(ddo, args);
		if (n > VARIABLE_LENGTH)
			n = VARIABLE_LENGTH;
		if (n <= (int32_t)len || !*pad)
			return;
		size_t extra = (size_t)n - len;
		if (!strcmp(verb, "PADVARL")) {
			memmove(v->text + extra, v->text, len);
			memset(v->text, *pad, extra);
		}
		else
			memset(v->text + len, *pad, extra);
		v->text[n] = 0;
		v->length  = (size_t)n;
		return;
	}
	if (!strcmp(verb, "VARDEL")) {
		if (!st_ddofx_field_number(ddo, &args, &count) || count <= 0 || n <= 0 || n > (int32_t)len)
			return;
		size_t start   = (size_t)n - 1;
		size_t removed = (size_t)count < len - start ? (size_t)count : len - start;
		memmove(v->text + start, v->text + start + removed, len - start - removed + 1);
		v->length -= removed;
		return;
	}
	struct variable *other = variable(ddo->script, n);
	if (other == NULL)
		return;
	if (!strcmp(verb, "VAREQU"))
		set_value(v, other->text, other->length);
	else if (!strcmp(verb, "VARLEN") || !strcmp(verb, "VARPOS")) {
		size_t result = len;
		if (!strcmp(verb, "VARPOS")) {
			size_t      size;
			const char *needle = st_ddofx_value_bytes(ddo, args ? args : "", &size);
			result             = 0;
			for (size_t i = 0; size && size <= len && i <= len - size; ++i) {
				if (!memcmp(v->text + i, needle, size)) {
					result = i + 1;
					break;
				}
			}
		}
		other->type = NUMERIC;
		set_number(other, (int64_t)result);
	}
	else if (!strcmp(verb, "VARCPY") && st_ddofx_field_number(ddo, &args, &index)
	         && st_ddofx_field_number(ddo, &args, &count)) {
		size_t start = index <= 1 ? 0 : (size_t)index - 1;
		if (start > len)
			start = len;
		size_t size = count < 0 ? 0 : (size_t)count;
		set_value(other, v->text + start, size < len - start ? size : len - start);
	}
	else if (!strcmp(verb, "VARINS") && st_ddofx_field_number(ddo, &args, &index)) {
		char   joined[2 * VARIABLE_LENGTH + 1];
		size_t start  = index <= 1 ? 0 : (size_t)index - 1;
		size_t length = other->length;
		if (start > length)
			start = length;
		memcpy(joined, other->text, start);
		memcpy(joined + start, v->text, len);
		memcpy(joined + start + len, other->text + start, length - start + 1);
		set_value(other, joined, len + length);
	}
}

static void
variable_command(struct st_ddofx *ddo, const char *verb, char *args)
{
	struct st_ddofx_script *s = ddo->script;
	int32_t                 id, n;
	if (!st_ddofx_field_number(ddo, &args, &id))
		return;
	if (!strcmp(verb, "VARCAP")) {
		if (id >= 0 && id <= VARIABLE_COUNT)
			s->capture = id;
		return;
	}
	if (id == 0 && !strcmp(verb, "DEFVAR")) {
		memset(s->variables, 0, sizeof(s->variables));
		return;
	}
	struct variable *v = variable(s, id);
	if (v == NULL)
		return;
	if (!strcmp(verb, "CAPVAR")) {
		for (size_t i = 0; i < v->length; ++i)
			v->text[i] = (char)ascii_to_upper((unsigned char)v->text[i]);
		return;
	}
	if (!strcmp(verb, "INCVAR") || !strcmp(verb, "DECVAR")) {
		size_t len = v->length;
		if (v->type == TEXT) {
			if (*verb == 'I' && len < VARIABLE_LENGTH) {
				v->text[len++] = ' ';
				v->text[len]   = 0;
				v->length      = len;
			}
			else if (*verb == 'D' && len)
				v->text[--v->length] = 0;
		}
		else
			arithmetic(s, v, verb, 1);
		return;
	}
	if (!st_ddofx_field_number(ddo, &args, &n))
		return;
	if (!strcmp(verb, "DEFVAR") || !strcmp(verb, "VARTYP")) {
		if (n != TEXT && n != NUMERIC)
			return;
		v->type = n == TEXT ? TEXT : NUMERIC;
		if (!strcmp(verb, "DEFVAR"))
			set_value(v, args ? args : "", args ? strlen(args) : 0);
	}
	else if (!strcmp(verb, "RNDVAR") || !strcmp(verb, "VARADD") || !strcmp(verb, "VARSUB")
	         || !strcmp(verb, "VARMLT") || !strcmp(verb, "VARDIV"))
		arithmetic(s, v, verb, n);
	else
		edit_variables(ddo, v, verb, args, n);
}

static unsigned
comparison_operator(const char *op)
{
	if (!strcmp(op, "="))
		return EQUAL;
	if (!strcmp(op, "<>"))
		return LESS | GREATER;
	if (!strcmp(op, "<"))
		return LESS;
	if (!strcmp(op, ">"))
		return GREATER;
	if (!strcmp(op, "<="))
		return LESS | EQUAL;
	if (!strcmp(op, ">="))
		return GREATER | EQUAL;
	return 0;
}

static bool
compare(const struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const struct predicate *p, bool *valid)
{
	const struct variable *v = reference(ddo, p->master);
	char                   builtin[ST_DDOFX_BODY_MAX + 1];
	bool                   numeric = false;
	const char            *master;
	if (v != NULL) {
		if (v->type == UNDEFINED) {
			*valid = false;
			return false;
		}
		master  = v->text;
		numeric = v->type == NUMERIC;
	}
	else if (st_ddofx_builtin(ddo, sink, p->master, builtin, &numeric))
		master = builtin;
	else
		master = p->master;
	int relation;
	if (numeric) {
		int32_t a = 0, b = 0;
		if (v)
			a = variable_integer(v);
		else
			integer(master, &a);
		st_ddofx_number(ddo, p->slave, &b);
		relation = (a > b) - (a < b);
	}
	else {
		size_t      a = v ? v->length : strlen(master), b;
		const char *slave = st_ddofx_value_bytes(ddo, p->slave, &b);
		relation          = 0;
		for (size_t i = 0; !relation && i < a && i < b; ++i)
			relation =
			    (int)ascii_to_upper((uint8_t)master[i]) - (int)ascii_to_upper((uint8_t)slave[i]);
		if (!relation)
			relation = (a > b) - (a < b);
	}
	*valid = true;
	unsigned order = relation < 0 ? LESS : relation > 0 ? GREATER : EQUAL;
	return (p->relations & order) != 0;
}

static void
repeat_macro(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	struct predicate p     = {0};
	bool             until = !strcmp(verb, "REPVAR");
	int32_t          count = 1;
	if (!strcmp(verb, "REPMAC")) {
		if (!st_ddofx_field_number(ddo, &args, &count) || count < 1 || count > 32000)
			return;
	}
	else {
		char *master = st_ddofx_field(&args), *op = st_ddofx_field(&args), *slave = st_ddofx_field(&args);
		if (master == NULL || op == NULL || slave == NULL)
			return;
		p.relations = comparison_operator(st_ddofx_value(ddo, op));
		if (!p.relations)
			return;
		copy_comma_field(p.master, master, ST_DDOFX_BODY_MAX);
		copy_comma_field(p.slave, slave, ST_DDOFX_BODY_MAX);
		if (!until) {
			bool valid;
			if (!compare(ddo, sink, &p, &valid))
				return;
		}
	}
	const char *body = args ? st_ddofx_value(ddo, args) : NULL;
	if (!body || !*body)
		return;
	struct macro_frame *f = push(ddo->script);
	if (f == NULL)
		return;
	f->resource_text = f->parent != NULL && f->parent->resource_text;
	macro_text(f->source, body, ST_DDOFX_BODY_MAX);
	f->length    = strlen(f->source);
	f->remaining = count;
	f->until     = until;
	f->condition = p;
}

int
st_ddofx_script_step(struct st_ddofx *ddo, const struct st_ddofx_sink *sink)
{
	struct st_ddofx_script *s = ddo->script;
	struct macro_frame     *f = s->top;
	if (f->group_anchor) {
		bool started;
		switch (f->group_kind) {
			case DDO_ICON_OPEN:
			case DDO_ICON_RESTORE:
			case DDO_ICON_RESTORE_ALL:
				started = st_ddofx_next_icon(ddo, f->group_kind, f->group_anchor, &f->group_next);
				break;
			case DDO_LIST_BOX:
				started = st_ddofx_next_list_item(ddo, f->group_anchor, &f->group_next);
				break;
			case DDO_TEXT_FIELD:
				started = st_ddofx_next_text_field(ddo, f->group_anchor, &f->group_next);
				break;
			default:
				started = st_ddofx_next_choice(ddo, f->group_kind, f->group_anchor, &f->group_next);
				break;
		}
		if (!started) {
			if (f->submitted_by_key)
				ddo->in_key = false;
			pop(s);
		}
		return 0;
	}
	if (f->pressed_button) {
		st_ddofx_button_up(ddo, f->pressed_button - 1);
		f->pressed_button = 0;
	}
	if (f->position < f->length) {
		size_t               used;
		const uint8_t       *source      = f->resource != NULL ? f->resource : (const uint8_t *)f->source;
		struct st_ddofx_sink destination = *sink;
		if (f->resource_text && sink->resource_text != NULL)
			destination.text = sink->resource_text;
		int status = st_ddofx_parse(ddo, &f->framer, &destination, source + f->position,
		    f->length - f->position, &used);
		if (ddo->abort_script) {
			/* The framer and instruction bytes belong to these frames. Release
			 * them only after parse returns. Queued connection input stays live. */
			while (s->top)
				pop(s);
			ddo->abort_script = false;
			ddo->in_key = ddo->in_mouse = ddo->in_event = false;
		}
		else
			f->position += used;
		return status;
	}
	if (f->macro && f->next_line <= f->end_line) {
		/* Snapshot this line. Commands in it may change any later line. */
		f->resource_text = s->macro_resource_text[f->macro - 1][f->next_line - 1];
		copy(f->source, s->macros[f->macro - 1][f->next_line++ - 1], LINE_LENGTH);
		f->length = strlen(f->source);
	}
	else if (f->until) {
		bool valid;
		if (compare(ddo, sink, &f->condition, &valid) || !valid) {
			pop(s);
			return 0;
		}
	}
	else if (--f->remaining <= 0) {
		enum st_ddofx_callback_kind kind = f->callback;
		if (f->callback) {
			ddo->in_event   = f->saved_in_event;
			ddo->wait_until = f->saved_wait;
			if (f->after_reply_length)
				sink->reply(sink->context, f->after_reply, f->after_reply_length);
		}
		pop(s);
		if (kind == DDO_RAY)
			st_ddofx_ray_after_profile(ddo);
		else if (kind == DDO_FIELD)
			st_ddofx_finish_field(ddo);
		else if (kind == DDO_MOUSE)
			st_ddofx_resume_mouse(ddo);
		return kind == DDO_KEY ? st_ddofx_resume_key(ddo, sink) : 0;
	}
	f->position = 0;
	f->framer   = (struct st_ddofx_framer) {0};
	return 0;
}

void
st_ddofx_script_command(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, char *body)
{
	char  root = *body++;
	char *args = body;
	char *verb = st_ddofx_field(&args);
	if (args == NULL)
		return;
	if (!script_ready(ddo))
		return;
	for (char *p = verb; *p; ++p)
		*p = (char)ascii_to_upper((unsigned char)*p);
	if (root == '4' && !strcmp(verb, "MKEY"))
		st_ddofx_define_key(ddo, args);
	else if (root == '4')
		named_macro(ddo, verb, args);
	else if (root == '7' && (!strcmp(verb, "ZMBKGRND") || !strcmp(verb, "ZMSTEALTH"))) {
		int32_t enabled;
		if (ddo->graphics && st_ddofx_number(ddo, args, &enabled) && (enabled == 0 || enabled == 1)) {
			if (!strcmp(verb, "ZMBKGRND"))
				ddo->enable_background_download = enabled != 0;
			else
				ddo->define_stealth = enabled != 0;
		}
	}
	else if (root == '7' && (!strcmp(verb, "CEVENT") || !strcmp(verb, "AEVENT") || !strcmp(verb, "DEVENT")))
		st_ddofx_event_command(ddo, verb, args);
	else if (root == '7')
		named_return(ddo, sink, verb, args);
	else if (!strcmp(verb, "GTXTFLD+") || !strcmp(verb, "GTXTFLD-")) {
		int32_t id, field;
		if (!st_ddofx_field_number(ddo, &args, &id) || !st_ddofx_field_number(ddo, &args, &field) || args)
			return;
		struct variable *v = variable(ddo->script, id);
		bool full = verb[7] == '+';
		uint8_t result[ST_DDOFX_FIELD_REPLY_MAX];
		size_t  length;
		if (v && st_ddofx_text_field_value(ddo, field, full, result, &length))
			set_value(v, result, length);
	}
	else if (!strcmp(verb, "INFSAV") || !strcmp(verb, "INFLOD"))
		st_ddofx_info(ddo, sink, !strcmp(verb, "INFSAV"), args);
	else if (!strcmp(verb, "IFVAR") || !strcmp(verb, "REPVAR") || !strcmp(verb, "REPMAC"))
		repeat_macro(ddo, sink, verb, args);
	else
		variable_command(ddo, verb, args);
}
