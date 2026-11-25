/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_internal.h"
#include "st_rip3_variable_reference.h"
#include "io_error.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

static int
parse(struct st_rip3_variable_reference *result, const char *text)
{
	return st_rip3_variable_reference_parse(result, (const uint8_t *)text, strlen(text));
}

static int
examples(void)
{
	struct st_rip3_variable_reference reference;

	CHECK(parse(&reference, "$*+20,50:NAME(ToName),30@What's your name?=John Doe$") == 0);
	CHECK(strcmp(reference.name, "NAME") == 0);
	CHECK(reference.directives == (ST_RIP3_VARIABLE_REQUIRED | ST_RIP3_VARIABLE_PERSISTENT));
	CHECK(reference.has_position && reference.has_x && reference.x == 20);
	CHECK(reference.has_y && reference.y == 50 && reference.has_width && reference.width == 30);
	CHECK(reference.mode == ST_RIP3_VARIABLE_ANY && reference.conversion == ST_RIP3_VARIABLE_TONAME);
	CHECK(reference.has_question && reference.question.length == 17);
	CHECK(memcmp(reference.question.bytes, "What's your name?", 17) == 0);
	CHECK(reference.has_default && reference.default_value.length == 8);
	CHECK(memcmp(reference.default_value.bytes, "John Doe", 8) == 0);
	CHECK(parse(&reference, "$*#20,10:PASSWORD,10@Please enter your password$") == 0);
	CHECK(reference.directives == (ST_RIP3_VARIABLE_REQUIRED | ST_RIP3_VARIABLE_MASKED));
	CHECK(reference.x == 20 && reference.y == 10 && reference.width == 10);
	CHECK(reference.has_question && !reference.has_default);
	CHECK(strcmp((const char *)reference.question.bytes, "Please enter your password") == 0);
	CHECK(parse(&reference, "$state=Ca$") == 0);
	CHECK(strcmp(reference.name, "STATE") == 0 && !reference.has_position);
	CHECK(!reference.has_question && reference.has_default && reference.default_value.length == 2);
	CHECK(parse(&reference, "$state@What state do you live in?=Ca$") == 0);
	CHECK(reference.has_question && reference.has_default && reference.default_value.length == 2);
	CHECK(parse(&reference, "$,50:nAme_1$") == 0);
	CHECK(strcmp(reference.name, "NAME_1") == 0 && reference.has_position && !reference.has_x);
	CHECK(reference.has_y && reference.y == 50);
	CHECK(parse(&reference, "$50,:NAME$") == 0);
	CHECK(reference.has_position && reference.has_x && !reference.has_y);
	CHECK(parse(&reference, "$,:NAME$") == 0);
	CHECK(reference.has_position && !reference.has_x && !reference.has_y);
	CHECK(parse(&reference, "$NAME$") == 0);
	CHECK(!reference.has_position && !reference.has_width && !reference.has_question && !reference.has_default);
	CHECK(reference.mode == ST_RIP3_VARIABLE_ANY && reference.conversion == ST_RIP3_VARIABLE_UNCONVERTED);
	CHECK(parse(&reference, "$&-#=+*NAME$") == 0 && reference.directives == 63);
	CHECK(parse(&reference, "$*+=#-&NAME$") == 0 && reference.directives == 63);
	/* Keeping both scope bits is deliberate; this parser selects no precedence. */
	CHECK(parse(&reference, "$-=NAME=value$") == 0);
	CHECK(reference.directives == (ST_RIP3_VARIABLE_SET | ST_RIP3_VARIABLE_SESSION));
	CHECK(parse(&reference, "$&NAME$") == 0 && reference.directives == ST_RIP3_VARIABLE_GET);
	return 0;
}

