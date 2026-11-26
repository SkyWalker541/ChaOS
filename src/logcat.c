/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See logcat.h. */
#include "logcat.h"

#include <ctype.h>
#include <string.h>

static bool g_on[LOGCAT_COUNT] = { true, true, true, true, false };

void logcat_set(logcat c, bool on) { if ((unsigned)c < LOGCAT_COUNT) g_on[c] = on; }
bool logcat_get(logcat c) { return (unsigned)c < LOGCAT_COUNT ? g_on[c] : true; }

/* Every word a launcher line starts with, by group (grep the sources for
 * 'fprintf(stderr, "'). A new word is in no group, so it is always written
 * until it is added here: logcat-check lists the ones not yet sorted. */
static const struct { const char *word; logcat c; } WORDS[] = {
	{ "romm", LOGCAT_ONLINE }, { "--romm files", LOGCAT_ONLINE },
	{ "--romm games", LOGCAT_ONLINE }, { "--romm match", LOGCAT_ONLINE },
	{ "ra", LOGCAT_ONLINE }, { "cheevos", LOGCAT_ONLINE },
	{ "syncthing", LOGCAT_ONLINE }, { "itch", LOGCAT_ONLINE },
	{ "itchdl", LOGCAT_ONLINE }, { "net", LOGCAT_ONLINE },
	{ "launchbox", LOGCAT_ONLINE }, { "scrape", LOGCAT_ONLINE },
	{ "ss", LOGCAT_ONLINE }, { "art", LOGCAT_ONLINE },
	{ "cover", LOGCAT_ONLINE }, { "album art", LOGCAT_ONLINE },
	{ "updater art", LOGCAT_ONLINE },

	{ "game", LOGCAT_GAMES }, { "diatom", LOGCAT_GAMES },
	{ "to diatom", LOGCAT_GAMES }, { "wire", LOGCAT_GAMES },
	{ "port", LOGCAT_GAMES }, { "ports", LOGCAT_GAMES },
	{ "portmaster", LOGCAT_GAMES }, { "pico8", LOGCAT_GAMES },
	{ "cheats", LOGCAT_GAMES }, { "states", LOGCAT_GAMES },
	{ "discset", LOGCAT_GAMES }, { "turbo", LOGCAT_GAMES },
	{ "autolaunch", LOGCAT_GAMES }, { "gbsort", LOGCAT_GAMES },
	{ "scan", LOGCAT_GAMES }, { "rescan", LOGCAT_GAMES },
	{ "shelf", LOGCAT_GAMES }, { "shelves", LOGCAT_GAMES },
	{ "fav", LOGCAT_GAMES }, { "map", LOGCAT_GAMES },
	{ "coreopts", LOGCAT_GAMES }, { "display mode", LOGCAT_GAMES },
	{ "overlay", LOGCAT_GAMES }, { "overlays", LOGCAT_GAMES },
	{ "screenshot", LOGCAT_GAMES },

	{ "muse", LOGCAT_MUSE },

	{ "perf", LOGCAT_PERF },

	{ "battery", LOGCAT_SYSTEM }, { "power", LOGCAT_SYSTEM },
	{ "audio", LOGCAT_SYSTEM }, { "video", LOGCAT_SYSTEM },
	{ "video init", LOGCAT_SYSTEM }, { "renderer", LOGCAT_SYSTEM },
	{ "window", LOGCAT_SYSTEM }, { "screen texture", LOGCAT_SYSTEM },
	{ "texload", LOGCAT_SYSTEM }, { "ttf", LOGCAT_SYSTEM },
	{ "cube", LOGCAT_SYSTEM }, { "input", LOGCAT_SYSTEM },
	{ "clock", LOGCAT_SYSTEM },
	{ "settings", LOGCAT_SYSTEM }, { "setting", LOGCAT_SYSTEM },
	{ "idle", LOGCAT_SYSTEM }, { "lights", LOGCAT_SYSTEM },
	{ "boot", LOGCAT_SYSTEM }, { "exit", LOGCAT_SYSTEM },
	{ "cleanup", LOGCAT_SYSTEM },
};

logcat logcat_of(const char *line, size_t n)
{
	const char *colon = line ? memchr(line, ':', n) : NULL;
	size_t w, i;

	/* "[in] fd=3 code=...", a button press, has no colon */
	if (line && n >= 5 && !strncmp(line, "[in] ", 5)) return LOGCAT_SYSTEM;
	if (!colon) return LOGCAT_NONE;
	w = (size_t)(colon - line);
	for (i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++)
		if (strlen(WORDS[i].word) == w && !strncmp(WORDS[i].word, line, w)) return WORDS[i].c;
	return LOGCAT_NONE;
}

/* Something went wrong, said the ways the launcher says it */
static bool says_wrong(const char *line, size_t n)
{
	static const char *const BAD[] = {
		"fail", "error", "couldn't", "could not", "can't", "cannot", "not found",
		"refused", "denied", "timed out", "missing", "crash", "invalid", "bad ",
	};
	char low[256];
	size_t i, k = n < sizeof low - 1 ? n : sizeof low - 1;

	for (i = 0; i < k; i++) low[i] = (char)tolower((unsigned char)line[i]);
	low[k] = '\0';
	for (i = 0; i < sizeof BAD / sizeof BAD[0]; i++)
		if (strstr(low, BAD[i])) return true;
	return false;
}

bool logcat_keep(const char *line, size_t n)
{
	logcat c;

	if (!line || n == 0) return true;
	c = logcat_of(line, n);
	if (c == LOGCAT_NONE || g_on[c]) return true;
	return says_wrong(line, n);
}
