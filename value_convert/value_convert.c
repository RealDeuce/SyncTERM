/* Strict, locale-independent textual value conversions. */

/*
 * Copyright (c) 2026 Stephen Hurd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "value_convert.h"

#include "decimal_double.h"
#include "string_util.h"
#include "value_convert_time.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#if defined(VALUE_CONVERT_TEST_NO_DBL_DECIMAL_DIG)
	#undef DBL_DECIMAL_DIG
#endif

#if defined(DBL_DECIMAL_DIG)
	#define VALUE_DOUBLE_DECIMAL_DIG DBL_DECIMAL_DIG
#elif FLT_RADIX == 2 && DBL_MANT_DIG == 53
	/* C17 requires DBL_DECIMAL_DIG, but some otherwise usable headers omit it. */
	#define VALUE_DOUBLE_DECIMAL_DIG 17
#else
	#error "DBL_DECIMAL_DIG is unavailable for an unsupported double format"
#endif

_Static_assert(VALUE_DOUBLE_DECIMAL_DIG <= 19,
    "the double formatter supports at most 19 significant digits");

static bool
trimmed_span(const char *text, const char **begin, const char **end)
{
	size_t first;
	size_t last;

	if (text == NULL)
		return false;
	string_ascii_trim_span(text, strlen(text), &first, &last);
	if (first == last)
		return false;
	*begin = text + first;
	*end = text + last;
	return true;
}

static int
digit_value(unsigned char character, unsigned base)
{
	if (character >= '0' && character <= '9') {
		int digit = character - '0';
		return (unsigned)digit < base ? digit : -1;
	}
	if (character >= 'a' && character <= 'f') {
		int digit = character - 'a' + 10;
		return (unsigned)digit < base ? digit : -1;
	}
	if (character >= 'A' && character <= 'F') {
		int digit = character - 'A' + 10;
		return (unsigned)digit < base ? digit : -1;
	}
	return -1;
}

static bool
parse_magnitude(const char *text, const char *end, uint64_t limit,
    uint64_t *result)
{
	unsigned base = 10;
	uint64_t value = 0;
	bool any = false;

	if (end - text >= 2 && text[0] == '0'
	    && (text[1] == 'x' || text[1] == 'X')) {
		base = 16;
		text += 2;
	}
	for (; text < end; text++) {
		int digit = digit_value((unsigned char)*text, base);

		if (digit < 0 || value > (limit - (unsigned)digit) / base)
			return false;
		value = value * base + (unsigned)digit;
		any = true;
	}
	if (!any)
		return false;
	*result = value;
	return true;
}

bool
value_parse_uint64(const char *text, uint64_t *result)
{
	const char *begin;
	const char *end;
	uint64_t value;

	if (result == NULL || !trimmed_span(text, &begin, &end))
		return false;
	if (*begin == '+')
		begin++;
	else if (*begin == '-')
		return false;
	if (!parse_magnitude(begin, end, UINT64_MAX, &value))
		return false;
	*result = value;
	return true;
}

bool
value_parse_int64(const char *text, int64_t *result)
{
	const char *begin;
	const char *end;
	uint64_t magnitude;
	uint64_t limit = INT64_MAX;
	bool negative = false;

	if (result == NULL || !trimmed_span(text, &begin, &end))
		return false;
	if (*begin == '+' || *begin == '-') {
		negative = *begin == '-';
		begin++;
	}
	if (negative)
		limit++;
	if (!parse_magnitude(begin, end, limit, &magnitude))
		return false;
	if (negative && magnitude == (uint64_t)INT64_MAX + 1)
		*result = INT64_MIN;
	else if (negative)
		*result = -(int64_t)magnitude;
	else
		*result = (int64_t)magnitude;
	return true;
}

bool
value_parse_bool(const char *text, bool *result)
{
	const char *begin;
	const char *end;

	if (result == NULL || !trimmed_span(text, &begin, &end))
		return false;
	if (ascii_case_span_equal(begin, (size_t)(end - begin), "true")
	    || ascii_case_span_equal(begin, (size_t)(end - begin), "yes")
	    || ascii_case_span_equal(begin, (size_t)(end - begin), "on")
	    || (end - begin == 1 && *begin == '1')) {
		*result = true;
		return true;
	}
	if (ascii_case_span_equal(begin, (size_t)(end - begin), "false")
	    || ascii_case_span_equal(begin, (size_t)(end - begin), "no")
	    || ascii_case_span_equal(begin, (size_t)(end - begin), "off")
	    || (end - begin == 1 && *begin == '0')) {
		*result = false;
		return true;
	}
	return false;
}

