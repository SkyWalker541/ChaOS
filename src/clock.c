/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Date & Time's arithmetic and words. See clock.h. */
#include "clock.h"

#include <stdio.h>
#include <string.h>

/* West to east by standard time, so left and right move the way a map does
 * and L1/R1 a whole offset at a time. Every offset anyone lives in, and where
 * one splits on daylight saving (Phoenix and Denver, Brisbane and Sydney), a
 * city each way, so the offset is right all year. The standard offsets are
 * checked against the zone database (tools/clock-check.c), so a wrong one, or
 * a misspelled id (which reads as UTC), fails the build's checks. 2.7.9: the
 * thirty cities before this are all here, so no setting changes. */
const clock_zone CLOCK_ZONES[] = {
	{ "Pacific/Pago_Pago",              "Pago Pago",            -660 },
	{ "Pacific/Honolulu",               "Honolulu",             -600 },
	{ "Pacific/Marquesas",              "Marquesas",            -570 },
	{ "America/Anchorage",              "Anchorage",            -540 },
	{ "America/Los_Angeles",            "Los Angeles",          -480 },
	{ "America/Vancouver",              "Vancouver",            -480 },
	{ "America/Tijuana",                "Tijuana",              -480 },
	{ "America/Denver",                 "Denver",               -420 },
	{ "America/Phoenix",                "Phoenix",              -420 },
	{ "America/Edmonton",               "Edmonton",             -420 },
	{ "America/Chicago",                "Chicago",              -360 },
	{ "America/Mexico_City",            "Mexico City",          -360 },
	{ "America/Regina",                 "Regina",               -360 },
	{ "America/Costa_Rica",             "Costa Rica",           -360 },
	{ "America/New_York",               "New York",             -300 },
	{ "America/Toronto",                "Toronto",              -300 },
	{ "America/Bogota",                 "Bogota",               -300 },
	{ "America/Lima",                   "Lima",                 -300 },
	{ "America/Halifax",                "Halifax",              -240 },
	{ "America/Caracas",                "Caracas",              -240 },
	{ "America/Santiago",               "Santiago",             -240 },
	{ "America/Puerto_Rico",            "Puerto Rico",          -240 },
	{ "America/St_Johns",               "St. John's",           -210 },
	{ "America/Sao_Paulo",              "Sao Paulo",            -180 },
	{ "America/Argentina/Buenos_Aires", "Buenos Aires",         -180 },
	{ "America/Noronha",                "Noronha",              -120 },
	{ "Atlantic/Azores",                "Azores",                -60 },
	{ "Atlantic/Cape_Verde",            "Cape Verde",            -60 },
	{ "UTC",                            "UTC",                     0 },
	{ "Europe/London",                  "London",                  0 },
	{ "Europe/Dublin",                  "Dublin",                  0 },
	{ "Europe/Lisbon",                  "Lisbon",                  0 },
	{ "Atlantic/Reykjavik",             "Reykjavik",               0 },
	{ "Europe/Paris",                   "Paris",                  60 },
	{ "Europe/Berlin",                  "Berlin",                 60 },
	{ "Europe/Madrid",                  "Madrid",                 60 },
	{ "Europe/Rome",                    "Rome",                   60 },
	{ "Africa/Lagos",                   "Lagos",                  60 },
	{ "Europe/Athens",                  "Athens",                120 },
	{ "Africa/Johannesburg",            "Johannesburg",          120 },
	{ "Africa/Cairo",                   "Cairo",                 120 },
	{ "Europe/Helsinki",                "Helsinki",              120 },
	{ "Asia/Jerusalem",                 "Jerusalem",             120 },
	{ "Europe/Bucharest",               "Bucharest",             120 },
	{ "Europe/Moscow",                  "Moscow",                180 },
	{ "Europe/Istanbul",                "Istanbul",              180 },
	{ "Asia/Riyadh",                    "Riyadh",                180 },
	{ "Africa/Nairobi",                 "Nairobi",               180 },
	{ "Asia/Tehran",                    "Tehran",                210 },
	{ "Asia/Dubai",                     "Dubai",                 240 },
	{ "Asia/Kabul",                     "Kabul",                 270 },
	{ "Asia/Karachi",                   "Karachi",               300 },
	{ "Asia/Tashkent",                  "Tashkent",              300 },
	{ "Asia/Kolkata",                   "India",                 330 },
	{ "Asia/Colombo",                   "Colombo",               330 },
	{ "Asia/Kathmandu",                 "Kathmandu",             345 },
	{ "Asia/Dhaka",                     "Dhaka",                 360 },
	{ "Asia/Yangon",                    "Yangon",                390 },
	{ "Asia/Bangkok",                   "Bangkok",               420 },
	{ "Asia/Jakarta",                   "Jakarta",               420 },
	{ "Asia/Ho_Chi_Minh",               "Ho Chi Minh",      420 },
	{ "Asia/Shanghai",                  "Shanghai",              480 },
	{ "Asia/Singapore",                 "Singapore",             480 },
	{ "Asia/Hong_Kong",                 "Hong Kong",             480 },
	{ "Asia/Taipei",                    "Taipei",                480 },
	{ "Asia/Manila",                    "Manila",                480 },
	{ "Australia/Perth",                "Perth",                 480 },
	{ "Asia/Tokyo",                     "Tokyo",                 540 },
	{ "Asia/Seoul",                     "Seoul",                 540 },
	{ "Australia/Adelaide",             "Adelaide",              570 },
	{ "Australia/Darwin",               "Darwin",                570 },
	{ "Australia/Sydney",               "Sydney",                600 },
	{ "Australia/Melbourne",            "Melbourne",             600 },
	{ "Australia/Brisbane",             "Brisbane",              600 },
	{ "Pacific/Guam",                   "Guam",                  600 },
	{ "Pacific/Noumea",                 "Noumea",                660 },
	{ "Pacific/Auckland",               "Auckland",              720 },
	{ "Pacific/Fiji",                   "Fiji",                  720 },
	{ "Pacific/Chatham",                "Chatham",       765 },
	{ "Pacific/Tongatapu",              "Tonga",                 780 },
	{ "Pacific/Apia",                   "Samoa",                 780 },
	{ "Pacific/Kiritimati",             "Kiritimati",            840 },
};
const int CLOCK_ZONE_COUNT = (int)(sizeof CLOCK_ZONES / sizeof CLOCK_ZONES[0]);

