/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "ini.h"
#include "io_error.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "failed: %s:%d: %s\n", __FILE__, __LINE__, \
		    #condition); \
		return false; \
	} \
} while (0)

static FILE *
stream_from(const void *data, size_t length)
{
	FILE *stream = tmpfile();

	if (stream == NULL)
		return NULL;
	if (length != 0 && fwrite(data, 1, length, stream) != length) {
		fclose(stream);
		return NULL;
	}
	if (fseek(stream, 0, SEEK_SET) != 0) {
		fclose(stream);
		return NULL;
	}
	return stream;
}

static char *
document_text(const ini_document_t *document)
{
	FILE *stream = tmpfile();
	long length;
	char *text;

	if (stream == NULL || ini_document_write(document, stream) != 0
	    || fflush(stream) != 0 || fseek(stream, 0, SEEK_END) != 0) {
		if (stream != NULL)
			fclose(stream);
		return NULL;
	}
	length = ftell(stream);
	if (length < 0 || fseek(stream, 0, SEEK_SET) != 0) {
		fclose(stream);
		return NULL;
	}
	text = malloc((size_t)length + 1);
	if (text == NULL) {
		fclose(stream);
		return NULL;
	}
	if (fread(text, 1, (size_t)length, stream) != (size_t)length) {
		free(text);
		fclose(stream);
		return NULL;
	}
	text[length] = '\0';
	fclose(stream);
	return text;
}