bool
value_parse_double(const char *text, double *result)
{
	const char *begin;
	const char *end;

	return result != NULL && trimmed_span(text, &begin, &end)
	    && decimal_parse_double(begin, end, result);
}

static bool
copy_result(char *buffer, size_t size, const char *source, size_t length)
{
	if (buffer == NULL || size <= length)
		return false;
	memcpy(buffer, source, length);
	buffer[length] = '\0';
	return true;
}

bool
value_format_uint64(char *buffer, size_t size, uint64_t value)
{
	char reverse[32];
	char output[32];
	size_t length = 0;

	do {
		reverse[length++] = (char)('0' + value % 10);
		value /= 10;
	} while (value != 0);
	for (size_t i = 0; i < length; i++)
		output[i] = reverse[length - i - 1];
	return copy_result(buffer, size, output, length);
}

bool
value_format_int64(char *buffer, size_t size, int64_t value)
{
	uint64_t magnitude;

	if (value >= 0)
		return value_format_uint64(buffer, size, (uint64_t)value);
	if (buffer == NULL || size < 2)
		return false;
	magnitude = (uint64_t)(-(value + 1)) + 1;
	buffer[0] = '-';
	return value_format_uint64(buffer + 1, size - 1, magnitude);
}

bool
value_format_double(char *buffer, size_t size, double value)
{
	return decimal_format_double(buffer, size, value,
	    VALUE_DOUBLE_DECIMAL_DIG);
}

static bool
append_suffix(char *buffer, size_t size, const char *number, char suffix)
{
	size_t length = strlen(number);

	if (buffer == NULL || length + 2 > size)
		return false;
	memcpy(buffer, number, length);
	buffer[length] = suffix;
	buffer[length + 1] = '\0';
	return true;
}

bool
value_format_byte_estimate(char *buffer, size_t size, uint64_t bytes)
{
	static const struct {
		uint64_t divisor;
		char suffix;
	} units[] = {
		{UINT64_C(1) << 50, 'P'},
		{UINT64_C(1) << 40, 'T'},
		{UINT64_C(1) << 30, 'G'},
		{UINT64_C(1) << 20, 'M'},
		{UINT64_C(1) << 10, 'K'},
	};
	char number[32];

	for (size_t index = 0; index < sizeof(units) / sizeof(units[0]); index++) {
		uint64_t divisor = units[index].divisor;
		uint64_t remainder;
		uint64_t scaled_remainder;
		uint64_t whole;
		unsigned tenth;
		size_t length;

		if (bytes < divisor)
			continue;
		whole = bytes / divisor;
		remainder = bytes % divisor;
		scaled_remainder = remainder * 10;
		tenth = (unsigned)(scaled_remainder / divisor);
		remainder = scaled_remainder % divisor;
		if (remainder > divisor / 2
		    || (remainder == divisor / 2 && (tenth & 1) != 0)) {
			if (++tenth == 10) {
				tenth = 0;
				whole++;
			}
		}
		if (!value_format_uint64(number, sizeof(number), whole))
			return false;
		length = strlen(number);
		if (length + 4 > sizeof(number))
			return false;
		number[length++] = '.';
		number[length++] = (char)('0' + tenth);
		number[length] = '\0';
		return append_suffix(buffer, size, number, units[index].suffix);
	}
	return value_format_uint64(buffer, size, bytes);
}

bool
value_format_duration_estimate(char *buffer, size_t size, double seconds)
{
	static const struct {
		double divisor;
		char suffix;
	} units[] = {
		{365.0 * 24.0 * 60.0 * 60.0, 'y'},
		{24.0 * 60.0 * 60.0, 'd'},
		{60.0 * 60.0, 'h'},
		{60.0, 'm'},
	};
	char number[64];
	char suffix = 's';
	bool compact = true;

	if (!(seconds >= 0.0))
		seconds = 0.0;
	if (isinf(seconds))
		return copy_result(buffer, size, "infy", 4);
	for (size_t index = 0; index < sizeof(units) / sizeof(units[0]); index++) {
		if (seconds >= units[index].divisor) {
			seconds /= units[index].divisor;
			suffix = units[index].suffix;
			compact = false;
			break;
		}
	}
	if (!decimal_format_fixed(number, sizeof(number), seconds, 1))
		return false;
	if (compact) {
		size_t length = strlen(number);

		if (length >= 2 && number[length - 2] == '.'
		    && number[length - 1] == '0')
			number[length - 2] = '\0';
	}
	return append_suffix(buffer, size, number, suffix);
}

