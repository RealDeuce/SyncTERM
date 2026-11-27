/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
/* Instantiate one translation-unit-local table from the consumer's SDK types.
 * Each entry gives the return type, field name, and parameter types. */
#include "dynamic_library.h"
#include "st_threads.h"

#define ST_API_JOIN_(a, b) a##b
#define ST_API_JOIN(a, b) ST_API_JOIN_(a, b)
#define ST_API_LOCAL(suffix) ST_API_JOIN(ST_API_NAME, suffix)
#define ST_API_SYMBOL(field) ST_API_JOIN(ST_API_PREFIX, field)
#ifndef ST_API_CALL
 #define ST_API_CALL
#endif
#define ST_API_MEMBER(result, field, args) result (ST_API_CALL *field) args;

#if ST_API_LINKED
 #define ST_API_ADDRESS(result, field, args) .field = ST_API_SYMBOL(field),
static const struct {
	ST_API_FUNCTIONS(ST_API_MEMBER)
} ST_API_NAME = { ST_API_FUNCTIONS(ST_API_ADDRESS) };
 #undef ST_API_ADDRESS

static inline bool
ST_API_LOCAL(_available)(void)
{
	return true;
}
#else
static struct {
	ST_API_FUNCTIONS(ST_API_MEMBER)
} ST_API_NAME;
static bool ST_API_LOCAL(_ready);

static void
ST_API_LOCAL(_load)(void)
{
	static const char *const names[] = { ST_API_NAMES };
 /* Compare each signature to the SDK in an unevaluated expression. */
 #define ST_API_CHECK(result, field, args) \
	_Static_assert(_Generic(&ST_API_SYMBOL(field), result (ST_API_CALL *) args: 1, default: 0), #field " signature");
	ST_API_FUNCTIONS(ST_API_CHECK)
 #undef ST_API_CHECK
 #define ST_API_STRING_(name) #name
 #define ST_API_STRING(name) ST_API_STRING_(name)
 #define ST_API_IMPORT(result, field, args) {ST_API_STRING(ST_API_PREFIX) #field, &ST_API_NAME.field, sizeof(ST_API_NAME.field)},
	const struct dynamic_library_import imports[] = { ST_API_FUNCTIONS(ST_API_IMPORT) };
 #undef ST_API_IMPORT
 #undef ST_API_STRING
 #undef ST_API_STRING_
	ST_API_LOCAL(_ready) = dynamic_library_bind(names, sizeof(names) / sizeof(names[0]), ST_API_MAJOR,
	    imports, sizeof(imports) / sizeof(imports[0]));
}

static bool
ST_API_LOCAL(_available)(void)
{
	static st_once_flag_t once = ST_ONCE_FLAG_INIT;
	st_call_once(&once, ST_API_LOCAL(_load));
	return ST_API_LOCAL(_ready);
}
#endif

#undef ST_API_MEMBER
#undef ST_API_SYMBOL
#undef ST_API_LOCAL
#undef ST_API_JOIN
#undef ST_API_JOIN_
#undef ST_API_NAME
#undef ST_API_FUNCTIONS
#undef ST_API_LINKED
#undef ST_API_NAMES
#undef ST_API_MAJOR
#undef ST_API_PREFIX
#undef ST_API_CALL
