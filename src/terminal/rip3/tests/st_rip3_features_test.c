/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_features.h"
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
expect(const struct st_rip3_features *features, const char *keyword,
    const char *category, const char *expected)
{
	char result[512], before[512];
	size_t length = strlen(expected);

	memset(result, 0xa5, sizeof(result));
	memcpy(before, result, sizeof(result));
	CHECK(st_rip3_features_query(features, keyword, category, result, length) == IO_ERROR_OUT_OF_RANGE);
	CHECK(memcmp(result, before, sizeof(result)) == 0);
	CHECK(st_rip3_features_query(features, keyword, category, result, length + 1) == 0);
	CHECK(strcmp(result, expected) == 0);
	CHECK(memcmp(result + length + 1, before + length + 1, sizeof(result) - length - 1) == 0);
	return 0;
}

int
main(void)
{
	struct st_rip3_features features = {0};
	const struct st_rip3_features empty = {0};
	char result[16], before[16];
	uint32_t old_bits;
	const char all[] = "ANSI,BMP,CISQUICKB,DOORWAY,ENG,JPEG,KERMIT,RIPSCRIP,SUPERKERMIT,VT102,WAV,XMODEM,XMODEM1K,XMODEM1KG,XMODEMCRC,YMODEM,YMODEMG,ZMODEM,ZMODEMCR";

	CHECK(expect(&features, "JPEG", NULL, "0") == 0);
	CHECK(expect(&features, "RIPSCRIP", NULL, "0") == 0);
	CHECK(expect(&features, "_EMULATIONS", NULL, "0") == 0);
	CHECK(expect(&features, "LIST", NULL, "") == 0);
	CHECK(expect(&features, "LIST", "ALL", "") == 0);
	CHECK(expect(&features, "LIST", "_IMAGE", "") == 0);
	CHECK(st_rip3_features_set(&features, ST_RIP3_FEATURE_ANSI, true) == 0);
	CHECK(st_rip3_features_set(&features, ST_RIP3_FEATURE_DOORWAY, true) == 0);
	CHECK(st_rip3_features_set(&features, ST_RIP3_FEATURE_RIPSCRIP, true) == 0);
	CHECK(st_rip3_features_set(&features, ST_RIP3_FEATURE_VT102, true) == 0);
	/* Literal emulation-category listing from the pinned reference. */
	CHECK(expect(&features, "LIST", "_EMULATIONS", "ANSI,DOORWAY,RIPSCRIP,VT102") == 0);
	CHECK(expect(&features, "LIST", NULL, "_EMULATIONS") == 0);
	CHECK(expect(&features, "_eMuLaTiOnS", NULL, "1") == 0);
	CHECK(expect(&features, "LiSt", "_eMuLaTiOnS", "ANSI,DOORWAY,RIPSCRIP,VT102") == 0);
	CHECK(expect(&features, "ripscrip", NULL, "1") == 0);
	CHECK(expect(&features, "_IMAGE", NULL, "0") == 0);
	CHECK(st_rip3_features_set(&features, ST_RIP3_FEATURE_JPEG, true) == 0);
	CHECK(expect(&features, "JPEG", NULL, "1") == 0);
	CHECK(expect(&features, "_IMAGE", NULL, "1") == 0);
	CHECK(expect(&features, "LIST", "_IMAGE", "JPEG") == 0);
	CHECK(st_rip3_features_set(&features, ST_RIP3_FEATURE_JPEG, false) == 0);
	CHECK(expect(&features, "JPEG", NULL, "0") == 0);
	CHECK(expect(&features, "_IMAGE", NULL, "0") == 0);
	CHECK(expect(&features, "LIST", "_IMAGE", "") == 0);
	CHECK(expect(&empty, "LIST", NULL, "") == 0); /* Independent session state. */
	for (unsigned i = 0; i < ST_RIP3_FEATURE_COUNT; i++) {
		int expected = i == ST_RIP3_FEATURE_EXTAPPS ? IO_ERROR_NOT_SUPPORTED : 0;
		CHECK(st_rip3_features_set(&features, (enum st_rip3_feature)i, true) == expected);
	}
	CHECK(expect(&features, "LIST", "ALL", all) == 0);
	CHECK(expect(&features, "LIST", NULL, "_AUDIO,_EMULATIONS,_IMAGE,_LANGUAGES,_PROTOCOLS") == 0);
	CHECK(expect(&features, "LIST", "_PROTOCOLS", "CISQUICKB,KERMIT,SUPERKERMIT,XMODEM,XMODEM1K,XMODEM1KG,XMODEMCRC,YMODEM,YMODEMG,ZMODEM,ZMODEMCR") == 0);
	CHECK(expect(&features, "LIST", "_AUDIO", "WAV") == 0);
	CHECK(expect(&features, "LIST", "_LANGUAGES", "ENG") == 0);
	CHECK(expect(&features, "LIST", "_IMAGE", "BMP,JPEG") == 0);
	CHECK(expect(&features, "LIST", "_MISC", "") == 0);
	CHECK(expect(&features, "EXTAPPS", NULL, "0") == 0);
	CHECK(expect(&features, "_MISC", NULL, "0") == 0);
	CHECK(expect(&features, "UNKNOWN", NULL, "0") == 0);
	CHECK(expect(&features, "_UNKNOWN", NULL, "0") == 0);
	CHECK(expect(&features, "LIST", "_UNKNOWN", "") == 0);
	CHECK(expect(&features, "LIST", "JPEG", "") == 0);
	CHECK(expect(&features, "LIST", "", "") == 0);
	/* Even an erroneous direct bit write cannot advertise excluded launching. */
	features.bits |= UINT32_C(1) << ST_RIP3_FEATURE_EXTAPPS;
	CHECK(expect(&features, "EXTAPPS", NULL, "0") == 0);
	CHECK(expect(&features, "LIST", "ALL", all) == 0);
	CHECK(st_rip3_features_set(&features, ST_RIP3_FEATURE_EXTAPPS, false) == 0);
	old_bits = features.bits;
	CHECK(st_rip3_features_set(&features, (enum st_rip3_feature)-1, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_features_set(&features, ST_RIP3_FEATURE_COUNT, true) == IO_ERROR_OUT_OF_RANGE);
	CHECK(st_rip3_features_set(&features, ST_RIP3_FEATURE_EXTAPPS, true) == IO_ERROR_NOT_SUPPORTED);
	CHECK(features.bits == old_bits);
	CHECK(st_rip3_features_set(NULL, ST_RIP3_FEATURE_JPEG, true) == IO_ERROR_INVALID_ARGUMENT);
	memset(result, 0xa5, sizeof(result)); memcpy(before, result, sizeof(result));
	CHECK(st_rip3_features_query(NULL, "JPEG", NULL, result, sizeof(result)) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_features_query(&features, NULL, NULL, result, sizeof(result)) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(st_rip3_features_query(&features, "", NULL, result, sizeof(result)) == IO_ERROR_INVALID_FORMAT);
	CHECK(st_rip3_features_query(&features, "JPEG", "ALL", result, sizeof(result)) == IO_ERROR_INVALID_FORMAT);
	CHECK(st_rip3_features_query(&features, "LIST", NULL, NULL, sizeof(result)) == IO_ERROR_INVALID_ARGUMENT);
	CHECK(memcmp(result, before, sizeof(result)) == 0);
	return 0;
}