bool
value_parse_duration(const char *text, double *seconds)
{
	const char *begin;
	const char *end;
	const char *number_end;
	double value;
	double multiplier = 1.0;

	if (seconds == NULL || !trimmed_span(text, &begin, &end))
		return false;
	number_end = end;
	if (end > begin && ((* (end - 1) >= 'A' && *(end - 1) <= 'Z')
	    || (*(end - 1) >= 'a' && *(end - 1) <= 'z'))) {
		unsigned char suffix = ascii_to_lower((unsigned char)*(end - 1));

		number_end--;
		switch (suffix) {
		case 's': multiplier = 1.0; break;
		case 'm': multiplier = 60.0; break;
		case 'h': multiplier = 60.0 * 60.0; break;
		case 'd': multiplier = 24.0 * 60.0 * 60.0; break;
		case 'w': multiplier = 7.0 * 24.0 * 60.0 * 60.0; break;
		case 'y': multiplier = 365.0 * 24.0 * 60.0 * 60.0; break;
		default: return false;
		}
	}
	while (number_end > begin
	    && ascii_is_space((unsigned char)number_end[-1]))
		number_end--;
	if (!decimal_parse_double(begin, number_end, &value)
	    || !isfinite(value * multiplier))
		return false;
	*seconds = value * multiplier;
	return true;
}

bool
value_format_duration(char *buffer, size_t size, double seconds)
{
	static const struct {
		double multiplier;
		char suffix;
	} units[] = {
		{365.0 * 24.0 * 60.0 * 60.0, 'y'},
		{7.0 * 24.0 * 60.0 * 60.0, 'w'},
		{24.0 * 60.0 * 60.0, 'd'},
		{60.0 * 60.0, 'h'},
		{60.0, 'm'},
	};
	char number[VALUE_DOUBLE_DECIMAL_DIG + 20];
	double value = seconds;
	char suffix = 's';

	if (!isfinite(seconds))
		return false;
	for (size_t i = 0; seconds != 0.0 && i < sizeof(units) / sizeof(units[0]); i++) {
		if (fmod(seconds, units[i].multiplier) == 0.0) {
			value = seconds / units[i].multiplier;
			suffix = units[i].suffix;
			break;
		}
	}
	if (!value_format_double(number, sizeof(number), value))
		return false;
	if (strlen(number) + 2 > size || buffer == NULL)
		return false;
	memcpy(buffer, number, strlen(number));
	buffer[strlen(number)] = suffix;
	buffer[strlen(number) + 1] = '\0';
	return true;
}

static bool
parse_fixed_uint(const char *text, size_t length, int *result)
{
	int value = 0;

	for (size_t i = 0; i < length; i++) {
		if (text[i] < '0' || text[i] > '9')
			return false;
		value = value * 10 + (text[i] - '0');
	}
	*result = value;
	return true;
}

static int
month_number(const char *text)
{
	static const char *const months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	for (int i = 0; i < 12; i++) {
		if (ascii_case_ncompare(text, months[i], 3) == 0)
			return i;
	}
	return -1;
}

static bool
parse_day_field(const char *text, int *result)
{
	if (text[0] == ' ' && text[1] >= '0' && text[1] <= '9') {
		*result = text[1] - '0';
		return true;
	}
	return parse_fixed_uint(text, 2, result);
}

static bool
finish_datetime(struct tm *parts, time_t *result)
{
	struct tm checked;
	int year = parts->tm_year;
	int month = parts->tm_mon;
	int day = parts->tm_mday;
	int hour = parts->tm_hour;
	int minute = parts->tm_min;
	int second = parts->tm_sec;
	time_t value;

	if (parts->tm_mon < 0 || parts->tm_mon > 11 || parts->tm_mday < 1
	    || parts->tm_mday > 31 || parts->tm_hour < 0 || parts->tm_hour > 23
	    || parts->tm_min < 0 || parts->tm_min > 59 || parts->tm_sec < 0
	    || parts->tm_sec > 60)
		return false;
	parts->tm_isdst = -1;
	value = mktime(parts);
	if (value == (time_t)-1 || !value_convert_localtime(value, &checked)
	    || checked.tm_year != year || checked.tm_mon != month
	    || checked.tm_mday != day || checked.tm_hour != hour
	    || checked.tm_min != minute || checked.tm_sec != second)
		return false;
	*result = value;
	return true;
}

