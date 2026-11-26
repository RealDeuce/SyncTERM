/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "io_error.h"
#include "st_rip3_host_internal.h"

enum field {
	ADOW,
	AMPM,
	DATE,
	DATETIME,
	DAY,
	DOW,
	DOY,
	FYEAR,
	HOUR,
	MHOUR,
	MINUTE,
	MONTH,
	MONTHNUM,
	SECOND,
	TIME,
	TIMEZONE,
	WDAY,
	WOY,
	WOYM,
	YEAR,
};

static const char *const names[] = {
    "ADOW",
    "AMPM",
    "DATE",
    "DATETIME",
    "DAY",
    "DOW",
    "DOY",
    "FYEAR",
    "HOUR",
    "MHOUR",
    "MIN",
    "MONTH",
    "MONTHNUM",
    "SEC",
    "TIME",
    "TIMEZONE",
    "WDAY",
    "WOY",
    "WOYM",
    "YEAR",
};
_Static_assert(sizeof(names) / sizeof(*names) == YEAR + 1, "Every date/time field needs its protocol name");

static const char *const days[] = {
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",
};
static const char *const short_days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const months[]     = {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December",
};

const char *
st_rip3_datetime_zone(const struct st_rip3_host_variables *host, char *zone, size_t capacity)
{
	if (host->time_zone != NULL)
		return host->time_zone[0] == '\0' ? "NONE" : host->time_zone;
	struct tm local;
	if (host->local_clock != NULL && host->local_clock(host->clock_context, &local)
	    && strftime(zone, capacity, "%Z", &local) != 0)
		return zone;
	return "NONE";
}

static bool
valid_snapshot(const struct tm *local)
{
	static const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int              year, maximum;
	bool             leap;

	/* Check before adding 1900 or using any field as an array index. The
	 * client supplies mutually consistent weekday/year-day fields from one
	 * clock conversion; formatting never consults another clock or locale. */
	if (local->tm_year < -1899 || local->tm_year > 8099 || local->tm_mon < 0 || local->tm_mon > 11
	    || local->tm_hour < 0 || local->tm_hour > 23 || local->tm_min < 0 || local->tm_min > 59
	    || local->tm_sec < 0 || local->tm_sec > 59 || local->tm_wday < 0 || local->tm_wday > 6)
		return false;
	year    = local->tm_year + 1900;
	leap    = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
	maximum = month_days[local->tm_mon] + (local->tm_mon == 1 && leap ? 1 : 0);
	return local->tm_mday >= 1 && local->tm_mday <= maximum && local->tm_yday >= 0
	       && local->tm_yday < (leap ? 366 : 365);
}

int
st_rip3_datetime_execute(const struct st_rip3_host_variables *host,
    const struct st_rip3_builtin_reference *reference, char *result, size_t capacity)
{
	const struct tm *local = host->local_time;
	struct tm        current;
	const char      *text = NULL;
	char             formatted[32];
	char             zone[ST_RIP3_TIME_ZONE_CAPACITY];
	enum field       field = ADOW;
	bool             found = false;
	int              added = 0;
	size_t           length;
	unsigned         year, month, day, hour, minute, second, weekday, yearday, value = 0;
	const char      *number_format = NULL;

	for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
		if (strcmp(reference->name, names[i]) == 0) {
			field = (enum field)i;
			found = true;
			break;
		}
	}
	if (!found)
		return IO_ERROR_NOT_SUPPORTED;
	if (reference->count != 0)
		return IO_ERROR_INVALID_FORMAT;
	if (field == TIMEZONE)
		text = st_rip3_datetime_zone(host, zone, sizeof(zone));
	else {
		if (host->local_clock != NULL) {
			if (!host->local_clock(host->clock_context, &current))
				return IO_ERROR_IO;
			local = &current;
		}
		if (local == NULL)
			return IO_ERROR_NOT_SUPPORTED;
		if (!valid_snapshot(local))
			return IO_ERROR_IO; /* Unusable client clock data, not RIP syntax. */
		year    = (unsigned)(local->tm_year + 1900);
		month   = (unsigned)local->tm_mon;
		day     = (unsigned)local->tm_mday;
		hour    = (unsigned)local->tm_hour;
		minute  = (unsigned)local->tm_min;
		second  = (unsigned)local->tm_sec;
		weekday = (unsigned)local->tm_wday;
		yearday = (unsigned)local->tm_yday;
		switch (field) {
			case ADOW:
				text = short_days[weekday];
				break;
			case AMPM:
				text = hour < 12 ? "AM" : "PM";
				break;
			case DOW:
				text = days[weekday];
				break;
			case MONTH:
				text = months[month];
				break;
			case DATE:
				added = snprintf(formatted, sizeof(formatted), "%02u/%02u/%02u", month + 1, day,
				    year % 100);
				break;
			case DATETIME:
				added = snprintf(formatted, sizeof(formatted), "%02u/%02u/%02u %02u:%02u:%02u",
				    month + 1, day, year % 100, hour, minute, second);
				break;
			case TIME:
				added =
				    snprintf(formatted, sizeof(formatted), "%02u:%02u:%02u", hour, minute, second);
				break;
			case DAY:
				value         = day;
				number_format = "%02u";
				break;
			case DOY:
				value         = yearday + 1;
				number_format = "%03u";
				break;
			case FYEAR:
				value         = year;
				number_format = "%04u";
				break;
			case HOUR:
				value         = hour % 12 == 0 ? 12 : hour % 12;
				number_format = "%02u";
				break;
			case MHOUR:
				value         = hour;
				number_format = "%02u";
				break;
			case MINUTE:
				value         = minute;
				number_format = "%02u";
				break;
			case MONTHNUM:
				value         = month + 1;
				number_format = "%02u";
				break;
			case SECOND:
				value         = second;
				number_format = "%02u";
				break;
			case WDAY:
				value         = weekday;
				number_format = "%u";
				break;
			/* Week zero precedes the first Sunday/Monday, not ISO week numbering. */
			case WOY:
				value         = (yearday + 7 - weekday) / 7;
				number_format = "%02u";
				break;
			case WOYM:
				value         = (yearday + 7 - (weekday + 6) % 7) / 7;
				number_format = "%02u";
				break;
			case YEAR:
				value         = year % 100;
				number_format = "%02u";
				break;
			default:
				return IO_ERROR_NOT_SUPPORTED;
		}
		if (number_format != NULL)
			added = snprintf(formatted, sizeof(formatted), number_format, value);
		if (text == NULL) {
			if (added < 0 || (size_t)added >= sizeof(formatted))
				return IO_ERROR_IO;
			text = formatted;
		}
	}
	length = strlen(text);
	if (length >= capacity)
		return IO_ERROR_OUT_OF_RANGE;
	memcpy(result, text, length + 1);
	return 0;
}
