/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "ini.h"
#include "io_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #c); return EXIT_FAILURE; } } while (0)

static char *
stream_contents(FILE *stream)
{
	long length;
	char *result;

	if (fflush(stream) != 0 || fseek(stream, 0, SEEK_END) != 0)
		return NULL;
	length = ftell(stream);
	if (length < 0 || fseek(stream, 0, SEEK_SET) != 0)
		return NULL;
	result = malloc((size_t)length + 1);
	if (result == NULL)
		return NULL;
	if (fread(result, 1, (size_t)length, stream) != (size_t)length) {
		free(result);
		return NULL;
	}
	result[length] = '\0';
	return result;
}

int
main(int argc, char **argv)
{
	static const char input[] =
	    "; top\r\n"
	    "Root=one\r\n"
	    "\r\n"
	    "[Sec]\r\n"
	    "Key=first\r\n"
	    "; attached\r\n"
	    "Key:\" spaced \"\r\n"
	    "[sec]\r\n"
	    "Key=last\r\n"
	    "Bad\xc3\xa9=hidden\r\n"
	    "[Invalid\xc3\xa9]\r\n"
	    "Leaked=no\r\n";
	struct ini_write_style style = ini_default_style;
	ini_document_t *document = NULL;
	ini_document_t *empty = NULL;
	ini_section_t *section;
	ini_section_t *first;
	FILE *source = tmpfile();
	FILE *output = tmpfile();
	FILE *after = tmpfile();
	char *written;
	long source_position;
	str_list_t names = NULL;
	name_value_list_t pairs = NULL;

	CHECK(source != NULL && output != NULL && after != NULL);
	CHECK(fwrite(input, 1, sizeof(input) - 1, source) == sizeof(input) - 1);
	CHECK(fseek(source, 7, SEEK_SET) == 0);
	source_position = ftell(source);
	CHECK(source_position == 7);
	CHECK(ini_document_read(&document, source) == 0);
	CHECK(ftell(source) == source_position);
	CHECK(ini_document_section_count(document) == 2);
	CHECK(strcmp(ini_section_find(ini_document_root(document), "root"),
	    "one") == 0);
	section = ini_document_find_section(document, "SEC");
	CHECK(section == ini_document_section_at(document, 1));
	CHECK(strcmp(ini_section_find(section, "key"), "last") == 0);
	CHECK(!ini_section_has_key(section, "Bad\xc3\xa9"));
	CHECK(!ini_section_has_key(section, "Leaked"));
	first = ini_document_section_at(document, 0);
	CHECK(strcmp(ini_section_find(first, "KEY"), " spaced ") == 0);
	CHECK(ini_section_copy_pairs(first, &pairs) == 0);
	CHECK(name_value_list_count(pairs) == 2);
	name_value_list_free(&pairs);
	CHECK(ini_document_write(document, output) == 0);
	written = stream_contents(output);
	CHECK(written != NULL && strcmp(written, input) == 0);
	free(written);

	CHECK(ini_section_set_double(section, "Scale", 1.25, NULL) == 0);
	CHECK(ini_section_get_double(section, "scale", 0.0) == 1.25);
	CHECK(ini_section_set_bool(section, "Enabled", true, NULL) == 0);
	CHECK(ini_section_get_bool(section, "enabled", false));
	CHECK(ini_section_set_string(section, "Spaced", " value ", NULL) == 0);
	CHECK(strcmp(ini_section_find(section, "spaced"), " value ") == 0);
	CHECK(ini_section_remove_key(section, "KEY") == 1);
	CHECK(!ini_section_has_key(section, "key"));
	CHECK(ini_document_remove_section(document, first));
	CHECK(ini_document_write(document, after) == 0);
	written = stream_contents(after);
	CHECK(written != NULL && strstr(written, "; attached\r\n[sec]") != NULL);
	free(written);

	CHECK(str_list_append(&names, "zero"));
	CHECK(str_list_append(&names, "one"));
	CHECK(ini_section_set_enum(section, "Enum", names, 1, NULL) == 0);
	CHECK(ini_section_get_enum(section, "enum", names, 0) == 1);
	str_list_free(&names);

	CHECK(ini_document_create(&empty) == 0);
	style.key_width = 8;
	style.key_prefix = "\t";
	style.section_separator = "\n";
	CHECK(ini_section_set_int64(ini_document_root(empty), "Count", -2,
	    &style) == 0);
	CHECK(ini_document_add_section(empty, "New", &style, &section) == 0);
	{
		ini_section_t *earlier = section;
		ini_section_t *later;

		CHECK(ini_document_add_section(empty, "Later", &style, &later) == 0);
		CHECK(ini_document_rename_section(empty, earlier, "Later"));
		CHECK(ini_document_find_section(empty, "later") == later);
		section = later;
	}
	CHECK(ini_section_set_duration(section, "Wait", 7200.0, &style) == 0);
	CHECK(ini_section_get_duration(section, "wait", 0.0) == 7200.0);
	{
		static const int64_t values[] = {-1, 2, 3};
		int64_t *parsed = NULL;
		size_t count = 0;

		CHECK(ini_section_set_int64_list(section, "Numbers", values, 3, ",",
		    &style) == 0);
		CHECK(ini_section_get_int64_list(section, "numbers", ",", &parsed,
		    &count) == 0);
		CHECK(count == 3 && parsed[0] == -1 && parsed[2] == 3);
		ini_free_int64_list(parsed);
	}
	CHECK(ini_section_set_datetime(section, "When", (time_t)0, true,
	    &style) == 0);
	CHECK(ini_section_get_datetime(section, "when", (time_t)-1) == 0);

	ini_document_free(&empty);
	ini_document_free(&document);
	fclose(after);
	fclose(output);
	fclose(source);
	if (argc > 1) {
		FILE *fixture = fopen(argv[1], "rb");

		CHECK(fixture != NULL);
		CHECK(ini_document_read(&document, fixture) == 0);
		CHECK(ini_document_section_count(document) > 0
		    || ini_section_count(ini_document_root(document)) > 0);
		ini_document_free(&document);
		fclose(fixture);
	}
	return EXIT_SUCCESS;
}
