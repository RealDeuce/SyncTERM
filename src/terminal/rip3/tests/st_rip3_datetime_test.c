/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */
#include "st_rip3_host_variables.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #expression); \
		return 1; \
	} \
} while (0)

struct observations {
	char bytes[5000];
	size_t replies, diagnostics;
	enum st_rip3_host_diagnostic last;
	bool overflow;
};

static void
reply(void *context, const uint8_t *bytes, size_t length)
{
	struct observations *observed = context;
	observed->replies++;
	if (length >= sizeof(observed->bytes)) {
		observed->overflow = true;
		return;
	}
	memcpy(observed->bytes, bytes, length);
	observed->bytes[length] = 0;
}

static void
diagnostic(void *context, enum st_rip3_host_diagnostic kind)
{
	struct observations *observed = context;
	observed->diagnostics++;
	observed->last = kind;
}

static int
expect(const struct st_rip3_host_variables *host, const char *expression,
    int error, const char *expected)
{
	struct observations observed = {0};
	const struct st_rip3_host_sink sink = {&observed,reply,diagnostic};

	st_rip3_host_variables_process(host, &sink, (const uint8_t *)expression, strlen(expression));
	CHECK(!observed.overflow);
	CHECK(observed.diagnostics == (error >= 0 ? 1u : 0u));
	if (error >= 0) {
		CHECK((int)observed.last == error && observed.replies == 0);
	}
	else {
		CHECK(observed.replies == 1 && strcmp(observed.bytes, expected) == 0);
	}
	return 0;
}

struct test_clock {
	unsigned calls;
	bool fail;
};

static bool
read_clock(void *context, struct tm *local)
{
	struct test_clock *clock = context;
	clock->calls++;
	if (clock->fail)
		return false;
	*local = (struct tm){.tm_year=124,.tm_mon=1,.tm_mday=29,.tm_hour=23,
	    .tm_min=4,.tm_sec=(int)clock->calls,.tm_wday=4,.tm_yday=59};
	return true;
}

