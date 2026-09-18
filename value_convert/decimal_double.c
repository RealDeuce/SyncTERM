/* Exact, locale-independent decimal-to-double conversion. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "decimal_double.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if FLT_RADIX != 2
	#error "decimal conversion currently requires radix-two double"
#endif

#if DBL_MANT_DIG > 63
	#error "decimal conversion currently supports at most 63 double significand bits"
#endif

#define DECIMAL_EXPONENT_LIMIT 5000
#define DECIMAL_DIGIT_CAPACITY (DECIMAL_EXPONENT_LIMIT * 5 / 2 + 64)

/* Digits are stored least-significant first. */
struct decimal_integer {
	uint8_t digit[DECIMAL_DIGIT_CAPACITY];
	size_t length;
	int exponent;
};

static void
decimal_normalize(struct decimal_integer *value)
{
	while (value->length > 0 && value->digit[value->length - 1] == 0)
		value->length--;
	while (value->length > 0 && value->digit[0] == 0) {
		memmove(value->digit, value->digit + 1, --value->length);
		value->exponent++;
	}
}

static bool
decimal_multiply(struct decimal_integer *value, unsigned factor)
{
	unsigned carry = 0;

	for (size_t index = 0; index < value->length; index++) {
		unsigned product = value->digit[index] * factor + carry;

		value->digit[index] = (uint8_t)(product % 10);
		carry = product / 10;
	}
	while (carry != 0) {
		if (value->length == DECIMAL_DIGIT_CAPACITY)
			return false;
		value->digit[value->length++] = (uint8_t)(carry % 10);
		carry /= 10;
	}
	return true;
}

static void
decimal_set_uint64(struct decimal_integer *value, uint64_t initial)
{
	value->length = 0;
	value->exponent = 0;
	do {
		value->digit[value->length++] = (uint8_t)(initial % 10);
		initial /= 10;
	} while (initial != 0);
}

static int
decimal_digit_at(const struct decimal_integer *value, int position)
{
	ptrdiff_t index = (ptrdiff_t)position - value->exponent;

	if (index < 0 || (size_t)index >= value->length)
		return 0;
	return value->digit[index];
}

static int
decimal_compare(const struct decimal_integer *left,
    const struct decimal_integer *right)
{
	int left_top;
	int right_top;
	int bottom;

	if (left->length == 0 || right->length == 0) {
		if (left->length == right->length)
			return 0;
		return left->length == 0 ? -1 : 1;
	}
	left_top = (int)left->length + left->exponent;
	right_top = (int)right->length + right->exponent;
	if (left_top != right_top)
		return left_top < right_top ? -1 : 1;
	bottom = left->exponent < right->exponent ?
	    left->exponent : right->exponent;
	for (int position = left_top - 1; position >= bottom; position--) {
		int left_digit = decimal_digit_at(left, position);
		int right_digit = decimal_digit_at(right, position);

		if (left_digit != right_digit)
			return left_digit < right_digit ? -1 : 1;
	}
	return 0;
}

static bool
decimal_power_of_two(int exponent, struct decimal_integer *result)
{
	result->length = 1;
	result->digit[0] = 1;
	result->exponent = 0;
	if (exponent >= 0) {
		for (int index = 0; index < exponent; index++) {
			if (!decimal_multiply(result, 2))
				return false;
		}
	}
	else {
		for (int index = 0; index > exponent; index--) {
			if (!decimal_multiply(result, 5))
				return false;
			result->exponent--;
		}
	}
	decimal_normalize(result);
	return true;
}

static bool
decimal_find_binary_exponent(const struct decimal_integer *value,
    int *result)
{
	struct decimal_integer power;
	struct decimal_integer next;
	int decimal_exponent = (int)value->length + value->exponent - 1;
	int exponent = decimal_exponent * 3321 / 1000;

	if (!decimal_power_of_two(exponent, &power))
		return false;
	while (decimal_compare(value, &power) < 0) {
		if (!decimal_multiply(&power, 5))
			return false;
		power.exponent--;
		exponent--;
		decimal_normalize(&power);
	}
	for (;;) {
		next = power;
		if (!decimal_multiply(&next, 2))
			return false;
		if (decimal_compare(value, &next) < 0)
			break;
		power = next;
		exponent++;
	}
	*result = exponent;
	return true;
}

