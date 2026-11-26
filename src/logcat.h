/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_LOGCAT_H
#define CHAOS_LOGCAT_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: About ChaOS > Logging. chaos.log's lines by what they are about,
 * from the word before the colon every line starts with ("romm: ...", "muse:
 * ..."), so a player can keep the log to what matters to them. A line whose
 * group is off is not written at all.
 *
 * Never dropped, whatever is off: the updater's lines ("update:"), the log's
 * own ("logs:"), a line in no group, and any line that says something went
 * wrong (failed, error, couldn't...), since those are what a log is for.
 *
 * No SDL, no state but the switches: tools/logcat-check.c links it alone. */
typedef enum {
	LOGCAT_ONLINE,   /* Wi-Fi, RomM, Save Sync, RetroAchievements, downloads, art */
	LOGCAT_GAMES,    /* starting and ending games, ports, cheats, the shelves */
	LOGCAT_MUSE,     /* the music and audiobook player */
	LOGCAT_SYSTEM,   /* battery, power, sound, screen, buttons, the clock, settings */
	LOGCAT_PERF,     /* each emulator's RAM, CPU and GPU (perf.h); off by default */
	LOGCAT_COUNT,
	LOGCAT_NONE = -1
} logcat;

/* On is the default for every group but Performance. */
void   logcat_set(logcat c, bool on);
bool   logcat_get(logcat c);

/* Which group a line belongs to, from its start; LOGCAT_NONE for none. */
logcat logcat_of(const char *line, size_t n);

/* Whether a line (its first `n` bytes, the start of it at least) is written. */
bool   logcat_keep(const char *line, size_t n);

#endif