static bool
test_document_and_parse(void)
{
	static const char decomposed[] =
	    "[Cafe\xcc\x81]\nValue=e\xcc\x81\n";
	static const char composed[] =
	    "[Caf\xc3\xa9]\nValue=\xc3\xa9\n";
	static const char input[] =
	    " ; root comment\r\n"
	    "Root = caf\xc3\xa9\r\n"
	    "Empty=\r\n"
	    "Literal:\"line\\n\\t\\\\\\\"\\x7f\"\r\n"
	    "BadLiteral:\"\\q\"\r\n"
	    "NotLiteral:unquoted\r\n"
	    "Equals=one=two\r\n"
	    "\r\n"
	    "[ Alpha ]\r\n"
	    "Key=first\r\n"
	    "key=second\r\n"
	    "[alpha]\r\n"
	    "Key=third\r\n"
	    "[Bad\xc3\xa9]\r\n"
	    "Hidden=no\r\n"
	    "[Unclosed\r\n"
	    "StillHidden=no\r\n"
	    "[Beta]\r\n"
	    "Raw=\xc3\xa9\r\n"
	    "Last=no-newline";
	ini_document_t *document = NULL;
	ini_section_t *root;
	ini_section_t *first;
	ini_section_t *second;
	ini_section_t *unicode;
	ini_section_t *beta;
	name_value_list_t pairs = NULL;
	const struct name_value *pair;
	FILE *stream = stream_from(input, sizeof(input) - 1);
	char *written;

	CHECK(stream != NULL);
	CHECK(ini_document_read(&document, stream) == 0);
	fclose(stream);
	root = ini_document_root(document);
	CHECK(root != NULL && ini_section_name(root) == NULL);
	CHECK(ini_section_count(root) == 4);
	CHECK(strcmp(ini_section_find(root, "ROOT"), "caf\xc3\xa9") == 0);
	CHECK(strcmp(ini_section_find(root, "empty"), "") == 0);
	CHECK(strcmp(ini_section_get_string(root, "empty", "default"),
	    "default") == 0);
	CHECK(strcmp(ini_section_find(root, "literal"),
	    "line\n\t\\\"\x7f") == 0);
	CHECK(strcmp(ini_section_find(root, "equals"), "one=two") == 0);
	CHECK(!ini_section_has_key(root, "BadLiteral"));
	CHECK(!ini_section_has_key(root, "NotLiteral"));

	CHECK(ini_document_section_count(document) == 4);
	first = ini_document_section_at(document, 0);
	second = ini_document_section_at(document, 1);
	unicode = ini_document_section_at(document, 2);
	beta = ini_document_section_at(document, 3);
	CHECK(first != NULL && second != NULL && unicode != NULL && beta != NULL);
	CHECK(strcmp(ini_section_name(first), "Alpha") == 0);
	size_t first_key_size;
	const char *first_key = ini_section_name_key(first, &first_key_size);
	CHECK(first_key != NULL && first_key_size == 5
	    && memcmp(first_key, "alpha", first_key_size) == 0);
	CHECK(strcmp(ini_section_name(second), "alpha") == 0);
	CHECK(strcmp(ini_section_name(unicode), "Bad\xc3\xa9") == 0);
	CHECK(strcmp(ini_section_name(beta), "Beta") == 0);
	CHECK(ini_document_find_section(document, "ALPHA") == second);
	CHECK(ini_section_count(first) == 2);
	CHECK(strcmp(ini_section_find(first, "KEY"), "second") == 0);
	pair = ini_section_at(first, 0);
	CHECK(pair != NULL && strcmp(pair->name, "Key") == 0
	    && strcmp(pair->value, "first") == 0);
	CHECK(pair->name_key != NULL && pair->name_key_size == 3
	    && memcmp(pair->name_key, "key", pair->name_key_size) == 0);
	CHECK(ini_section_at(first, 2) == NULL);
	CHECK(strcmp(ini_section_find(unicode, "Hidden"), "no") == 0);
	CHECK(!ini_section_has_key(second, "StillHidden"));
	CHECK(strcmp(ini_section_find(beta, "Raw"), "\xc3\xa9") == 0);
	CHECK(strcmp(ini_section_find(beta, "Last"), "no-newline") == 0);

	CHECK(name_value_list_append(&pairs, "old", "value"));
	CHECK(ini_section_copy_pairs(first, &pairs) == 0);
	CHECK(name_value_list_count(pairs) == 2);
	CHECK(strcmp(pairs[0]->name, "Key") == 0);
	CHECK(strcmp(pairs[1]->value, "second") == 0);
	name_value_list_free(&pairs);

	written = document_text(document);
	CHECK(written != NULL && strcmp(written, input) == 0);
	free(written);
	ini_document_free(&document);
	CHECK(document == NULL);

	stream = stream_from("Only=one\n", sizeof("Only=one\n") - 1);
	CHECK(stream != NULL && ini_document_read(&document, stream) == 0);
	fclose(stream);
	written = document_text(document);
	CHECK(written != NULL && strcmp(written, "Only=one\n") == 0);
	free(written);
	ini_document_free(&document);
	stream = stream_from("", 0);
	CHECK(stream != NULL && ini_document_read(&document, stream) == 0);
	fclose(stream);
	CHECK(ini_section_count(ini_document_root(document)) == 0);
	ini_document_free(&document);
	CHECK(ini_document_parse(&document, input, sizeof(input) - 1) == 0);
	char *serialized = NULL;
	size_t serialized_size = 0;
	CHECK(ini_document_serialize(document, &serialized, &serialized_size) == 0);
	CHECK(serialized_size == sizeof(input) - 1
	    && memcmp(serialized, input, serialized_size) == 0
	    && serialized[serialized_size] == '\0');
	free(serialized);
	ini_document_free(&document);
	CHECK(ini_document_parse(&document, NULL, 0) == 0);
	ini_document_free(&document);
	CHECK(ini_document_parse(&document, decomposed,
	    sizeof(decomposed) - 1) == 0);
	CHECK(strcmp(ini_section_find(ini_document_find_section(document,
	    "Caf\xc3\xa9"),
	    "Value"), "\xc3\xa9") == 0);
	size_t cafe_key_size;
	const char *cafe_key = ini_section_name_key(
	    ini_document_find_section(document, "CAF\xc3\x89"), &cafe_key_size);
	CHECK(cafe_key != NULL && cafe_key_size == 6
	    && memcmp(cafe_key, "cafe\xcc\x81", cafe_key_size) == 0);
	serialized = NULL;
	serialized_size = 0;
	CHECK(ini_document_serialize(document, &serialized, &serialized_size) == 0);
	CHECK(serialized_size == sizeof(composed) - 1
	    && memcmp(serialized, composed, serialized_size) == 0);
	free(serialized);
	ini_document_free(&document);
	return true;
}