static int
directives(void)
{
	static const char flags[]="*+=#-&";
	struct st_rip3_variable_reference reference;
	/* Every flag combination, in both orders, with repeated occurrences. The
	 * post-name equals sign must remain the default delimiter in every case. */
	for (unsigned mask=0; mask<64; mask++) for (unsigned reverse=0; reverse<2; reverse++) {
		char text[100];size_t at=0;
		text[at++]='$';
		for (unsigned i=0; i<6; i++) {
			unsigned which=reverse!=0 ? 5-i : i;
			if ((mask&(1u<<which))==0) continue;
			for (unsigned repetition=0; repetition<3; repetition++) text[at++]=flags[which];
		}
		memcpy(text+at,"nAmE=%literal$",15);at+=14;
		CHECK(st_rip3_variable_reference_parse(&reference,(const uint8_t *)text,at)==0
		    && reference.directives==mask && strcmp(reference.name,"NAME")==0
		    && reference.has_default && strcmp((const char *)reference.default_value.bytes,"%literal")==0);
	}
	CHECK(parse(&reference,"$--==NAME=value$")==0
	    && reference.directives==(ST_RIP3_VARIABLE_SET|ST_RIP3_VARIABLE_SESSION));
	CHECK(parse(&reference,"$++==NAME=value$")==0
	    && reference.directives==(ST_RIP3_VARIABLE_PERSISTENT|ST_RIP3_VARIABLE_SESSION));
	CHECK(parse(&reference,"$=-+NAME=$")==0 && reference.has_default && reference.default_value.length==0
	    && reference.directives==(ST_RIP3_VARIABLE_SESSION|ST_RIP3_VARIABLE_SET|ST_RIP3_VARIABLE_PERSISTENT));
	CHECK(parse(&reference,"$-=NAME$")==0 && !reference.has_default);
	CHECK(parse(&reference,"$==NAME==value$")==0
	    && reference.directives==ST_RIP3_VARIABLE_SESSION
	    && strcmp((const char *)reference.default_value.bytes,"=value")==0);
	return 0;
}

static int
format_options(void)
{
	const char *const modes[] = {"ANY","ALPHA","NUMBER","ALPHANUM"};
	const char *const conversions[] = {"TONAME","TOUPPER","TOLOWER"};
	struct st_rip3_variable_reference reference;
	char text[80];

	CHECK(parse(&reference, "$USERDATA()$") == 0);
	for (size_t mode = 0; mode < 4; mode++) {
		(void)snprintf(text, sizeof(text), "$USERDATA(%s)$", modes[mode]);
		CHECK(parse(&reference, text) == 0 && reference.mode == (enum st_rip3_variable_mode)mode);
		for (size_t conversion = 0; conversion < 3; conversion++) {
			(void)snprintf(text, sizeof(text), "$USERDATA(%s,%s)$", modes[mode], conversions[conversion]);
			CHECK(parse(&reference, text) == 0);
			CHECK(reference.mode == (enum st_rip3_variable_mode)mode);
			CHECK(reference.conversion == (enum st_rip3_variable_conversion)(conversion + 1));
		}
	}
	CHECK(parse(&reference, "$USERDATA(aLpHaNuM,tOlOwEr)$") == 0);
	CHECK(reference.mode == ST_RIP3_VARIABLE_ALPHANUM && reference.conversion == ST_RIP3_VARIABLE_TOLOWER);
	for (size_t conversion = 0; conversion < 3; conversion++) {
		(void)snprintf(text, sizeof(text), "$USERDATA(%s)$", conversions[conversion]);
		CHECK(parse(&reference, text) == 0 && reference.mode == ST_RIP3_VARIABLE_ANY);
		CHECK(reference.conversion == (enum st_rip3_variable_conversion)(conversion + 1));
	}
	return 0;
}