bool
value_parse_datetime(const char *text, time_t *result)
{
	const char *begin;
	const char *end;
	size_t length;
	struct tm parts = {0};
	int year;
	int month;
	int day;
	int hour = 0;
	int minute = 0;
	int second = 0;
	int64_t epoch;

	if (result == NULL || !trimmed_span(text, &begin, &end))
		return false;
	length = (size_t)(end - begin);
	if (ascii_case_span_equal(begin, (size_t)(end - begin), "never")) {
		*result = (time_t)0;
		return true;
	}
	if (value_parse_int64(text, &epoch) && (int64_t)(time_t)epoch == epoch) {
		*result = (time_t)epoch;
		return true;
	}
	if ((length == 8 || length == 15)
	    && parse_fixed_uint(begin, 4, &year)
	    && parse_fixed_uint(begin + 4, 2, &month)
	    && parse_fixed_uint(begin + 6, 2, &day)) {
		if (length == 15) {
			if (begin[8] != 'T'
			    || !parse_fixed_uint(begin + 9, 2, &hour)
			    || !parse_fixed_uint(begin + 11, 2, &minute)
			    || !parse_fixed_uint(begin + 13, 2, &second))
				return false;
		}
		parts.tm_year = year - 1900;
		parts.tm_mon = month - 1;
		parts.tm_mday = day;
		parts.tm_hour = hour;
		parts.tm_min = minute;
		parts.tm_sec = second;
		return finish_datetime(&parts, result);
	}
	if ((length == 11 || length == 20) && month_number(begin) >= 0
	    && begin[3] == ' ' && begin[6] == ' '
	    && parse_day_field(begin + 4, &day)
	    && parse_fixed_uint(begin + 7, 4, &year)) {
		if (length == 20) {
			if (begin[11] != ' ' || begin[14] != ':' || begin[17] != ':'
			    || !parse_fixed_uint(begin + 12, 2, &hour)
			    || !parse_fixed_uint(begin + 15, 2, &minute)
			    || !parse_fixed_uint(begin + 18, 2, &second))
				return false;
		}
		parts.tm_year = year - 1900;
		parts.tm_mon = month_number(begin);
		parts.tm_mday = day;
		parts.tm_hour = hour;
		parts.tm_min = minute;
		parts.tm_sec = second;
		return finish_datetime(&parts, result);
	}
	return false;
}

bool
value_format_datetime(char *buffer, size_t size, time_t value,
    bool include_time)
{
	static const char *const months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	struct tm parts;
	char year[16];
	char fields[6][3];
	size_t needed = include_time ? 20 : 11;

	if (value == (time_t)0)
		return copy_result(buffer, size, "Never", 5);
	if (!value_convert_localtime(value, &parts) || parts.tm_mon < 0
	    || parts.tm_mon > 11 || !value_format_int64(year, sizeof(year),
	    (int64_t)parts.tm_year + 1900) || strlen(year) != 4
	    || buffer == NULL || size <= needed)
		return false;
	fields[0][0] = parts.tm_mday < 10 ? ' '
	    : (char)('0' + parts.tm_mday / 10);
	fields[0][1] = (char)('0' + parts.tm_mday % 10);
	fields[1][0] = (char)('0' + parts.tm_hour / 10);
	fields[1][1] = (char)('0' + parts.tm_hour % 10);
	fields[2][0] = (char)('0' + parts.tm_min / 10);
	fields[2][1] = (char)('0' + parts.tm_min % 10);
	fields[3][0] = (char)('0' + parts.tm_sec / 10);
	fields[3][1] = (char)('0' + parts.tm_sec % 10);
	memcpy(buffer, months[parts.tm_mon], 3);
	buffer[3] = ' ';
	memcpy(buffer + 4, fields[0], 2);
	buffer[6] = ' ';
	memcpy(buffer + 7, year, 4);
	if (include_time) {
		buffer[11] = ' ';
		memcpy(buffer + 12, fields[1], 2);
		buffer[14] = ':';
		memcpy(buffer + 15, fields[2], 2);
		buffer[17] = ':';
		memcpy(buffer + 18, fields[3], 2);
	}
	buffer[needed] = '\0';
	return true;
}
