/* Unicode comparison tests. */

#include "st_unicode_compare.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #condition); \
		++failures; \
	} \
} while (0)

static void
caseless_tests(void)
{
	bool equal = false;
	bool matched = false;
	int compared;

	CHECK(st_unicode_caseless_equal(&equal, "Stra\xc3\x9f" "e", 7,
	    "STRASSE", 7) == 0 && equal);
	CHECK(st_unicode_caseless_equal(&equal, "\xce\x9f\xce\xa3\xce\x9f\xce\xa3", 8,
	    "\xce\xbf\xcf\x83\xce\xbf\xcf\x82", 8) == 0 && equal);
	CHECK(st_unicode_caseless_equal(&equal, "\xc3\x85", 2,
	    "A\xcc\x8a", 3) == 0 && equal);
	CHECK(st_unicode_caseless_compare(&compared, "a", 1, "B", 1) == 0
	    && compared < 0);
	CHECK(st_unicode_caseless_equal(&equal, "\xc4\xb0", 2, "i", 1) == 0
	    && !equal);
	CHECK(st_unicode_caseless_equal(&equal, "\xc0\x80", 2, "", 0)
	    < 0);
	CHECK(st_unicode_caseless_starts_with(&matched, "Stra\xc3\x9f" "e", 7,
	    "strass", 6) == 0 && matched);
	CHECK(st_unicode_caseless_contains(&matched, "Stra\xc3\x9f" "e", 7,
	    "s", 1) == 0 && matched);
	CHECK(st_unicode_caseless_contains(&matched, "\xc3\x9f", 2,
	    "s", 1) == 0 && !matched);
}

static void
collation_tests(void)
{
	int compared;
	st_unicode_sort_key_t *lower = NULL;
	st_unicode_sort_key_t *upper = NULL;

	CHECK(st_unicode_collate(&compared, "cab", 3, "c\xc3\xa1" "b", 4,
	    ST_UNICODE_COLLATION_CASE_INSENSITIVE) == 0 && compared < 0);
	CHECK(st_unicode_collate(&compared, "c\xc3\xa1" "b", 4, "dab", 3,
	    ST_UNICODE_COLLATION_CASE_INSENSITIVE) == 0 && compared < 0);
	CHECK(st_unicode_sort_key_create(&lower, "a", 1,
	    ST_UNICODE_COLLATION_CASE_INSENSITIVE) == 0);
	CHECK(st_unicode_sort_key_create(&upper, "A", 1,
	    ST_UNICODE_COLLATION_CASE_INSENSITIVE) == 0);
	CHECK(st_unicode_sort_key_compare(lower, upper) != 0);
	st_unicode_sort_key_destroy(&lower);
	st_unicode_sort_key_destroy(&upper);
}

static void
wildcard_and_name_tests(void)
{
	bool matched = false;

	CHECK(st_unicode_wildcard_match(&matched, "*.INI", 5,
	    "r\xc3\xa9sum\xc3\xa9.ini", 12, false) == 0 && matched);
	CHECK(st_unicode_wildcard_match(&matched, "?", 1,
	    "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd", 8, false) == 0 && matched);
	CHECK(st_unicode_wildcard_match(&matched, "\xc3\x85?", 3,
	    "A\xcc\x8a" "b", 4, true) == 0 && matched);
	CHECK(st_unicode_wildcard_match(&matched, "\xc0\x80", 2,
	    "text", 4, false) < 0);
	CHECK(st_unicode_name_valid("M\xc3\xbcnchen", 30, 60, "[]"));
	CHECK(st_unicode_name_valid("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb",
	    30, 60, "[]"));
	CHECK(!st_unicode_name_valid(" leading", 30, 60, "[]"));
	CHECK(!st_unicode_name_valid("bad[name", 30, 60, "[]"));
	CHECK(!st_unicode_name_valid("bad\xe2\x80\xae", 30, 60, "[]"));
}

int
main(void)
{
	caseless_tests();
	collation_tests();
	wildcard_and_name_tests();
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