static const char *const MONTHS[12] = {
	"January", "February", "March", "April", "May", "June", "July",
	"August", "September", "October", "November", "December",
};

static int days_in(int year, int mon)
{
	static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	if (mon == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
		return 29;
	return days[mon];
}

/* v brought back into lo..hi, going round */
static int wrap(int v, int lo, int hi)
{
	int span = hi - lo + 1;

	return ((v - lo) % span + span) % span + lo;
}

time_t clock_step(time_t t, clock_field f, int d)
{
	struct tm tm;
	int year, last;

	localtime_r(&t, &tm);
	tm.tm_sec = 0;
	switch (f) {
	case CLK_YEAR:
		/* Stops rather than going round: 2099 to 2000 is never what a
		 * press of right meant */
		year = tm.tm_year + 1900 + d;
		if (year < 2000) year = 2000;
		if (year > 2099) year = 2099;
		tm.tm_year = year - 1900;
		break;
	case CLK_MONTH:  tm.tm_mon  = wrap(tm.tm_mon + d, 0, 11); break;
	case CLK_DAY:
		tm.tm_mday = wrap(tm.tm_mday + d, 1, days_in(tm.tm_year + 1900, tm.tm_mon));
		break;
	case CLK_HOUR:   tm.tm_hour = wrap(tm.tm_hour + d, 0, 23); break;
	case CLK_MINUTE: tm.tm_min  = wrap(tm.tm_min + d, 0, 59); break;
	default: break;
	}
	/* January 31 moved to February is February's last day, not March 3 */
	last = days_in(tm.tm_year + 1900, tm.tm_mon);
	if (tm.tm_mday > last) tm.tm_mday = last;
	tm.tm_isdst = -1;   /* the zone decides, for the date it now is */
	return mktime(&tm);
}

void clock_field_label(time_t t, clock_field f, char *out, size_t n)
{
	struct tm tm;
	int h;

	localtime_r(&t, &tm);
	h = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
	switch (f) {
	case CLK_YEAR:   snprintf(out, n, "%d", tm.tm_year + 1900); break;
	case CLK_MONTH:  snprintf(out, n, "%s", MONTHS[tm.tm_mon]); break;
	case CLK_DAY:    snprintf(out, n, "%d", tm.tm_mday); break;
	case CLK_HOUR:   snprintf(out, n, "%d %s", h, tm.tm_hour < 12 ? "AM" : "PM"); break;
	case CLK_MINUTE: snprintf(out, n, "%02d", tm.tm_min); break;
	default:         snprintf(out, n, "%s", ""); break;
	}
}

void clock_label(time_t t, char *out, size_t n)
{
	struct tm tm;
	int h;

	localtime_r(&t, &tm);
	h = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
	snprintf(out, n, "%.3s %d, %d:%02d %s", MONTHS[tm.tm_mon], tm.tm_mday,
	         h, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
}

void clock_hhmm(time_t t, bool h24, char *out, size_t n)
{
	struct tm tm;
	int h;

	localtime_r(&t, &tm);
	if (h24) {
		snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
		return;
	}
	h = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
	snprintf(out, n, "%d:%02d %s", h, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
}

int clock_zone_find(const char *id)
{
	int i;

	if (!id) return -1;
	for (i = 0; i < CLOCK_ZONE_COUNT; i++)
		if (!strcmp(CLOCK_ZONES[i].id, id)) return i;
	return -1;
}

int clock_zone_step(const char *id, int d)
{
	int i = clock_zone_find(id);

	if (i < 0) return clock_zone_find("America/New_York");
	i += d;
	if (i < 0) i = 0;
	if (i >= CLOCK_ZONE_COUNT) i = CLOCK_ZONE_COUNT - 1;
	return i;
}

void clock_offset_label(long gmtoff, char *out, size_t n)
{
	long m = gmtoff / 60, a = m < 0 ? -m : m;

	if (!m) snprintf(out, n, "UTC");
	else if (a % 60) snprintf(out, n, "UTC%c%ld:%02ld", m < 0 ? '-' : '+', a / 60, a % 60);
	else snprintf(out, n, "UTC%c%ld", m < 0 ? '-' : '+', a / 60);
}

int clock_zone_jump(const char *id, int d)
{
	int i = clock_zone_find(id), std;

	if (i < 0) return clock_zone_find("America/New_York");
	std = CLOCK_ZONES[i].std_min;
	if (d > 0) {
		while (i < CLOCK_ZONE_COUNT && CLOCK_ZONES[i].std_min == std) i++;
		return i < CLOCK_ZONE_COUNT ? i : clock_zone_find(id);
	}
	/* back: to the start of the offset before this one */
	while (i > 0 && CLOCK_ZONES[i].std_min == std) i--;
	if (CLOCK_ZONES[i].std_min == std) return clock_zone_find(id);   /* the first offset */
	std = CLOCK_ZONES[i].std_min;
	while (i > 0 && CLOCK_ZONES[i - 1].std_min == std) i--;
	return i;
}
