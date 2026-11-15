/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#ifndef ST_RIP3_FEATURES_H_
#define ST_RIP3_FEATURES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum st_rip3_feature {
	ST_RIP3_FEATURE_ANSI,
	ST_RIP3_FEATURE_BMP,
	ST_RIP3_FEATURE_CISQUICKB,
	ST_RIP3_FEATURE_DOORWAY,
	ST_RIP3_FEATURE_ENG,
	ST_RIP3_FEATURE_EXTAPPS,
	ST_RIP3_FEATURE_JPEG,
	ST_RIP3_FEATURE_KERMIT,
	ST_RIP3_FEATURE_RIPSCRIP,
	ST_RIP3_FEATURE_SUPERKERMIT,
	ST_RIP3_FEATURE_VT102,
	ST_RIP3_FEATURE_WAV,
	ST_RIP3_FEATURE_XMODEM,
	ST_RIP3_FEATURE_XMODEM1K,
	ST_RIP3_FEATURE_XMODEM1KG,
	ST_RIP3_FEATURE_XMODEMCRC,
	ST_RIP3_FEATURE_YMODEM,
	ST_RIP3_FEATURE_YMODEMG,
	ST_RIP3_FEATURE_ZMODEM,
	ST_RIP3_FEATURE_ZMODEMCR,
	ST_RIP3_FEATURE_COUNT,
};

struct st_rip3_features {
	uint32_t bits; /* Private representation; initialize the whole object to zero. */
};

/* No capability is assumed from a kernel existing or a build option. The
 * client registers only services available to this session, including runtime
 * JPEG/audio/transfer availability. EXTAPPS is always unsupported by policy;
 * enabling it returns NOT_SUPPORTED and changes nothing. One session owner. */
int st_rip3_features_set(struct st_rip3_features *features,
	enum st_rip3_feature feature, bool supported);
/* IFS value kernel, after argument parsing/expansion. Keyword is required;
 * category is optional and permitted only with LIST. Keywords are ASCII
 * case-insensitive. Unknown feature/category queries return 0; unknown LIST
 * categories return empty. Only supported features and nonempty categories
 * appear in sorted comma-separated lists. LIST/ALL emits features alone.
 * Output is NUL terminated without CR or trailing delimiter. Invalid arguments
 * and insufficient capacity leave the output buffer unchanged. No host I/O. */
int st_rip3_features_query(const struct st_rip3_features *features,
	const char *keyword, const char *category, char *result, size_t capacity);

#endif