static bool
decimal_round_to_uint64(const struct decimal_integer *value,
    uint64_t *result)
{
	size_t fractional = value->exponent < 0 ?
	    (size_t)-value->exponent : 0;
	uint64_t integer = 0;
	int comparison = -1;

	if (fractional < value->length) {
		for (size_t index = value->length; index > fractional; index--) {
			unsigned digit = value->digit[index - 1];

			if (integer > (UINT64_MAX - digit) / 10)
				return false;
			integer = integer * 10 + digit;
		}
	}
	if (value->exponent > 0) {
		for (int index = 0; index < value->exponent; index++) {
			if (integer > UINT64_MAX / 10)
				return false;
			integer *= 10;
		}
	}
	if (fractional > 0) {
		size_t tenths = fractional - 1;
		int first = tenths < value->length ? value->digit[tenths] : 0;

		if (first > 5)
			comparison = 1;
		else if (first == 5) {
			comparison = 0;
			for (size_t index = 0; index < tenths && index < value->length;
			    index++) {
				if (value->digit[index] != 0) {
					comparison = 1;
					break;
				}
			}
		}
	}
	if (comparison > 0 || (comparison == 0 && (integer & 1) != 0)) {
		if (integer == UINT64_MAX)
			return false;
		integer++;
	}
	*result = integer;
	return true;
}

static bool
parse_decimal(const char *begin, const char *end, bool *negative,
    struct decimal_integer *value)
{
	const char *mantissa_begin;
	const char *mantissa_end;
	const char *cursor;
	int fractional_digits = 0;
	int explicit_exponent = 0;
	bool exponent_negative = false;
	bool exponent_too_large = false;
	bool fraction = false;
	bool any = false;

	*negative = false;
	if (begin < end && (*begin == '+' || *begin == '-')) {
		*negative = *begin == '-';
		begin++;
	}
	mantissa_begin = begin;
	cursor = begin;
	while (cursor < end) {
		unsigned char character = (unsigned char)*cursor;

		if (character >= '0' && character <= '9') {
			if (fraction) {
				if (fractional_digits == DECIMAL_EXPONENT_LIMIT)
					exponent_too_large = true;
				else
					fractional_digits++;
			}
			any = true;
			cursor++;
			continue;
		}
		if (character == '.' && !fraction) {
			fraction = true;
			cursor++;
			continue;
		}
		break;
	}
	mantissa_end = cursor;
	if (!any)
		return false;
	if (cursor < end && (*cursor == 'e' || *cursor == 'E')) {
		bool exponent_digit = false;

		cursor++;
		if (cursor < end && (*cursor == '+' || *cursor == '-')) {
			exponent_negative = *cursor == '-';
			cursor++;
		}
		while (cursor < end && *cursor >= '0' && *cursor <= '9') {
			if (explicit_exponent > DECIMAL_EXPONENT_LIMIT / 10
			    || (explicit_exponent == DECIMAL_EXPONENT_LIMIT / 10
			    && *cursor - '0' > DECIMAL_EXPONENT_LIMIT % 10))
				exponent_too_large = true;
			else if (!exponent_too_large)
				explicit_exponent = explicit_exponent * 10
				    + (*cursor - '0');
			exponent_digit = true;
			cursor++;
		}
		if (!exponent_digit)
			return false;
	}
	if (cursor != end)
		return false;
	value->length = 0;
	for (cursor = mantissa_end; cursor > mantissa_begin; ) {
		unsigned char character = (unsigned char)*--cursor;

		if (character == '.')
			continue;
		if (value->length == DECIMAL_DIGIT_CAPACITY)
			return false;
		value->digit[value->length++] = (uint8_t)(character - '0');
	}
	value->exponent = 0;
	decimal_normalize(value);
	if (value->length == 0)
		return true;
	if (exponent_too_large)
		return false;
	if (exponent_negative)
		explicit_exponent = -explicit_exponent;
	if (explicit_exponent < -DECIMAL_EXPONENT_LIMIT + fractional_digits
	    || explicit_exponent > DECIMAL_EXPONENT_LIMIT + fractional_digits)
		return false;
	value->exponent += explicit_exponent - fractional_digits;
	return true;
}

