/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See controls.h. The pages, and nothing else. */
#include "controls.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static bool fn_volume;        /* see ctl_set_fn_volume */
static bool battery_lights;   /* see ctl_set_battery_lights */

void ctl_set_fn_volume(bool on) { fn_volume = on; }
void ctl_set_battery_lights(bool on) { battery_lights = on; }

static const char *const NAMES[CTL_PAGES] = {
	[CTL_MOVING]   = "Moving",
	[CTL_SHELF]    = "On the shelf",
	[CTL_MENUS]    = "In menus",
	[CTL_MUSE]     = "In Muse",
	[CTL_ANYWHERE] = "Anywhere",
};

const char *ctl_page_name(ctl_page p)
{
	return NAMES[(unsigned)p < CTL_PAGES ? p : CTL_MOVING];
}

/* "‹  On the shelf · 2 of 5  ›", the arrows only toward a page there is.
 * One buffer a page number deep, so two pagers built for a page and the
 * next are both still good. */
const char *ctl_pager(const char *name, int page, int pages)
{
	static char buf[4][64];
	static int k;
	char *b = buf[k = (k + 1) % 4];

	snprintf(b, sizeof buf[0], "%s%s · %d of %d%s",
	         page > 0 ? "\xe2\x80\xb9  " : "", name, page + 1, pages,
	         page + 1 < pages ? "  \xe2\x80\xba" : "");
	return b;
}

/* ChaOS: a console's controls (see controls.h). Its buttons that do
 * something, four to a page, then its shortcuts; each page under the row that
 * opens Button Mapping, so changing them is one press from reading them. */
#define CC_PER_PAGE 4

static int cc_buttons(const ctl_console *cc, int *idx)
{
	int n = 0, i;
	for (i = 0; i < CTL_CC_BUTTONS; i++)
		if (cc->value[i] && strcmp(cc->value[i], "None")) idx[n++] = i;
	return n;
}

/* What no mapping changes: MENU and its chords, and POWER */
static int cc_shortcuts(const ctl_console *cc, menu_row *out)
{
	int n = 0;
	out[n++] = (menu_row){ "MENU",    "Menu",              false };
	out[n++] = (menu_row){ "MENU+R1", "Fast forward",      false };
	if (cc->rewind)
		out[n++] = (menu_row){ "MENU+L1", "Rewind, held",  false };
	out[n++] = (menu_row){ "MENU+X",  "Screenshot",        false };
	out[n++] = (menu_row){ "POWER",   "Save and turn off", false };
	return n;
}

int ctl_console_pages(const ctl_console *cc)
{
	int idx[CTL_CC_BUTTONS], nb = cc_buttons(cc, idx);
	menu_row sc[8];
	int ns = cc_shortcuts(cc, sc);
	return (nb + CC_PER_PAGE - 1) / CC_PER_PAGE + (ns + CC_PER_PAGE - 1) / CC_PER_PAGE;
}

int ctl_console_rows(const ctl_console *cc, int page, menu_row *out)
{
	int idx[CTL_CC_BUTTONS], nb = cc_buttons(cc, idx);
	int bpages = (nb + CC_PER_PAGE - 1) / CC_PER_PAGE;
	int pages = ctl_console_pages(cc), n = 0, i;
	menu_row sc[8];
	int ns = cc_shortcuts(cc, sc);
	const char *name;

	if (page < 0) page = 0;
	if (page >= pages) page = pages - 1;
	out[n] = (menu_row){ "Button Mapping", NULL, true };
	out[n++].opens = true;
	out[n++] = MENU_RULE;
	if (page < bpages) {
		for (i = page * CC_PER_PAGE; i < nb && i < (page + 1) * CC_PER_PAGE; i++)
			out[n++] = (menu_row){ cc->button[idx[i]], cc->value[idx[i]], false };
		name = "Buttons";
	} else {
		/* shared out evenly over their pages: 3 and 2 rather than 4 and 1 */
		int sp = page - bpages, spages = pages - bpages;
		int per = (ns + spages - 1) / spages;
		for (i = sp * per; i < ns && i < (sp + 1) * per; i++)
			out[n++] = sc[i];
		name = "Shortcuts";
	}
	out[n++] = MENU_RULE;
	out[n++] = MENU_NOTE(ctl_pager(name, page, pages));
	return n;
}