static int
boundaries(void)
{
	struct st_rip3_variable_reference reference, before;
	uint8_t text[620];
	size_t length;

	CHECK(parse(&reference, "$ABCDEFGHIJKLMNOPQRST$") == 0 && strlen(reference.name) == 20);
	CHECK(parse(&reference, "$065535,0:NAME,4294967295$") == 0);
	CHECK(reference.has_x && reference.x == 65535 && reference.has_y && reference.y == 0
	    && reference.width == UINT32_MAX);
	CHECK(parse(&reference, "$0000,0001:NAME,0$") == 0 && reference.has_width && reference.width == 0);
	CHECK(parse(&reference, "$NAME@=$") == 0);
	CHECK(reference.has_question && reference.question.length == 0);
	CHECK(reference.has_default && reference.default_value.length == 0);
	CHECK(parse(&reference, "$NAME==a@b(c),d$") == 0);
	CHECK(strcmp((const char *)reference.default_value.bytes, "=a@b(c),d") == 0);
	memcpy(text, "$NAME@", 6);
	memset(text + 6, 'q', 300); text[306] = '=';
	memset(text + 307, 'd', 300); text[607] = '$'; length = 608;
	/* Retain protocol bytes, including NUL, CP437 and a complete UTF-8 sequence. */
	text[6] = 0; text[7] = 0x82; text[8] = 0xc3; text[9] = 0xa9;
	CHECK(st_rip3_variable_reference_parse(&reference, text, length) == 0);
	CHECK(reference.question.length == 100 && memcmp(reference.question.bytes, text + 6, 100) == 0);
	CHECK(reference.default_value.length == 100 && memcmp(reference.default_value.bytes, text + 307, 100) == 0);
	CHECK(reference.question.bytes[100] == 0 && reference.default_value.bytes[100] == 0);
	before = reference; text[250] = '$'; /* Not ignored just because beyond truncation. */
	CHECK(st_rip3_variable_reference_parse(&reference, text, length) == IO_ERROR_INVALID_FORMAT);
	CHECK(memcmp(&reference, &before, sizeof(reference)) == 0);
	return 0;
}

static int
failures(void)
{
	const char *const malformed[] = {
		"", "$", "$$", "NAME", "$NAME", "NAME$", "$NAME$$", "$NA$ME$",
		"$_NAME$", "$1NAME$", "$N AME$", "$NAME!$", "$ NAME$",
		"$NAME(TONAME,ANY)$", "$NAME(ANY,ALPHA)$", "$NAME(TONAME,TOUPPER)$",
		"$NAME(NAME)$", "$NAME(,TONAME)$", "$NAME(ANY,)$", "$NAME(ANY,TONAME,TOLOWER)$",
		"$NAME(ANY$", "$NAME(ANY))$", "$NAME( ANY)$", "$NAME,10(ANY)$",
		"$20:NAME$", "$20,NAME$", "$20,50NAME$", "$,:$", "$20,-1:NAME$",
		"$NAME,$", "$NAME,-1$", "$NAME,+1$", "$NAME,1x$", "$NAME@bad$tail$",
		"$?0$", "$&MSG<<FIELDID>>$", "$N\x82$",
		"$%NAME$", "$%%NAME$", "$-%NAME=value$", "$+%NAME=value$", "$+%=NAME=value$",
	};
	const char *const oversized[] = {
		"$ABCDEFGHIJKLMNOPQRSTU$", "$65536,0:NAME$", "$0,65536:NAME$",
		"$0,999999999999999999999:NAME$", "$NAME,4294967296$",
	};
	struct st_rip3_variable_reference reference, before;

	memset(&reference, 0xa5, sizeof(reference)); before = reference;
	for (size_t i = 0; i < sizeof(malformed) / sizeof(*malformed); i++) {
		CHECK(parse(&reference, malformed[i]) == IO_ERROR_INVALID_FORMAT);
		CHECK(memcmp(&reference, &before, sizeof(reference)) == 0);
	}
	for (size_t i = 0; i < sizeof(oversized) / sizeof(*oversized); i++) {
		CHECK(parse(&reference, oversized[i]) == IO_ERROR_OUT_OF_RANGE);
		CHECK(memcmp(&reference, &before, sizeof(reference)) == 0);
	}
	CHECK(st_rip3_variable_reference_parse(NULL, (const uint8_t *)"$NAME$", 6) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_variable_reference_parse(&reference, NULL, 6) == IO_ERROR_INVALID_ARGUMENT);
	/* Every cut in a valid expression must fail without publishing a partial parse. */
	for (size_t length = 0; length < 12; length++) {
		CHECK(st_rip3_variable_reference_parse(&reference, (const uint8_t *)"$NAME=Value$", length) != 0);
		CHECK(memcmp(&reference, &before, sizeof(reference)) == 0);
	}
	return 0;
}

int
main(void)
{
	CHECK(examples() == 0);
	CHECK(directives() == 0);
	CHECK(format_options() == 0);
	CHECK(boundaries() == 0);
	CHECK(failures() == 0);
	return 0;
}
