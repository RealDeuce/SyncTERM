/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "st_ddofx_script.h"
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

	enum {
		UNDEFINED,
		TEXT,
		NUMERIC
	} type;
};

struct predicate {
	char master[ST_DDOFX_BODY_MAX + 1], slave[ST_DDOFX_BODY_MAX + 1], op[3];
};

struct macro_frame {
	struct macro_frame    *parent;
	struct st_ddofx_framer framer;
	char                   source[ST_DDOFX_BODY_MAX + 1];
	size_t                 position, length;
	int                    macro, next_line, end_line;
	int                    remaining;
	bool                   until;
	struct predicate       condition;
};

struct st_ddofx_script {
	struct variable     variables[VARIABLE_COUNT];
	char                returns[RETURN_COUNT][LINE_LENGTH + 1];
	char                macros[MACRO_COUNT][MACRO_LINES][LINE_LENGTH + 1];
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

static bool
integer(const char *s, int32_t *result)
{
	while (*s == ' ')
		++s;
	int base = *s == '$' ? 16 : 10;
	if (base == 16)
		++s;
	char *end;
	errno      = 0;
	intmax_t n = strtoimax(s, &end, base);
	if (s == end || *end || errno == ERANGE || n < INT32_MIN || n > INT32_MAX)
		return false;
	*result = (int32_t)n;
	return true;
}

static struct variable *
variable(struct st_ddofx_script *script, int32_t id)
{
	return script != NULL && id >= 1 && id <= VARIABLE_COUNT ? &script->variables[id - 1] : NULL;
}

static struct variable *
reference(const struct st_ddofx *ddo, const char *s)
{
	int32_t id;
	if (ascii_case_ncompare(s, "DEFVAR", 6) || !integer(s + 6, &id))
		return NULL;
	return variable(ddo->script, id);
}

const char *
st_ddofx_value(const struct st_ddofx *ddo, const char *s)
{
	if (ascii_case_ncompare(s, "DEFVAR", 6))
		return s;
	const struct variable *v = reference(ddo, s);
	return v ? v->text : "";
}

bool
st_ddofx_number(const struct st_ddofx *ddo, const char *s, int32_t *value)
{
	return integer(st_ddofx_value(ddo, s), value);
}

bool
st_ddofx_capture(struct st_ddofx *ddo, const char *text, bool numeric)
{
	struct st_ddofx_script *script = ddo->script;
	if (script == NULL || !script->capture)
		return false;
	struct variable *v = variable(script, script->capture);
	v->type            = numeric ? NUMERIC : TEXT;
	copy(v->text, text, VARIABLE_LENGTH);
	return true;
}

static void
set_number(struct variable *v, int64_t n)
{
	/* DOS long integers wrap at 32 bits. Spell the result without a signed overflow. */
	n = (uint32_t)n;
	if (n > INT32_MAX)
		n -= INT64_C(4294967296);
	snprintf(v->text, sizeof(v->text), "%" PRId64, n);
}

static char *
field(char **remaining)
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

static bool
field_number(const struct st_ddofx *ddo, char **args, int32_t *n)
{
	const char *s = field(args);
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

static void
named_macro(struct st_ddofx *ddo, const char *verb, char *args)
{
	struct st_ddofx_script *s = ddo->script;
	int32_t                 id, line = 1;
	if (!field_number(ddo, &args, &id))
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
		if (!field_number(ddo, &args, &line) || line < 1 || line > MACRO_LINES)
			return;
		if (!strcmp(verb, "DEFM")) {
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
named_return(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	struct st_ddofx_script *s = ddo->script;
	int32_t                 id;
	if (!field_number(ddo, &args, &id) || id < 0 || id > RETURN_COUNT)
		return;
	if (!strcmp(verb, "DEFRET")) {
		if (id == 0)
			memset(s->returns, 0, sizeof(s->returns));
		else {
			char *out = s->returns[id - 1];
			copy(out, st_ddofx_value(ddo, args ? args : ""), LINE_LENGTH);
			for (; *out; ++out)
				if (*out == '~')
					*out = ',';
		}
	}
	else if (id && !strcmp(verb, "SRET")) {
		const uint8_t *p = (const uint8_t *)s->returns[id - 1];
		uint8_t        bytes[LINE_LENGTH];
		size_t         n = 0;
		while (*p) {
			uint8_t c = *p++;
			if (c == '^' && *p)
				c = (uint8_t)(ascii_to_upper(*p++) - 64);
			bytes[n++] = c;
		}
		if (n)
			sink->reply(sink->context, bytes, n);
	}
}

static void
arithmetic(struct st_ddofx_script *s, struct variable *v, const char *verb, int32_t amount)
{
	int32_t n = 0;
	integer(v->text, &n);
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
	size_t  len = strlen(v->text);
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
		return;
	}
	if (!strcmp(verb, "VARDEL")) {
		if (!field_number(ddo, &args, &count) || count <= 0 || n <= 0 || n > (int32_t)len)
			return;
		size_t start   = (size_t)n - 1;
		size_t removed = (size_t)count < len - start ? (size_t)count : len - start;
		memmove(v->text + start, v->text + start + removed, len - start - removed + 1);
		return;
	}
	struct variable *other = variable(ddo->script, n);
	if (other == NULL)
		return;
	if (!strcmp(verb, "VAREQU"))
		copy(v->text, other->text, VARIABLE_LENGTH);
	else if (!strcmp(verb, "VARLEN") || !strcmp(verb, "VARPOS")) {
		size_t result = len;
		if (!strcmp(verb, "VARPOS")) {
			const char *needle = args ? st_ddofx_value(ddo, args) : "";
			const char *pos    = *needle ? strstr(v->text, needle) : NULL;
			result             = pos == NULL ? 0 : (size_t)(pos - v->text) + 1;
		}
		other->type = NUMERIC;
		set_number(other, (int64_t)result);
	}
	else if (!strcmp(verb, "VARCPY") && field_number(ddo, &args, &index) && field_number(ddo, &args, &count)) {
		size_t start = index <= 1 ? 0 : (size_t)index - 1;
		if (start > len)
			start = len;
		copy(other->text, v->text + start, count < 0 ? 0 : (size_t)count);
	}
	else if (!strcmp(verb, "VARINS") && field_number(ddo, &args, &index)) {
		char   joined[2 * VARIABLE_LENGTH + 1];
		size_t start  = index <= 1 ? 0 : (size_t)index - 1;
		size_t length = strlen(other->text);
		if (start > length)
			start = length;
		memcpy(joined, other->text, start);
		memcpy(joined + start, v->text, len);
		memcpy(joined + start + len, other->text + start, length - start + 1);
		copy(other->text, joined, VARIABLE_LENGTH);
	}
}

static void
variable_command(struct st_ddofx *ddo, const char *verb, char *args)
{
	struct st_ddofx_script *s = ddo->script;
	int32_t                 id, n;
	if (!field_number(ddo, &args, &id))
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
		for (char *p = v->text; *p; ++p)
			*p = (char)ascii_to_upper((unsigned char)*p);
		return;
	}
	if (!strcmp(verb, "INCVAR") || !strcmp(verb, "DECVAR")) {
		size_t len = strlen(v->text);
		if (v->type == TEXT) {
			if (*verb == 'I' && len < VARIABLE_LENGTH) {
				v->text[len++] = ' ';
				v->text[len]   = 0;
			}
			else if (*verb == 'D' && len)
				v->text[len - 1] = 0;
		}
		else
			arithmetic(s, v, verb, 1);
		return;
	}
	if (!field_number(ddo, &args, &n))
		return;
	if (!strcmp(verb, "DEFVAR") || !strcmp(verb, "VARTYP")) {
		if (n != TEXT && n != NUMERIC)
			return;
		v->type = n == TEXT ? TEXT : NUMERIC;
		if (!strcmp(verb, "DEFVAR"))
			copy(v->text, args ? args : "", VARIABLE_LENGTH);
	}
	else if (!strcmp(verb, "RNDVAR") || !strcmp(verb, "VARADD") || !strcmp(verb, "VARSUB")
	         || !strcmp(verb, "VARMLT") || !strcmp(verb, "VARDIV"))
		arithmetic(s, v, verb, n);
	else
		edit_variables(ddo, v, verb, args, n);
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
	else {
		*valid = false;
		return false;
	}
	const char *slave = st_ddofx_value(ddo, p->slave);
	int         relation;
	if (numeric) {
		int32_t a = 0, b = 0;
		integer(master, &a);
		integer(slave, &b);
		relation = (a > b) - (a < b);
	}
	else
		relation = ascii_case_compare(master, slave);
	*valid = true;
	if (!strcmp(p->op, "="))
		return relation == 0;
	if (!strcmp(p->op, "<>"))
		return relation != 0;
	if (!strcmp(p->op, "<"))
		return relation < 0;
	if (!strcmp(p->op, ">"))
		return relation > 0;
	if (!strcmp(p->op, "<="))
		return relation <= 0;
	if (!strcmp(p->op, ">="))
		return relation >= 0;
	*valid = false;
	return false;
}

static void
repeat_macro(struct st_ddofx *ddo, const struct st_ddofx_sink *sink, const char *verb, char *args)
{
	struct predicate p     = {0};
	bool             until = !strcmp(verb, "REPVAR");
	int32_t          count = 1;
	if (!strcmp(verb, "REPMAC")) {
		if (!field_number(ddo, &args, &count) || count < 1 || count > 32000)
			return;
	}
	else {
		char *master = field(&args), *op = field(&args), *slave = field(&args);
		if (master == NULL || op == NULL || slave == NULL)
			return;
		const char *operation = st_ddofx_value(ddo, op);
		if (strlen(operation) > 2)
			return;
		copy(p.master, master, ST_DDOFX_BODY_MAX);
		copy(p.op, operation, 2);
		copy(p.slave, slave, ST_DDOFX_BODY_MAX);
		for (char *c = p.slave; *c; ++c)
			if (*c == '~')
				*c = ',';
		bool valid;
		bool match = compare(ddo, sink, &p, &valid);
		if (!valid || (!until && !match))
			return;
	}
	if (args == NULL || !*args)
		return;
	struct macro_frame *f = push(ddo->script);
	if (f == NULL)
		return;
	macro_text(f->source, st_ddofx_value(ddo, args), ST_DDOFX_BODY_MAX);
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
	if (f->position < f->length) {
		size_t used;
		int    status = st_ddofx_parse(ddo, &f->framer, sink, (const uint8_t *)f->source + f->position,
		    f->length - f->position, &used);
		f->position += used;
		return status;
	}
	if (f->macro && f->next_line <= f->end_line) {
		/* Snapshot this line. Commands in it may change any later line. */
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
		pop(s);
		return 0;
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
	char *verb = field(&args);
	if (args == NULL)
		return;
	if (ddo->script == NULL) {
		ddo->script = calloc(1, sizeof(*ddo->script));
		if (ddo->script == NULL)
			return;
	}
	for (char *p = verb; *p; ++p)
		*p = (char)ascii_to_upper((unsigned char)*p);
	if (root == '4')
		named_macro(ddo, verb, args);
	else if (root == '7')
		named_return(ddo, sink, verb, args);
	else if (!strcmp(verb, "IFVAR") || !strcmp(verb, "REPVAR") || !strcmp(verb, "REPMAC"))
		repeat_macro(ddo, sink, verb, args);
	else
		variable_command(ddo, verb, args);
}
