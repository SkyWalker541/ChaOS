/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Does Date & Time step and read the way its rows say?
 *
 *     make check-clock
 *
 * Each row moves only itself, so the edges are the whole of it: the minute
 * after 59, the day after the month's last, January 31 moved to February, a
 * leap year, noon and midnight in 12-hour time, and the zone list's ends.
 * Run in New York, the shipped zone, so daylight saving is in play.
 *
 * No SDL, no device, and the clock is never set: time is a number this file
 * chooses.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/clock.h"

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

/* Local time, from its parts */
static time_t at(int year, int mon, int day, int hour, int min)
{
	struct tm tm = { 0 };

	tm.tm_year = year - 1900; tm.tm_mon = mon - 1; tm.tm_mday = day;
	tm.tm_hour = hour; tm.tm_min = min; tm.tm_isdst = -1;
	return mktime(&tm);
}

static int part(time_t t, clock_field f)
{
	struct tm tm;

	localtime_r(&t, &tm);
	switch (f) {
	case CLK_YEAR:   return tm.tm_year + 1900;
	case CLK_MONTH:  return tm.tm_mon + 1;
	case CLK_DAY:    return tm.tm_mday;
	case CLK_HOUR:   return tm.tm_hour;
	case CLK_MINUTE: return tm.tm_min;
	default:         return tm.tm_sec;
	}
}

static int label_is(time_t t, clock_field f, const char *want)
{
	char got[32];

	clock_field_label(t, f, got, sizeof got);
	if (strcmp(got, want)) { printf("  (read \"%s\")\n", got); return 0; }
	return 1;
}