int
main(void)
{
	struct tm local = {.tm_year=124,.tm_mon=1,.tm_mday=29,.tm_hour=23,
	    .tm_min=4,.tm_sec=5,.tm_wday=4,.tm_yday=59,.tm_isdst=0};
	struct st_rip3_host_variables host = {.local_time=&local,.time_zone="EST"};
	const struct {const char *expression, *reply;} fields[] = {
		{"$ADOW$","Thu"}, {"$AMPM$","PM"}, {"$DATE$","02/29/24"},
		{"$DATETIME$","02/29/24 23:04:05"},
		{"$DAY$","29"}, {"$DOW$","Thursday"}, {"$DOY$","060"},
		{"$FYEAR$","2024"}, {"$HOUR$","11"}, {"$MHOUR$","23"},
		{"$MIN$","04"}, {"$MONTH$","February"}, {"$MONTHNUM$","02"},
		{"$SEC$","05"}, {"$TIME$","23:04:05"}, {"$TIMEZONE$","EST"},
		{"$WDAY$","4"}, {"$WOY$","08"}, {"$WOYM$","09"}, {"$YEAR$","24"},
	};
	const char *const months[] = {"January","February","March","April","May","June",
	    "July","August","September","October","November","December"};
	const int weekdays[] = {1,4,5,1,3,6,1,4,0,2,5,0};
	const int yeardays[] = {0,31,60,91,121,152,182,213,244,274,305,335};
	const char *const days[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
	const char *const abbreviated[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
	const struct tm leap_day = local;
	char huge_zone[4097];

	for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
		CHECK(expect(&host, fields[i].expression, -1, fields[i].reply) == 0);
	CHECK(expect(&host, "$DATETIME(1)$", ST_RIP3_HOST_INVALID_REFERENCE, NULL) == 0);
	CHECK(expect(&host, "$DAY(1)$", ST_RIP3_HOST_INVALID_REFERENCE, NULL) == 0);
	CHECK(expect(&host, "$DAY$", -1, "29") == 0);
	/* All English month/day names are independent of the client's locale. */
	for (int i = 0; i < 12; i++) {
		local = leap_day; local.tm_mon = i; local.tm_mday = 1;
		local.tm_wday = weekdays[i]; local.tm_yday = yeardays[i];
		CHECK(expect(&host, "$MONTH$", -1, months[i]) == 0);
	}
	for (int i = 0; i < 7; i++) {
		local = leap_day; local.tm_mon = 0; local.tm_mday = i + 1;
		local.tm_yday = i; local.tm_wday = (i + 1) % 7;
		CHECK(expect(&host, "$DOW$", -1, days[(i + 1) % 7]) == 0);
		CHECK(expect(&host, "$ADOW$", -1, abbreviated[(i + 1) % 7]) == 0);
		if (i == 4) CHECK(expect(&host, "$DAY$", -1, "05") == 0);
	}
	/* Literal examples from the pinned reference. */
	local = (struct tm){.tm_year=93,.tm_mon=11,.tm_mday=19,.tm_wday=0,.tm_yday=352,
	    .tm_hour=14,.tm_min=38,.tm_sec=50};
	CHECK(expect(&host, "$DATE$", -1, "12/19/93") == 0);
	CHECK(expect(&host, "$DATETIME$", -1, "12/19/93 14:38:50") == 0);
	local.tm_mday=5; local.tm_yday=338;
	CHECK(expect(&host, "$DATETIME()$", -1, "12/05/93 14:38:50") == 0);
	CHECK(expect(&host, "$FYEAR$", -1, "1993") == 0);
	CHECK(expect(&host, "$YEAR$", -1, "93") == 0);
	CHECK(expect(&host, "$MONTHNUM$", -1, "12") == 0);
	local.tm_hour = 11;
	CHECK(expect(&host, "$HOUR$", -1, "11") == 0);
	local.tm_hour = 17; local.tm_min = 45; local.tm_sec = 59;
	CHECK(expect(&host, "$MHOUR$", -1, "17") == 0);
	CHECK(expect(&host, "$MIN$", -1, "45") == 0);
	CHECK(expect(&host, "$SEC$", -1, "59") == 0);
	local.tm_hour = 18; local.tm_min = 9; local.tm_sec = 33;
	CHECK(expect(&host, "$TIME$", -1, "18:09:33") == 0);
	local = (struct tm){.tm_year=123,.tm_mon=7,.tm_mday=2,.tm_wday=3,.tm_yday=213};
	CHECK(expect(&host, "$DOY$", -1, "214") == 0);
	local.tm_mday = 9; local.tm_yday = 220;
	CHECK(expect(&host, "$WOY$", -1, "32") == 0);
	CHECK(expect(&host, "$WOYM$", -1, "32") == 0);
	local = (struct tm){.tm_year=123,.tm_mon=0,.tm_mday=1,.tm_wday=0,.tm_yday=0};
	CHECK(expect(&host, "$HOUR$", -1, "12") == 0);
	CHECK(expect(&host, "$AMPM$", -1, "AM") == 0);
	CHECK(expect(&host, "$TIME$", -1, "00:00:00") == 0);
	CHECK(expect(&host, "$DAY$", -1, "01") == 0);
	CHECK(expect(&host, "$DOY$", -1, "001") == 0);
	CHECK(expect(&host, "$WOY$", -1, "01") == 0);
	CHECK(expect(&host, "$WOYM$", -1, "00") == 0);
	local.tm_year = 124; local.tm_wday = 1; local.tm_hour = 12;
	CHECK(expect(&host, "$HOUR$", -1, "12") == 0);
	CHECK(expect(&host, "$AMPM$", -1, "PM") == 0);
	CHECK(expect(&host, "$WOY$", -1, "00") == 0);
	CHECK(expect(&host, "$WOYM$", -1, "01") == 0);
	local = (struct tm){.tm_year=123,.tm_mon=11,.tm_mday=31,.tm_wday=0,.tm_yday=364};
	CHECK(expect(&host, "$DOY$", -1, "365") == 0);
	CHECK(expect(&host, "$WOY$", -1, "53") == 0);
	CHECK(expect(&host, "$WOYM$", -1, "52") == 0);
	local = (struct tm){.tm_year=124,.tm_mon=11,.tm_mday=31,.tm_wday=2,.tm_yday=365};
	CHECK(expect(&host, "$DOY$", -1, "366") == 0);
	CHECK(expect(&host, "$WOY$", -1, "52") == 0);
	CHECK(expect(&host, "$WOYM$", -1, "53") == 0);
	local = (struct tm){.tm_year=99,.tm_mon=11,.tm_mday=31,.tm_wday=5,.tm_yday=364};
	CHECK(expect(&host, "$DATE$", -1, "12/31/99") == 0);
	local = (struct tm){.tm_year=100,.tm_mon=0,.tm_mday=1,.tm_wday=6,.tm_yday=0};
	CHECK(expect(&host, "$DATETIME$", -1, "01/01/00 00:00:00") == 0);
	CHECK(expect(&host, "$YEAR$", -1, "00") == 0);
	CHECK(expect(&host, "$FYEAR$", -1, "2000") == 0);
	local = (struct tm){.tm_year=100,.tm_mon=1,.tm_mday=29,.tm_wday=2,.tm_yday=59};
	CHECK(expect(&host, "$DOY$", -1, "060") == 0);
	CHECK(expect(&host, "$WDAY$", -1, "2") == 0);
	local = (struct tm){.tm_year=-1899,.tm_mon=0,.tm_mday=1,.tm_wday=1,.tm_yday=0};
	CHECK(expect(&host, "$FYEAR$", -1, "0001") == 0);
	CHECK(expect(&host, "$YEAR$", -1, "01") == 0);
	local = (struct tm){.tm_year=8099,.tm_mon=11,.tm_mday=31,.tm_wday=5,.tm_yday=364};
	CHECK(expect(&host, "$FYEAR$", -1, "9999") == 0);
	CHECK(expect(&host, "$YEAR$", -1, "99") == 0);
	host.time_zone = NULL;
	CHECK(expect(&host, "$TIMEZONE$", -1, "NONE") == 0);
	host.time_zone = "";
	CHECK(expect(&host, "$TIMEZONE$", -1, "NONE") == 0);
	host.time_zone = "Pacific Standard Time";
	CHECK(expect(&host, "$TIMEZONE$", -1, "Pacific Standard Time") == 0);
	host.time_zone = "PST";
	CHECK(expect(&host, "$TIMEZONE$", -1, "PST") == 0);
	memset(huge_zone, 'Z', sizeof(huge_zone) - 1); huge_zone[sizeof(huge_zone) - 1] = 0;
	host.time_zone = huge_zone;
	CHECK(expect(&host, "$TIMEZONE$", -1, huge_zone) == 0);
	for (unsigned variant = 0; variant < 16; variant++) {
		local = leap_day;
		switch (variant) {
		case 0: local.tm_year = INT_MAX; break;
		case 1: local.tm_year = INT_MIN; break;
		case 2: local.tm_mon = 12; break;
		case 3: local.tm_mday = 30; break;
		case 4: local.tm_hour = 24; break;
		case 5: local.tm_sec = 60; break;
		case 6: local.tm_wday = 7; break;
		case 7: local.tm_yday = 366; break;
		case 8: local.tm_mon = -1; break;
		case 9: local.tm_mday = 0; break;
		case 10: local.tm_hour = -1; break;
		case 11: local.tm_min = -1; break;
		case 12: local.tm_min = 60; break;
		case 13: local.tm_sec = -1; break;
		case 14: local.tm_wday = -1; break;
		case 15: local.tm_yday = -1; break;
		}
		CHECK(expect(&host, "$DATE$", ST_RIP3_HOST_OPERATION_FAILED, NULL) == 0);
		local = leap_day;
		CHECK(expect(&host, "$DATE$", -1, "02/29/24") == 0);
	}
	local.tm_year = 0; local.tm_wday = 4;
	CHECK(expect(&host, "$DATE$", ST_RIP3_HOST_OPERATION_FAILED, NULL) == 0); /* 1900 not leap. */
	host.local_time = NULL;
	CHECK(expect(&host, "$DATE$", ST_RIP3_HOST_UNSUPPORTED_REFERENCE, NULL) == 0);
	host.time_zone = NULL;
	CHECK(expect(&host, "$TIMEZONE$", -1, "NONE") == 0);
	struct test_clock clock = {0};
	host.clock_context = &clock;
	host.local_clock = read_clock;
	CHECK(expect(&host, "$DATETIME(1)$", ST_RIP3_HOST_INVALID_REFERENCE, NULL) == 0);
	CHECK(clock.calls == 0);
	CHECK(expect(&host, "$DATETIME$", -1, "02/29/24 23:04:01") == 0);
	CHECK(expect(&host, "$TIME$", -1, "23:04:02") == 0);
	clock.fail = true;
	CHECK(expect(&host, "$DATETIME$", ST_RIP3_HOST_OPERATION_FAILED, NULL) == 0);
	CHECK(expect(&host, "$TIMEZONE$", -1, "NONE") == 0);
	clock.fail = false;
	CHECK(expect(&host, "$DATETIME$", -1, "02/29/24 23:04:05") == 0);
	return 0;
}
