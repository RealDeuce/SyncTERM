/* Private generated Unicode 18 comparison/property data. */

#ifndef ST_UNICODE_COMPARE_DATA_H_
#define ST_UNICODE_COMPARE_DATA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct st_unicode_collation_element {
	uint16_t primary;
	uint16_t secondary;
	uint16_t tertiary;
};

enum st_unicode_property_flag {
	ST_UNICODE_PROPERTY_WHITE_SPACE = UINT32_C(1) << 0,
	ST_UNICODE_PROPERTY_BIDI_CONTROL = UINT32_C(1) << 1,
	ST_UNICODE_PROPERTY_NONCHARACTER = UINT32_C(1) << 2,
	ST_UNICODE_PROPERTY_UNIFIED_IDEOGRAPH = UINT32_C(1) << 3,
	ST_UNICODE_PROPERTY_CONTROL = UINT32_C(1) << 4,
	ST_UNICODE_PROPERTY_UNASSIGNED = UINT32_C(1) << 5,
	ST_UNICODE_PROPERTY_PRIVATE_USE = UINT32_C(1) << 6,
	ST_UNICODE_PROPERTY_FORMAT = UINT32_C(1) << 7,
	ST_UNICODE_PROPERTY_LINE_SEPARATOR = UINT32_C(1) << 8,
	ST_UNICODE_PROPERTY_VARIATION_SELECTOR = UINT32_C(1) << 9,
};

size_t st_unicode_casefold_mapping(uint32_t codepoint, uint32_t output[3]);
size_t st_unicode_collation_mapping(const uint32_t *sequence, size_t count,
	const struct st_unicode_collation_element **elements);
bool st_unicode_collation_has_prefix(const uint32_t *sequence, size_t count);
bool st_unicode_implicit_range(uint32_t codepoint, uint16_t *lead,
	uint32_t *base);
uint32_t st_unicode_property_flags(uint32_t codepoint);

#endif