static bool
test_result_replacement_and_invalid_input(void)
{
	static const unsigned char embedded_nul[] = {'A', '=', '1', 0, '\n'};
	static const unsigned char malformed_utf8[] = {'A', '=', 0x80, '\n'};
	ini_document_t *document = NULL;
	ini_document_t *other = NULL;
	ini_document_t *original;
	ini_section_t *section = NULL;
	ini_section_t *other_section = NULL;
	name_value_list_t pairs = NULL;
	str_list_t strings = NULL;
	int64_t *integers = NULL;
	size_t count = 99;
	FILE *stream;
	FILE *read_only;
	struct ini_write_style style = ini_default_style;

	CHECK(ini_document_create(&document) == 0);
	CHECK(ini_section_set_string(ini_document_root(document), "Keep", "yes",
	    NULL) == 0);
	original = document;
	stream = stream_from(embedded_nul, sizeof(embedded_nul));
	CHECK(stream != NULL);
	CHECK(ini_document_read(&document, stream) == IO_ERROR_INVALID_ENCODING);
	CHECK(document == original);
	CHECK(strcmp(ini_section_find(ini_document_root(document), "Keep"),
	    "yes") == 0);
	fclose(stream);
	stream = stream_from(malformed_utf8, sizeof(malformed_utf8));
	CHECK(stream != NULL);
	CHECK(ini_document_read(&document, stream) == IO_ERROR_INVALID_ENCODING);
	CHECK(document == original);
	fclose(stream);

	CHECK(ini_document_create(NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_document_parse(NULL, "", 0) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_document_parse(&document, NULL, 1)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_document_serialize(NULL, NULL, NULL)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_document_read(NULL, stdin) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_document_read(&document, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_document_write(NULL, stdout) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_document_write(document, NULL) == IO_ERROR_INVALID_ARGUMENT);
	read_only = fopen(__FILE__, "rb");
	CHECK(read_only != NULL);
	CHECK(ini_document_write(document, read_only) == IO_ERROR_IO);
	fclose(read_only);
	CHECK(ini_document_root(NULL) == NULL);
	CHECK(ini_document_section_count(NULL) == 0);
	CHECK(ini_document_section_at(NULL, 0) == NULL);
	CHECK(ini_document_section_at(document, 0) == NULL);
	CHECK(ini_document_find_section(NULL, "x") == NULL);
	CHECK(ini_document_find_section(document, NULL) == NULL);
	CHECK(ini_document_find_section(document, "missing") == NULL);
	CHECK(ini_section_name(NULL) == NULL);
	CHECK(ini_section_count(NULL) == 0);
	CHECK(ini_section_at(NULL, 0) == NULL);
	CHECK(ini_section_find(NULL, "x") == NULL);
	CHECK(ini_section_find(ini_document_root(document), NULL) == NULL);
	CHECK(!ini_section_has_key(NULL, "x"));
	CHECK(ini_section_copy_pairs(NULL, &pairs) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_copy_pairs(ini_document_root(document), NULL)
	    == IO_ERROR_INVALID_ARGUMENT);

	CHECK(ini_document_add_section(NULL, "x", NULL, &section)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_document_add_section(document, "", NULL, &section)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_document_add_section(document, " bad", NULL, &section)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_document_add_section(document, "bad]", NULL, &section)
	    == IO_ERROR_INVALID_ARGUMENT);
	style.section_separator = NULL;
	CHECK(ini_document_add_section(document, "Valid", &style, &section)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(section == NULL);
	CHECK(ini_document_add_section(document, "Default", NULL, &section) == 0);
	CHECK(section != NULL);
	CHECK(ini_document_create(&other) == 0);
	CHECK(ini_document_add_section(other, "Other", NULL, &other_section) == 0);
	CHECK(!ini_document_remove_section(NULL, NULL));
	CHECK(!ini_document_remove_section(document, ini_document_root(document)));
	CHECK(!ini_document_remove_section(document, other_section));
	CHECK(!ini_document_rename_section(document, ini_document_root(document),
	    "x"));
	CHECK(!ini_document_rename_section(document, other_section, "x"));
	CHECK(!ini_document_rename_section(document, section, " bad"));

	CHECK(ini_section_get_string_list(NULL, "x", ",", &strings)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_get_string_list(ini_document_root(document), NULL, ",",
	    &strings) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_get_string_list(ini_document_root(document), "x", NULL,
	    &strings) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_get_string_list(ini_document_root(document), "x", ",",
	    NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_get_int64_list(ini_document_root(document), "x", ",",
	    NULL, &count) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_get_int64_list(ini_document_root(document), "x", ",",
	    &integers, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_set_string(NULL, "x", "y", NULL)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_set_string(ini_document_root(document), "bad=key", "y",
	    NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_set_string(ini_document_root(document), "x", NULL, NULL)
	    == IO_ERROR_INVALID_ARGUMENT);
	style = ini_default_style;
	style.key_prefix = NULL;
	CHECK(ini_section_set_string(ini_document_root(document), "x", "y", &style)
	    == IO_ERROR_INVALID_ARGUMENT);
	style = ini_default_style;
	style.value_separator = "";
	CHECK(ini_section_set_string(ini_document_root(document), "x", "y", &style)
	    == IO_ERROR_INVALID_ARGUMENT);
	style = ini_default_style;
	style.literal_separator = "::";
	CHECK(ini_section_set_string(ini_document_root(document), "x", "y", &style)
	    == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_set_string_list(ini_document_root(document), "x", NULL,
	    NULL, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_set_int64_list(ini_document_root(document), "x", NULL, 1,
	    ",", NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_set_int64_list(ini_document_root(document), "x", NULL, 0,
	    NULL, NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_set_double(ini_document_root(document), "x", NAN, NULL)
	    == IO_ERROR_OUT_OF_RANGE);
	CHECK(ini_section_set_bitfield(ini_document_root(document), "x", NULL, 1,
	    1, NULL) == IO_ERROR_INVALID_ARGUMENT);
	style = ini_default_style;
	style.bit_separator = NULL;
	CHECK(ini_section_set_bitfield(ini_document_root(document), "x", NULL, 0,
	    0, &style) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_remove_key(NULL, "x") == 0);
	CHECK(ini_section_remove_key(ini_document_root(document), NULL) == 0);

	ini_document_free(NULL);
	ini_document_free(&other);
	ini_document_free(&document);
	ini_document_free(&document);
	return true;
}

static bool
test_typed_values(void)
{
	static const struct ini_bit_name bits[] = {
		{1, "Read"}, {4, "Write"}
	};
	static const char *const constant_names[] = { "First", "Second" };
	static const int64_t integer_values[] = {INT64_MIN, 0, INT64_MAX};
	ini_document_t *document = NULL;
	ini_section_t *root;
	str_list_t names = NULL;
	str_list_t values = NULL;
	str_list_t parsed_strings = NULL;
	int64_t *parsed_integers = NULL;
	size_t count = 0;

	CHECK(ini_document_create(&document) == 0);
	root = ini_document_root(document);
	CHECK(ini_section_set_int64(root, "Signed", INT64_MIN, NULL) == 0);
	CHECK(ini_section_get_int64(root, "signed", 7) == INT64_MIN);
	CHECK(ini_section_set_uint64(root, "Unsigned", UINT64_MAX, NULL) == 0);
	CHECK(ini_section_get_uint64(root, "unsigned", 7) == UINT64_MAX);
	CHECK(ini_section_set_double(root, "Double", DBL_MAX, NULL) == 0);
	CHECK(ini_section_get_double(root, "double", 0.0) == DBL_MAX);
	CHECK(ini_section_set_double(root, "Negative", -1.0, NULL) == 0);
	CHECK(ini_section_get_double(root, "negative", 0.0) == -1.0);
	CHECK(ini_section_set_bool(root, "Boolean", true, NULL) == 0);
	CHECK(ini_section_get_bool(root, "boolean", false));
	CHECK(ini_section_set_duration(root, "Duration", 0.125, NULL) == 0);
	CHECK(ini_section_get_duration(root, "duration", 0.0) == 0.125);
	CHECK(ini_section_set_datetime(root, "Date", (time_t)0, false, NULL) == 0);
	CHECK(ini_section_get_datetime(root, "date", (time_t)-1) == 0);
	CHECK(ini_section_set_datetime(root, "DateTime", (time_t)0, true, NULL)
	    == 0);
	CHECK(ini_section_get_datetime(root, "datetime", (time_t)-1) == 0);

	CHECK(str_list_append(&names, "Zero"));
	CHECK(str_list_append(&names, "One"));
	CHECK(ini_section_set_enum(root, "EnumName", names, 1, NULL) == 0);
	CHECK(ini_section_get_enum(root, "enumname", names, 9) == 1);
	CHECK(ini_section_set_enum(root, "EnumNumber", names, 7, NULL) == 0);
	CHECK(ini_section_get_enum(root, "enumnumber", names, 9) == 7);
	CHECK(ini_section_set_enum_names(root, "ConstantEnum", constant_names,
	    2, 1, NULL) == 0);
	CHECK(ini_section_get_enum_names(root, "constantenum", constant_names,
	    2, 9) == 1);

	CHECK(ini_section_set_bitfield(root, "Bits", bits, 2, 7, NULL) == 0);
	CHECK(strcmp(ini_section_find(root, "bits"), "Read|Write|2") == 0);
	CHECK(ini_section_get_bitfield(root, "bits", bits, 2, 0) == 7);
	CHECK(ini_section_set_bitfield(root, "NoBits", bits, 2, 0, NULL) == 0);
	CHECK(strcmp(ini_section_find(root, "nobits"), "") == 0);
	CHECK(ini_section_get_bitfield(root, "missing", bits, 2, 42) == 42);

	CHECK(str_list_append(&values, "alpha"));
	CHECK(str_list_append(&values, "beta"));
	CHECK(ini_section_set_string_list(root, "Strings", values, ", ", NULL)
	    == 0);
	CHECK(ini_section_get_string_list(root, "strings", ", ",
	    &parsed_strings) == 0);
	CHECK(str_list_count(parsed_strings) == 2);
	CHECK(strcmp(parsed_strings[0], "alpha") == 0);
	CHECK(strcmp(parsed_strings[1], "beta") == 0);
	CHECK(ini_section_set_int64_list(root, "Integers", integer_values, 3, ",",
	    NULL) == 0);
	CHECK(ini_section_get_int64_list(root, "integers", ",",
	    &parsed_integers, &count) == 0);
	CHECK(count == 3 && parsed_integers[0] == INT64_MIN
	    && parsed_integers[1] == 0 && parsed_integers[2] == INT64_MAX);
	ini_free_int64_list(parsed_integers);
	parsed_integers = NULL;

	CHECK(ini_section_set_string(root, "BadSigned", "12x", NULL) == 0);
	CHECK(ini_section_set_string(root, "BadUnsigned", "-1", NULL) == 0);
	CHECK(ini_section_set_string(root, "BadDouble", "1.2x", NULL) == 0);
	CHECK(ini_section_set_string(root, "BadBool", "perhaps", NULL) == 0);
	CHECK(ini_section_set_string(root, "BadDuration", "soon", NULL) == 0);
	CHECK(ini_section_set_string(root, "BadDate", "yesterday", NULL) == 0);
	CHECK(ini_section_set_string(root, "BadEnum", "unknown", NULL) == 0);
	CHECK(ini_section_set_string(root, "BadBits", "Read|unknown", NULL) == 0);
	CHECK(ini_section_set_string(root, "BadIntegers", "1,no", NULL) == 0);
	CHECK(ini_section_get_int64(root, "BadSigned", 11) == 11);
	CHECK(ini_section_get_uint64(root, "BadUnsigned", 12) == 12);
	CHECK(ini_section_get_double(root, "BadDouble", 1.5) == 1.5);
	CHECK(ini_section_get_bool(root, "BadBool", true));
	CHECK(ini_section_get_duration(root, "BadDuration", 2.5) == 2.5);
	CHECK(ini_section_get_datetime(root, "BadDate", (time_t)23) == 23);
	CHECK(ini_section_get_enum(root, "BadEnum", names, 13) == 13);
	CHECK(ini_section_get_bitfield(root, "BadBits", bits, 2, 14) == 14);
	CHECK(ini_section_get_bitfield(root, "BadBits", NULL, 2, 15) == 15);
	count = 88;
	parsed_integers = malloc(sizeof(*parsed_integers));
	CHECK(parsed_integers != NULL);
	parsed_integers[0] = 123;
	{
		int64_t *previous = parsed_integers;

		CHECK(ini_section_get_int64_list(root, "BadIntegers", ",",
	    &parsed_integers, &count) == IO_ERROR_INVALID_ARGUMENT);
		CHECK(parsed_integers == previous && parsed_integers[0] == 123);
		CHECK(count == 88);
	}
	ini_free_int64_list(parsed_integers);
	parsed_integers = NULL;

	CHECK(str_list_append(&parsed_strings, "old"));
	CHECK(ini_section_get_string_list(root, "missing", ",", &parsed_strings)
	    == 0);
	CHECK(parsed_strings == NULL);
	CHECK(strcmp(ini_section_get_string(root, "missing", "fallback"),
	    "fallback") == 0);

	str_list_free(&parsed_strings);
	str_list_free(&values);
	str_list_free(&names);
	ini_document_free(&document);
	return true;
}

static bool
test_mutation(void)
{
	static const char input[] =
	    "; root\r\n"
	    "Root = old  \r\n"
	    "\r\n"
	    "[One]\r\n"
	    "A=first\r\n"
	    "a=second\r\n"
	    "; before two\r\n"
	    "[Two]\r\n"
	    "K=V\r\n";
	ini_document_t *document = NULL;
	ini_section_t *one;
	ini_section_t *two;
	ini_section_t *three = NULL;
	struct ini_write_style style = ini_default_style;
	FILE *stream = stream_from(input, sizeof(input) - 1);
	char *text;

	CHECK(stream != NULL && ini_document_read(&document, stream) == 0);
	fclose(stream);
	one = ini_document_section_at(document, 0);
	two = ini_document_section_at(document, 1);
	CHECK(ini_section_set_string(ini_document_root(document), "root", "new",
	    NULL) == 0);
	CHECK(ini_section_set_string(one, "A", " spaced\t", NULL) == 0);
	CHECK(strcmp(ini_section_find(one, "a"), " spaced\t") == 0);
	CHECK(ini_section_set_string(one, "Escapes",
	    " \\ \"\n\r\t\v\f\x01", NULL) == 0);
	CHECK(strcmp(ini_section_find(one, "escapes"),
	    " \\ \"\n\r\t\v\f\x01") == 0);
	CHECK(ini_section_remove_key(one, "a") == 2);
	CHECK(ini_section_count(one) == 1);
	CHECK(!ini_section_has_key(one, "A"));

	style.key_width = 6;
	style.key_prefix = "\t";
	style.section_separator = "\n";
	CHECK(ini_section_set_string(two, "New", "value", &style) == 0);
	CHECK(ini_document_add_section(document, "Three", &style, &three) == 0);
	CHECK(three != NULL && strcmp(ini_section_name(three), "Three") == 0);
	CHECK(ini_document_section_count(document) == 3);
	CHECK(ini_document_rename_section(document, three, "two"));
	CHECK(strcmp(ini_section_name(three), "two") == 0);
	size_t renamed_key_size;
	const char *renamed_key = ini_section_name_key(three,
	    &renamed_key_size);
	CHECK(renamed_key != NULL && renamed_key_size == 3
	    && memcmp(renamed_key, "two", renamed_key_size) == 0);
	CHECK(ini_document_find_section(document, "TWO") == three);
	CHECK(!ini_document_remove_section(document, ini_document_root(document)));
	CHECK(ini_document_remove_section(document, one));
	CHECK(ini_document_section_count(document) == 2);
	CHECK(ini_document_section_at(document, 0) == two);

	text = document_text(document);
	CHECK(text != NULL);
	CHECK(strstr(text, "Root = new  \r\n") != NULL);
	CHECK(strstr(text, "\tNew   =value\r\n") != NULL);
	CHECK(strstr(text, "\n[two]\r\n") != NULL);
	CHECK(strstr(text, "[One]") == NULL);
	free(text);
	ini_document_free(&document);
	return true;
}

static bool
test_clone(void)
{
	static const char input[] =
	    "; comment\n\n[One]\nKey=first\n[One]\nKey=second\n";
	ini_document_t *document = NULL;
	ini_document_t *clone = NULL;
	char *original;
	char *copied;

	CHECK(ini_document_parse(&document, input, sizeof(input) - 1) == 0);
	CHECK(ini_document_clone(&clone, document) == 0);
	original = document_text(document);
	copied = document_text(clone);
	CHECK(original != NULL && copied != NULL);
	CHECK(strcmp(original, copied) == 0);
	free(original);
	free(copied);
	CHECK(ini_section_set_string(ini_document_find_section(clone, "one"),
	    "Key", "changed", NULL) == 0);
	CHECK(strcmp(ini_section_find(ini_document_find_section(document, "one"),
	    "Key"), "second") == 0);
	CHECK(ini_section_get_bounded_int64(ini_document_root(document),
	    "missing", 5, 0, 10) == 5);
	ini_document_free(&clone);
	ini_document_free(&document);
	return true;
}

static bool
test_zero_bitfield(void)
{
	static const struct ini_bit_name bits[] = {{1, "Read"}, {4, "Write"}};
	ini_document_t *document = NULL;
	uint64_t value;

	CHECK(ini_document_create(&document) == 0);
	CHECK(ini_section_set_bitfield(ini_document_root(document), "Bits",
	    bits, 2, 0, NULL) == 0);
	value = ini_section_get_bitfield(ini_document_root(document), "Bits",
	    bits, 2, 42);
	ini_document_free(&document);
	if (value != 0)
		fprintf(stderr, "zero bitfield read as %llu\n", (unsigned long long)value);
	CHECK(value == 0);
	return true;
}

static bool
test_key_name_grammar(void)
{
	static const char *const invalid_names[] = {
		NULL, "", " leading", "trailing ", "bad=key", "bad:key",
		";comment", "[section]",
	};
	static const char non_ascii_name[] = {'b', (char)0x80, 'd', '\0'};
	ini_document_t *document = NULL;
	ini_document_t *parsed = NULL;
	char *text = NULL;
	size_t size = 0;

	CHECK(ini_key_name_valid("Key"));
	CHECK(ini_key_name_valid("Key[Part]"));
	CHECK(ini_key_name_valid("M\xc3\xbcnchen"));
	for (size_t i = 0;
	    i < sizeof(invalid_names) / sizeof(invalid_names[0]); ++i) {
		CHECK(!ini_key_name_valid(invalid_names[i]));
	}
	CHECK(!ini_key_name_valid(non_ascii_name));
	CHECK(ini_document_parse(&document, "Keep=yes\n", 9) == 0);
	for (size_t i = 0;
	    i < sizeof(invalid_names) / sizeof(invalid_names[0]); ++i) {
		CHECK(ini_section_set_string(ini_document_root(document),
		    invalid_names[i], "value", NULL) == IO_ERROR_INVALID_ARGUMENT);
	}
	CHECK(ini_section_set_string(ini_document_root(document), non_ascii_name,
	    "value", NULL) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(ini_section_set_string(ini_document_root(document), "Key[Part]",
	    "value", NULL) == 0);
	CHECK(ini_document_serialize(document, &text, &size) == 0);
	CHECK(strcmp(text, "Keep=yes\nKey[Part]=value\n") == 0);
	CHECK(ini_document_parse(&parsed, text, size) == 0);
	CHECK(strcmp(ini_section_find(ini_document_root(parsed), "Keep"),
	    "yes") == 0);
	CHECK(strcmp(ini_section_find(ini_document_root(parsed), "Key[Part]"),
	    "value") == 0);
	free(text);
	ini_document_free(&parsed);
	ini_document_free(&document);
	return true;
}

static bool
test_append_after_unterminated_line(void)
{
	static const struct {
		const char *input;
		const char *section_name;
		const char *expected;
		const char *prior_key;
		const char *prior_value;
	} cases[] = {
		{"First=one", NULL, "First=one\nSecond=two\n", "First", "one"},
		{"[Section]", "Section", "[Section]\nSecond=two\n", NULL, NULL},
		{"[Section]\r\nFirst=one", "Section",
		    "[Section]\r\nFirst=one\r\nSecond=two\r\n", "First", "one"},
		{"First=one\n; trailing", NULL,
		    "First=one\nSecond=two\n; trailing", "First", "one"},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		ini_document_t *document = NULL;
		ini_document_t *parsed = NULL;
		ini_section_t *section;
		char *text = NULL;
		size_t size = 0;

		CHECK(ini_document_parse(&document, cases[i].input,
		    strlen(cases[i].input)) == 0);
		section = cases[i].section_name == NULL ? ini_document_root(document)
		    : ini_document_find_section(document, cases[i].section_name);
		CHECK(ini_section_set_string(section, "Second", "two", NULL) == 0);
		CHECK(ini_document_serialize(document, &text, &size) == 0);
		CHECK(strcmp(text, cases[i].expected) == 0);
		CHECK(ini_document_parse(&parsed, text, size) == 0);
		section = cases[i].section_name == NULL ? ini_document_root(parsed)
		    : ini_document_find_section(parsed, cases[i].section_name);
		CHECK(section != NULL);
		CHECK(strcmp(ini_section_find(section, "Second"), "two") == 0);
		if (cases[i].prior_key != NULL)
			CHECK(strcmp(ini_section_find(section, cases[i].prior_key),
			    cases[i].prior_value) == 0);
		free(text);
		ini_document_free(&parsed);
		ini_document_free(&document);
	}
	return true;
}

static bool
test_add_section_after_unterminated_line(void)
{
	static const struct {
		const char *input;
		const char *separator;
		const char *expected;
	} cases[] = {
		{"First=one", "", "First=one\n[Added]\n"},
		{"[Section]", "", "[Section]\n[Added]\n"},
		{"First=one\r\nLast=two", "",
		    "First=one\r\nLast=two\r\n[Added]\r\n"},
		{"First=one\n; trailing", "",
		    "First=one\n; trailing\n[Added]\n"},
		{"First=one", "\n", "First=one\n\n[Added]\n"},
		{"First=one", "; divide", "First=one\n; divide\n[Added]\n"},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		struct ini_write_style style = ini_default_style;
		ini_document_t *document = NULL;
		ini_document_t *parsed = NULL;
		ini_section_t *added = NULL;
		char *text = NULL;
		size_t size = 0;

		style.section_separator = cases[i].separator;
		CHECK(ini_document_parse(&document, cases[i].input,
		    strlen(cases[i].input)) == 0);
		CHECK(ini_document_add_section(document, "Added", &style,
		    &added) == 0);
		CHECK(added != NULL);
		CHECK(ini_document_serialize(document, &text, &size) == 0);
		CHECK(strcmp(text, cases[i].expected) == 0);
		CHECK(ini_document_parse(&parsed, text, size) == 0);
		CHECK(ini_document_find_section(parsed, "Added") != NULL);
		if (strncmp(cases[i].input, "First=one", 9) == 0)
			CHECK(strcmp(ini_section_find(ini_document_root(parsed),
			    "First"), "one") == 0);
		else
			CHECK(ini_document_find_section(parsed, "Section") != NULL);
		free(text);
		ini_document_free(&parsed);
		ini_document_free(&document);
	}
	return true;
}

static bool
test_cached_name_lookup(void)
{
	static const char input[] =
	    "[Names]\n"
	    "Stra\xc3\x9f" "e=sharp\n"
	    "Caf\xc3\xa9=accent\n"
	    "MixedCase=ascii\n";
	char long_name[140];
	ini_document_t *document = NULL;
	ini_section_t *section;

	CHECK(ini_document_parse(&document, input, sizeof(input) - 1) == 0);
	section = ini_document_find_section(document, "NAMES");
	CHECK(section != NULL);
	CHECK(strcmp(ini_section_find(section, "STRASSE"), "sharp") == 0);
	CHECK(strcmp(ini_section_find(section, "CAF\xc3\x89"), "accent") == 0);
	CHECK(strcmp(ini_section_find(section, "mixedcase"), "ascii") == 0);
	memset(long_name, 'A', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';
	CHECK(ini_section_set_string(section, long_name, "long", NULL) == 0);
	memset(long_name, 'a', sizeof(long_name) - 1);
	CHECK(strcmp(ini_section_find(section, long_name), "long") == 0);
	ini_document_free(&document);
	return true;
}

static bool
test_unterminated_record_without_insertion(void)
{
	ini_document_t *document = NULL;
	ini_document_t *clone = NULL;
	char *text = NULL;
	size_t size = 0;

	CHECK(ini_document_parse(&document, "First=one", 9) == 0);
	CHECK(ini_section_set_string(ini_document_root(document), "First",
	    "changed", NULL) == 0);
	CHECK(ini_document_serialize(document, &text, &size) == 0);
	CHECK(size == 13 && strcmp(text, "First=changed") == 0);
	free(text);
	text = NULL;
	CHECK(ini_document_clone(&clone, document) == 0);
	CHECK(ini_document_serialize(clone, &text, &size) == 0);
	CHECK(size == 13 && strcmp(text, "First=changed") == 0);
	free(text);
	ini_document_free(&clone);
	ini_document_free(&document);
	return true;
}

int
main(void)
{
	if (!test_document_and_parse()
	    || !test_result_replacement_and_invalid_input()
	    || !test_typed_values()
	    || !test_mutation()
	    || !test_clone())
		return EXIT_FAILURE;
	bool passed = test_append_after_unterminated_line();
	passed = test_add_section_after_unterminated_line() && passed;
	passed = test_unterminated_record_without_insertion() && passed;
	passed = test_key_name_grammar() && passed;
	passed = test_cached_name_lookup() && passed;
	passed = test_zero_bitfield() && passed;
	return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