bool
decimal_parse_double(const char *begin, const char *end, double *result)
{
	struct decimal_integer value;
	uint64_t significand;
	uint64_t significand_limit = UINT64_C(1) << DBL_MANT_DIG;
	int binary_exponent;
	int result_exponent;
	int shift;
	bool negative;
	double converted;

	if (begin == NULL || end == NULL || result == NULL || begin > end
	    || !parse_decimal(begin, end, &negative, &value))
		return false;
	if (value.length == 0) {
		*result = negative ? -0.0 : 0.0;
		return true;
	}
	if ((int)value.length + value.exponent - 1 > DBL_MAX_10_EXP)
		return false;
	if (!decimal_find_binary_exponent(&value, &binary_exponent)
	    || binary_exponent > DBL_MAX_EXP - 1)
		return false;
	if (binary_exponent >= DBL_MIN_EXP - 1) {
		result_exponent = binary_exponent - (DBL_MANT_DIG - 1);
		shift = DBL_MANT_DIG - 1 - binary_exponent;
	}
	else {
		result_exponent = DBL_MIN_EXP - DBL_MANT_DIG;
		shift = -result_exponent;
	}
	if (shift >= 0) {
		for (int index = 0; index < shift; index++) {
			if (!decimal_multiply(&value, 2))
				return false;
		}
	}
	else {
		for (int index = 0; index > shift; index--) {
			if (!decimal_multiply(&value, 5))
				return false;
			value.exponent--;
		}
	}
	decimal_normalize(&value);
	if (!decimal_round_to_uint64(&value, &significand)
	    || significand == 0)
		return false;
	if (significand == significand_limit) {
		significand >>= 1;
		result_exponent++;
	}
	if (result_exponent > DBL_MAX_EXP - DBL_MANT_DIG)
		return false;
	converted = ldexp((double)significand, result_exponent);
	if (!isfinite(converted) || converted == 0.0)
		return false;
	*result = negative ? -converted : converted;
	return true;
}

static bool
append_character(char *output, size_t capacity, size_t *length,
    char character)
{
	if (*length == capacity)
		return false;
	output[(*length)++] = character;
	return true;
}

static bool
append_exponent(char *output, size_t capacity, size_t *length, int exponent)
{
	char reverse[16];
	size_t digits = 0;
	unsigned magnitude;

	if (exponent < 0) {
		if (!append_character(output, capacity, length, '-'))
			return false;
		magnitude = (unsigned)-exponent;
	}
	else
		magnitude = (unsigned)exponent;
	do {
		reverse[digits++] = (char)('0' + magnitude % 10);
		magnitude /= 10;
	} while (magnitude != 0);
	while (digits > 0) {
		if (!append_character(output, capacity, length, reverse[--digits]))
			return false;
	}
	return true;
}