/* The names a console goes by in a heading: the shelf's own where it is
 * short, the everyday short form where it is not */
const char *ctl_console_short(const char *tag, const char *name)
{
	static const char *const SHORT[][2] = {
		{ "GB", "GB" }, { "GBC", "GBC" }, { "GBA", "GBA" }, { "NES", "NES" },
		{ "SFC", "SNES" }, { "MD", "Genesis" }, { "SMS", "SMS" }, { "GG", "GG" },
		{ "PCE", "TG-16" }, { "NGP", "NGP" }, { "NGPC", "NGPC" },
	};
	size_t i;
	for (i = 0; tag && i < sizeof SHORT / sizeof SHORT[0]; i++)
		if (!strcmp(tag, SHORT[i][0])) return SHORT[i][1];
	return name ? name : "";
}

/* A PAIR OF BUTTONS IS WRITTEN "L1/R1", with no air around the slash. Eric's,
 * 2026-09-20: spaced out it reads as two separate things rather than one
 * control with two ends, and it is not how he writes them.
 *
 * EVERY PAGE FITS, so none of them moves while it is being read.
 *
 * The panel holds seven rows under a one-line heading, and the last of the
 * seven is the footer. Six lines of content, measured against the shipped text
 * size 2026-09-19: at eight the footer was drawn half off the panel, and a
 * cursorless list that does not fit scrolls itself, which is right for a
 * synopsis and wrong for a table you are trying to read a button off.
 *
 * That ceiling is why the shelf is two pages. Its seven buttons split where
 * the hand does: the pad and the shoulders get you around, and the four face
 * buttons do something to what you have landed on. */
static int moving(ctl_dir dir, menu_row *out)
{
	int n = 0;

	/* Which axis does what is the whole of UI Direction. Cubic takes both -
	 * systems on one, games on the other - so it has no letter jump. */
	if (dir == CTL_CUBIC) {
		out[n++] = (menu_row){ "Up/Down",    "Console",        false };
		out[n++] = (menu_row){ "Left/Right", "Game",           false };
	} else if (dir == CTL_VERTICAL) {
		out[n++] = (menu_row){ "Up/Down",    "Move",           false };
		out[n++] = (menu_row){ "Left/Right", "Jump by letter", false };
	} else {
		out[n++] = (menu_row){ "Left/Right", "Move",           false };
		out[n++] = (menu_row){ "Up/Down",    "Jump by letter", false };
	}
	out[n++] = (menu_row){ "L1/R1", "A screenful", false };
	/* ChaOS: the letter scrub, with its letter shown big */
	out[n++] = (menu_row){ "L2/R2", "Letter by letter", false };
	return n;
}

static int shelf(ctl_dir dir, menu_row *out)
{
	int n = 0;

	out[n++] = (menu_row){ "A", dir == CTL_CUBIC ? "Start the game"
	                                             : "Open or play",   false };
	/* On the cube there is nowhere to go back to - the cube IS the shelf - so
	 * B is the console's menu there instead. */
	out[n++] = (menu_row){ "B", dir == CTL_CUBIC ? "Console's menu"
	                                             : "Back",           false };
	out[n++] = (menu_row){ "X", "Game details", false };
	out[n++] = (menu_row){ "Y", "Favorite",     false };
	/* Here rather than on Anywhere, which is full: it works outside a game,
	 * and in one L2 and R2 are the game's */
	if (battery_lights)
		out[n++] = (menu_row){ "L2+R2", "Battery level", false };
	return n;
}

