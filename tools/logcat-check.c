/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: About ChaOS > Logging's groups (src/logcat.h). Added with Logging in
 * 2.7.9-test1. Each word a launcher line starts with, read on stdin (the
 * Makefile greps them from src/), is in a group or one of the few always
 * written, so a new kind of line can't slip past a switch unsorted; a group
 * that is off drops its lines but never one that says something went wrong;
 * and the updater's lines are always written. */
#include <stdio.h>
#include <string.h>

#include "../src/logcat.h"

static int fails;
#define CK(c, ...) do { if (!(c)) { fails++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int keep(const char *s) { return logcat_keep(s, strlen(s)); }

int main(void)
{
	static const char *const ALWAYS[] = { "update", "logs", "usage" };
	char w[128];
	int words = 0, c;

	printf("logging: groups\n");
	while (fgets(w, sizeof w, stdin)) {
		size_t i, n = strcspn(w, "\n");
		int always = 0;

		w[n] = '\0';
		if (!n) continue;
		words++;
		for (i = 0; i < sizeof ALWAYS / sizeof ALWAYS[0]; i++) if (!strcmp(w, ALWAYS[i])) always = 1;
		strcat(w, ": x");
		if (always) CK(logcat_of(w, strlen(w)) == LOGCAT_NONE, "\"%s\" is in a group, and should always be written", w);
		else        CK(logcat_of(w, strlen(w)) != LOGCAT_NONE, "\"%s\" is in no group: add it to logcat.c", w);
	}
	CK(words > 30, "only %d words read from the sources", words);

	for (c = 0; c < LOGCAT_COUNT; c++) CK(logcat_get((logcat)c), "group %d is off before anything turned it off", c);
	CK(keep("romm: 12 games") && keep("muse: playing"), "every group on: everything written");

	logcat_set(LOGCAT_ONLINE, false);
	CK(!keep("romm: 12 games"), "Online off drops a RomM line");
	CK(!keep("ra: logged in"), "and a RetroAchievements line");
	CK(keep("romm: the download failed"), "but never one that says it failed");
	CK(keep("romm: Couldn't reach the server"), "or couldn't");
	CK(keep("muse: playing"), "and leaves Muse alone");
	CK(keep("update: ChaOS 2.7.9 found"), "the updater is always written");
	CK(keep("something new: hello"), "a line in no group is written");
	CK(keep("no colon here"), "and one with no word at all");
	logcat_set(LOGCAT_ONLINE, true);
	CK(keep("romm: 12 games"), "Online on again writes it");

	logcat_set(LOGCAT_SYSTEM, false);
	CK(!keep("[in] fd=4 code=304 val=1"), "System off drops button presses");
	CK(!keep("battery: 80%"), "and the battery");
	logcat_set(LOGCAT_SYSTEM, true);
	logcat_set((logcat)99, false);   /* out of range: nothing */
	CK(keep("battery: 80%"), "a group out of range changes nothing");

	if (fails) { printf("logging: %d failed\n", fails); return 1; }
	printf("ok: %d kinds of line, each in its group\n", words);
	return 0;
}