bool
decimal_format_double(char *buffer, size_t size, double value, int precision)
{
	struct decimal_integer exact;
	char digits[DBL_MANT_DIG + 1];
	char output[DBL_MANT_DIG + 32];
	double fraction;
	uint64_t significand;
	int binary_exponent;
	int scientific_exponent;
	int binary_shift;
	size_t digit_count;
	size_t length = 0;
	bool negative;

	if (buffer == NULL || precision < 1 || precision > DBL_MANT_DIG
	    || !isfinite(value))
		return false;
	negative = signbit(value);
	if (negative)
		value = fabs(value);
	if (value == 0.0) {
		if (size < (negative ? 3U : 2U))
			return false;
		if (negative)
			buffer[length++] = '-';
		buffer[length++] = '0';
		buffer[length] = '\0';
		return true;
	}
	fraction = frexp(value, &binary_exponent);
	significand = (uint64_t)ldexp(fraction, DBL_MANT_DIG);
	decimal_set_uint64(&exact, significand);
	binary_shift = binary_exponent - DBL_MANT_DIG;
	if (binary_shift >= 0) {
		for (int index = 0; index < binary_shift; index++) {
			if (!decimal_multiply(&exact, 2))
				return false;
		}
	}
	else {
		for (int index = 0; index > binary_shift; index--) {
			if (!decimal_multiply(&exact, 5))
				return false;
			exact.exponent--;
		}
	}
	decimal_normalize(&exact);
	scientific_exponent = (int)exact.length + exact.exponent - 1;
	digit_count = exact.length < (size_t)precision ?
	    exact.length : (size_t)precision;
	for (size_t index = 0; index < digit_count; index++)
		digits[index] = (char)('0' + exact.digit[exact.length - index - 1]);
	if (exact.length > digit_count) {
		size_t discarded = exact.length - digit_count;
		int first = exact.digit[discarded - 1];
		bool round_up = first > 5;

		if (first == 5) {
			round_up = (digits[digit_count - 1] - '0') % 2 != 0;
			for (size_t index = 0; index + 1 < discarded; index++) {
				if (exact.digit[index] != 0) {
					round_up = true;
					break;
				}
			}
		}
		if (round_up) {
			size_t index = digit_count;

			while (index > 0 && digits[index - 1] == '9')
				digits[--index] = '0';
			if (index == 0) {
				digits[0] = '1';
				digit_count = 1;
				scientific_exponent++;
			}
			else
				digits[index - 1]++;
		}
	}
	while (digit_count > 1 && digits[digit_count - 1] == '0')
		digit_count--;
	if (negative && !append_character(output, sizeof(output), &length, '-'))
		return false;
	if (scientific_exponent >= -4 && scientific_exponent < precision) {
		if (scientific_exponent < 0) {
			if (!append_character(output, sizeof(output), &length, '0')
			    || !append_character(output, sizeof(output), &length, '.'))
				return false;
			for (int index = -1; index > scientific_exponent; index--) {
				if (!append_character(output, sizeof(output), &length, '0'))
					return false;
			}
			for (size_t index = 0; index < digit_count; index++) {
				if (!append_character(output, sizeof(output), &length,
				    digits[index]))
					return false;
			}
		}
		else {
			size_t whole = (size_t)scientific_exponent + 1;
			size_t copied = whole < digit_count ? whole : digit_count;

			for (size_t index = 0; index < copied; index++) {
				if (!append_character(output, sizeof(output), &length,
				    digits[index]))
					return false;
			}
			for (size_t index = copied; index < whole; index++) {
				if (!append_character(output, sizeof(output), &length, '0'))
					return false;
			}
			if (digit_count > whole) {
				if (!append_character(output, sizeof(output), &length, '.'))
					return false;
				for (size_t index = whole; index < digit_count; index++) {
					if (!append_character(output, sizeof(output), &length,
					    digits[index]))
						return false;
				}
			}
		}
	}
	else {
		if (!append_character(output, sizeof(output), &length, digits[0]))
			return false;
		if (digit_count > 1) {
			if (!append_character(output, sizeof(output), &length, '.'))
				return false;
			for (size_t index = 1; index < digit_count; index++) {
				if (!append_character(output, sizeof(output), &length,
				    digits[index]))
					return false;
			}
		}
		if (!append_character(output, sizeof(output), &length, 'e')
		    || !append_exponent(output, sizeof(output), &length,
		    scientific_exponent))
			return false;
	}
	if (size <= length)
		return false;
	memcpy(buffer, output, length);
	buffer[length] = '\0';
	return true;
}
