/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_features.h"

#include "io_error.h"

#include <string.h>

enum category {
	AUDIO, EMULATIONS, IMAGE, LANGUAGES, MISC, PROTOCOLS, CATEGORY_COUNT,
};

static const char *const categories[] = {
	"_AUDIO", "_EMULATIONS", "_IMAGE", "_LANGUAGES", "_MISC", "_PROTOCOLS",
};

/* Wire names are ordered alphabetically, independently of locale. */
static const struct {
	const char *name;
	enum category category;
} entries[] = {
	{"ANSI",EMULATIONS}, {"BMP",IMAGE}, {"CISQUICKB",PROTOCOLS},
	{"DOORWAY",EMULATIONS}, {"ENG",LANGUAGES}, {"EXTAPPS",MISC},
	{"JPEG",IMAGE}, {"KERMIT",PROTOCOLS}, {"RIPSCRIP",EMULATIONS},
	{"SUPERKERMIT",PROTOCOLS}, {"VT102",EMULATIONS}, {"WAV",AUDIO},
	{"XMODEM",PROTOCOLS}, {"XMODEM1K",PROTOCOLS}, {"XMODEM1KG",PROTOCOLS},
	{"XMODEMCRC",PROTOCOLS}, {"YMODEM",PROTOCOLS}, {"YMODEMG",PROTOCOLS},
	{"ZMODEM",PROTOCOLS}, {"ZMODEMCR",PROTOCOLS},
};

_Static_assert(sizeof(entries) / sizeof(*entries) == ST_RIP3_FEATURE_COUNT,
    "Every feature needs a wire keyword/category");
_Static_assert(ST_RIP3_FEATURE_COUNT <= 32, "Features must fit the private mask");
_Static_assert(sizeof(categories) / sizeof(*categories) == CATEGORY_COUNT,
    "Every category needs a wire keyword");

static bool
equal(const char *text, const char *uppercase)
{
	for (size_t i = 0; ; i++) {
		unsigned char ch = (unsigned char)text[i];

		if (ch >= 'a' && ch <= 'z')
			ch = (unsigned char)(ch - 'a' + 'A');
		if (ch != (unsigned char)uppercase[i])
			return false;
		if (ch == 0)
			return true;
	}
}

static bool
supported(const struct st_rip3_features *features, unsigned index)
{
	return index != ST_RIP3_FEATURE_EXTAPPS
	    && (features->bits & (UINT32_C(1) << index)) != 0;
}

int
st_rip3_features_set(struct st_rip3_features *features,
    enum st_rip3_feature feature, bool enabled)
{
	uint32_t bit;

	if (features == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (feature < 0 || feature >= ST_RIP3_FEATURE_COUNT)
		return IO_ERROR_OUT_OF_RANGE;
	if (feature == ST_RIP3_FEATURE_EXTAPPS && enabled)
		return IO_ERROR_NOT_SUPPORTED;
	bit = UINT32_C(1) << (unsigned)feature;
	if (enabled)
		features->bits |= bit;
	else
		features->bits &= ~bit;
	return 0;
}

static int
append(char *output, size_t capacity, size_t *length, const char *word)
{
	size_t size = strlen(word);

	if (*length >= capacity || size >= capacity - *length
	    || (*length != 0 && size + 1 >= capacity - *length))
		return IO_ERROR_OUT_OF_RANGE;
	if (*length != 0)
		output[(*length)++] = ',';
	memcpy(output + *length, word, size);
	*length += size;
	return 0;
}

int
st_rip3_features_query(const struct st_rip3_features *features,
    const char *keyword, const char *category, char *result, size_t capacity)
{
	/* Each registered keyword is at most 12 bytes plus a comma. This also
	 * bounds the shorter category list. No caller output is used as scratch. */
	char output[ST_RIP3_FEATURE_COUNT * 13 + 1] = {0};
	bool defined[CATEGORY_COUNT] = {0};
	size_t length = 0;
	bool list;
	int status;

	if (features == NULL || keyword == NULL || result == NULL)
		return IO_ERROR_INVALID_ARGUMENT;
	if (keyword[0] == 0)
		return IO_ERROR_INVALID_FORMAT;
	list = equal(keyword, "LIST");
	if (category != NULL && !list)
		return IO_ERROR_INVALID_FORMAT;
	for (unsigned i = 0; i < ST_RIP3_FEATURE_COUNT; i++) {
		if (supported(features, i))
			defined[entries[i].category] = true;
	}
	if (list && category == NULL) {
		for (unsigned i = 0; i < CATEGORY_COUNT; i++) {
			if (defined[i]) {
				status = append(output, sizeof(output), &length, categories[i]);
				if (status != 0)
					return status;
			}
		}
	}
	else if (list) {
		for (unsigned i = 0; i < ST_RIP3_FEATURE_COUNT; i++) {
			if (supported(features, i)
			    && (equal(category, "ALL") || equal(category, categories[entries[i].category]))) {
				status = append(output, sizeof(output), &length, entries[i].name);
				if (status != 0)
					return status;
			}
		}
	}
	else {
		bool found = false;

		for (unsigned i = 0; i < ST_RIP3_FEATURE_COUNT; i++) {
			if (equal(keyword, entries[i].name))
				found = supported(features, i);
		}
		for (unsigned i = 0; i < CATEGORY_COUNT; i++) {
			if (equal(keyword, categories[i]))
				found = defined[i];
		}
		output[length++] = found ? '1' : '0';
	}
	if (length >= capacity)
		return IO_ERROR_OUT_OF_RANGE;
	output[length] = 0;
	memcpy(result, output, length + 1);
	return 0;
}