int main(void)
{
	char s[32];
	time_t t;

	setenv("TZ", "America/New_York", 1);
	tzset();

	printf("stepping:\n");
	t = clock_step(at(2026, 10, 3, 18, 59) + 30, CLK_MINUTE, +1);
	CHECK(part(t, CLK_MINUTE) == 0 && part(t, CLK_HOUR) == 18,
	      "the minute after 59 is 0 of the same hour");
	CHECK(part(t, -1) == 0, "and the seconds are zero");
	t = clock_step(at(2026, 10, 31, 9, 0), CLK_DAY, +1);
	CHECK(part(t, CLK_DAY) == 1 && part(t, CLK_MONTH) == 10,
	      "the day after the 31st is the 1st of the same month");
	t = clock_step(at(2026, 1, 31, 9, 0), CLK_MONTH, +1);
	CHECK(part(t, CLK_MONTH) == 2 && part(t, CLK_DAY) == 28,
	      "January 31 moved to February is February 28");
	t = clock_step(at(2028, 1, 31, 9, 0), CLK_MONTH, +1);
	CHECK(part(t, CLK_DAY) == 29, "and February 29 in a leap year");
	t = clock_step(at(2026, 12, 15, 9, 0), CLK_MONTH, +1);
	CHECK(part(t, CLK_MONTH) == 1 && part(t, CLK_YEAR) == 2026,
	      "December's next month is January of the same year");
	t = clock_step(at(2026, 10, 3, 23, 10), CLK_HOUR, +1);
	CHECK(part(t, CLK_HOUR) == 0 && part(t, CLK_DAY) == 3,
	      "the hour after 11 PM is midnight of the same day");
	t = clock_step(at(2026, 10, 3, 0, 10), CLK_HOUR, -1);
	CHECK(part(t, CLK_HOUR) == 23, "and back again");
	t = clock_step(at(2099, 6, 1, 9, 0), CLK_YEAR, +1);
	CHECK(part(t, CLK_YEAR) == 2099, "the year stops at 2099");
	t = clock_step(at(2000, 6, 1, 9, 0), CLK_YEAR, -1);
	CHECK(part(t, CLK_YEAR) == 2000, "and at 2000");
	t = clock_step(at(2018, 9, 4, 10, 43), CLK_YEAR, +8);
	CHECK(part(t, CLK_YEAR) == 2026 && part(t, CLK_MONTH) == 9,
	      "an unset Pixel's 2018 steps up to now");
	/* Daylight saving: July is EDT, January EST, and the hour shown stays
	 * the hour that was set */
	t = clock_step(at(2026, 7, 4, 15, 0), CLK_MONTH, +6);
	CHECK(part(t, CLK_MONTH) == 1 && part(t, CLK_HOUR) == 15,
	      "moving across daylight saving keeps the hour on the clock face");

	printf("words:\n");
	t = at(2026, 10, 3, 18, 5);
	CHECK(label_is(t, CLK_YEAR, "2026"), "the year");
	CHECK(label_is(t, CLK_MONTH, "October"), "the month by name");
	CHECK(label_is(t, CLK_DAY, "3"), "the day without a leading zero");
	CHECK(label_is(t, CLK_HOUR, "6 PM"), "the hour in 12-hour time");
	CHECK(label_is(t, CLK_MINUTE, "05"), "the minute with one");
	CHECK(label_is(at(2026, 10, 3, 0, 0), CLK_HOUR, "12 AM"), "midnight is 12 AM");
	CHECK(label_is(at(2026, 10, 3, 12, 0), CLK_HOUR, "12 PM"), "noon is 12 PM");
	clock_label(t, s, sizeof s);
	CHECK(!strcmp(s, "Oct 3, 6:05 PM"), "the menu row reads \"%s\"", s);
	clock_label(at(2026, 1, 9, 0, 30), s, sizeof s);
	CHECK(!strcmp(s, "Jan 9, 12:30 AM"), "and just after midnight \"%s\"", s);

	printf("status bar clock:\n");
	clock_hhmm(t, false, s, sizeof s);
	CHECK(!strcmp(s, "6:05 PM"), "12-hour, the status bar's words \"%s\"", s);
	clock_hhmm(t, true, s, sizeof s);
	CHECK(!strcmp(s, "18:05"), "24-hour, two digits \"%s\"", s);
	clock_hhmm(at(2026, 1, 9, 0, 30), false, s, sizeof s);
	CHECK(!strcmp(s, "12:30 AM"), "just after midnight is 12:30 AM \"%s\"", s);
	clock_hhmm(at(2026, 1, 9, 0, 30), true, s, sizeof s);
	CHECK(!strcmp(s, "00:30"), "and 00:30 in 24-hour \"%s\"", s);

	printf("zones:\n");
	CHECK(clock_zone_find("America/New_York") >= 0, "the shipped zone is on the list");
	CHECK(clock_zone_find("Mars/Olympus_Mons") < 0, "a zone not on it is not");
	CHECK(clock_zone_step("Mars/Olympus_Mons", +1) == clock_zone_find("America/New_York"),
	      "and steps onto it from New York");
	CHECK(clock_zone_step(CLOCK_ZONES[0].id, -1) == 0, "the west end stops");
	CHECK(clock_zone_step(CLOCK_ZONES[CLOCK_ZONE_COUNT - 1].id, +1) == CLOCK_ZONE_COUNT - 1,
	      "and so does the east");
#define ZI(id) clock_zone_find(id)
	CHECK(!strcmp(CLOCK_ZONES[clock_zone_step("America/New_York", +1)].city, "Toronto"),
	      "one east of New York is Toronto, on the same offset");
	/* L1/R1 (2.7.9): a whole offset at a time, to its first city */
	CHECK(clock_zone_jump("America/New_York", +1) == ZI("America/Halifax"), "R1 from New York: Halifax");
	CHECK(clock_zone_jump("America/New_York", -1) == ZI("America/Chicago"), "L1 from New York: Chicago");
	CHECK(clock_zone_jump("America/Toronto", -1) == ZI("America/Chicago"),
	      "L1 from inside an offset: the start of the one before");
	CHECK(clock_zone_jump("America/Chicago", -1) == ZI("America/Denver"), "and on west: Denver");
	CHECK(clock_zone_jump("Asia/Kolkata", +1) == ZI("Asia/Kathmandu"), "UTC+5:30 to UTC+5:45");
	CHECK(clock_zone_jump(CLOCK_ZONES[0].id, -1) == 0, "L1 stops at the west end");
	CHECK(clock_zone_jump(CLOCK_ZONES[CLOCK_ZONE_COUNT - 1].id, +1) == CLOCK_ZONE_COUNT - 1,
	      "R1 at the east end");
	CHECK(clock_zone_jump("Mars/Olympus_Mons", +1) == ZI("America/New_York"), "an unknown zone: New York");
	{
		/* Every city of the list before 2.7.9, so no one's setting changes */
		static const char *const old[] = {
			"Pacific/Honolulu", "America/Anchorage", "America/Los_Angeles", "America/Phoenix",
			"America/Denver", "America/Chicago", "America/Mexico_City", "America/New_York",
			"America/Halifax", "America/St_Johns", "America/Sao_Paulo", "UTC", "Europe/London",
			"Europe/Paris", "Europe/Berlin", "Africa/Lagos", "Europe/Athens", "Africa/Johannesburg",
			"Europe/Moscow", "Asia/Dubai", "Asia/Karachi", "Asia/Kolkata", "Asia/Dhaka",
			"Asia/Bangkok", "Asia/Shanghai", "Asia/Singapore", "Asia/Tokyo", "Australia/Adelaide",
			"Australia/Sydney", "Pacific/Auckland", NULL };
		int k, missing = 0;

		for (k = 0; old[k]; k++) if (ZI(old[k]) < 0) { missing++; printf("  missing %s\n", old[k]); }
		CHECK(!missing, "every zone of the old list is still on it");
	}
	{
		char o[16];

		clock_offset_label(-28800, o, sizeof o); CHECK(!strcmp(o, "UTC-8"), "%s", o);
		clock_offset_label(19800, o, sizeof o);  CHECK(!strcmp(o, "UTC+5:30"), "%s", o);
		clock_offset_label(-12600, o, sizeof o); CHECK(!strcmp(o, "UTC-3:30"), "%s", o);
		clock_offset_label(45900, o, sizeof o);  CHECK(!strcmp(o, "UTC+12:45"), "%s", o);
		clock_offset_label(0, o, sizeof o);      CHECK(!strcmp(o, "UTC"), "%s", o);
	}
	{
		/* Each zone's standard offset is the zone database's (the lesser of
		 * January's and July's, so either hemisphere's winter), and the list
		 * goes west to east by it. A misspelled id reads as UTC and fails
		 * the first, unless it is meant to be on UTC: so each one's file is
		 * looked for too. Ids, not just offsets, matter: daylight saving is
		 * the zone database's to keep. */
		int i, bad = 0, order = 0, nofile = 0;
		int prev = -24 * 60;

		for (i = 0; i < CLOCK_ZONE_COUNT; i++) {
			const time_t jan = 1768478400, jul = 1784116800;   /* 2026-01-15, 2026-07-15 12:00 UTC */
			struct tm a, b;
			long m;
			char f[160];

			setenv("TZ", CLOCK_ZONES[i].id, 1);
			tzset();
			localtime_r(&jan, &a);
			localtime_r(&jul, &b);
			m = (a.tm_gmtoff < b.tm_gmtoff ? a.tm_gmtoff : b.tm_gmtoff) / 60;
			if (m != CLOCK_ZONES[i].std_min) {
				bad++;
				printf("  %s: standard %ld, the list says %d\n", CLOCK_ZONES[i].id, m, CLOCK_ZONES[i].std_min);
			}
			if (CLOCK_ZONES[i].std_min < prev) { order++; printf("  %s is out of order\n", CLOCK_ZONES[i].id); }
			prev = CLOCK_ZONES[i].std_min;
			snprintf(f, sizeof f, "/usr/share/zoneinfo/%s", CLOCK_ZONES[i].id);
			if (access(f, R_OK) != 0) { nofile++; printf("  no zone file for %s\n", CLOCK_ZONES[i].id); }
		}
		CHECK(!bad, "every zone's standard offset is the zone database's (%d zones)", CLOCK_ZONE_COUNT);
		CHECK(!order, "west to east by standard time");
		CHECK(!nofile, "and every id is a zone the database has");
	}

	if (failures) { printf("\n%d check(s) failed\n", failures); return 1; }
	printf("\nok: every check passed\n");
	return 0;
}