/* ChaOS: the menus' own buttons, which no screen named: A and B are in every
 * menu, Left/Right changes a setting in place (held, it keeps going), and in
 * the in-game menu one button held hides the menu to show the game as the
 * shader and overlay chosen draw it. MENU's row stays on Anywhere. */
static int menus(menu_row *out)
{
	int n = 0;

	out[n++] = (menu_row){ "A",          "Choose or open",   false };
	out[n++] = (menu_row){ "B",          "Back",             false };
	out[n++] = (menu_row){ "Left/Right", "Change a setting", false };
	out[n++] = (menu_row){ CTL_PEEK_NAME, "Held: see the game *", false };
	out[n++] = MENU_NOTE("* in the in-game menu");
	return n;
}

static int muse(menu_row *out)
{
	int n = 0;

	/* A on Muse's SHELF opens an album, which the shelf page already says.
	 * These are the player's own buttons, the ones a shelf does not have. */
	out[n++] = (menu_row){ "A",          "Play or pause", false };
	out[n++] = (menu_row){ "L1/R1",      "Track or chapter", false };
	/* Ten seconds a tap and further held - see seek_step in main.c. Said as
	 * what it does, since the numbers do not fit in a value. */
	out[n++] = (menu_row){ "Left/Right", "Seek, faster held", false };
	/* "Mode" rather than "Mode, on Now Playing": you press it on Now Playing,
	 * where the mark beside the track count changes in front of you. Eric's,
	 * 2026-09-20 - the rest of that sentence was saying what the screen was
	 * about to show anyway. "Or speed" since 2026-09-30: on a book, which
	 * plays in order, Y is its speed instead. */
	out[n++] = (menu_row){ "Y",          "Mode or speed", false };
	/* B is not here. It goes back in Muse exactly as it does everywhere else,
	 * and the shelf's page already says so - and with the rule above the
	 * footer this page has room for five, not six. */
	/* The pocket lock. It took SELECT's row: "Close Muse" is SELECT
	 * doing on Muse what the Anywhere page already says it does. F1/F2 are
	 * L3/R3 to anyone coming from another CFW; the guide says so. */
	out[n++] = (menu_row){ fn_volume ? "FN+both Vol" : "F1+F2", "Lock, held", false };
	return n;
}

static int anywhere(menu_row *out)
{
	int n = 0;

	out[n++] = (menu_row){ "SELECT",        "Muse",       false };
	/* MENU is here rather than on a shelf page because it is the same promise
	 * wherever you are: the settings for whatever is in front of you. In a
	 * game it is a different menu, and that page says so. */
	out[n++] = (menu_row){ "MENU",          "Settings",   false };
	out[n++] = (menu_row){ "Volume rocker", "Sound",      false };
	out[n++] = (menu_row){ fn_volume ? "FN+Vol" : "F1/F2", "Brightness", false };
	out[n++] = (menu_row){ "POWER",         "Turn off",   false };
	return n;
}

int ctl_rows(ctl_page p, ctl_dir dir, menu_row *out)
{
	int n;

	switch (p) {
	case CTL_SHELF:    n = shelf(dir, out);  break;
	case CTL_MENUS:    n = menus(out);       break;
	case CTL_MUSE:     n = muse(out);        break;
	case CTL_ANYWHERE: n = anywhere(out);    break;
	default:           n = moving(dir, out); break;
	}
	/* The page is one of several and says so, under the same rule that
	 * divides every other list from its footer. Eric's, 2026-09-20. The rule
	 * is a row here but a thin one, which is what lets these pages keep it
	 * and still fit. ChaOS: the footer is the pager - the page's name, where
	 * it is in the set, and an arrow each way there is a page to go to. */
	out[n++] = MENU_RULE;
	out[n++] = MENU_NOTE(ctl_pager(ctl_page_name(p), (int)p, CTL_PAGES));
	return n;
}
