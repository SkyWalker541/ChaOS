/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TORTOS_CONTROLS_H
#define TORTOS_CONTROLS_H

#include "menu.h"

/* Every button, and what it does where: the one place ON THE DEVICE that says
 * so. Eric's, 2026-09-18.
 *
 * The controls that need it most are the ones no screen announces. SELECT
 * opens Muse from anywhere, the volume keys and F1/F2 work everywhere, and Y
 * sets the play mode on one Muse screen of four. Each of those is learned, or
 * read in a README that is not on the Brick. This page is where a player who
 * has only the handheld can look them up.
 *
 * THE SHELF'S PAGE FOLLOWS UI DIRECTION rather than listing all three. Which
 * axis moves and which jumps by letter IS that setting's effect, so a page
 * describing the two directions you are not in is a page to read past.
 *
 * The device is the source. The README's Controls section is a summary and
 * docs/guide.md is the long version, but this is what the hardware in the
 * player's hands says, and it is the copy that cannot be out of date by being
 * on a different device.
 *
 * Split out of main.c, the way sys_menu.c is: no SDL here, so
 * tools/controls-check.c can link it and read what these pages contain
 * without a renderer. */

/* Main Menu Controls: five pages, because six rows is what a panel holds
 * under its heading and the shelf's seven buttons do not fit on one: the pad
 * and the shoulders move you, the face buttons act on what you moved to.
 *
 * ChaOS: what a game's buttons do is not here but on its console's own
 * Controls (ctl_console_rows below), from the in-game menu and the console's
 * menu, so each screen says only what is true where it is opened. */
typedef enum {
	CTL_MOVING, CTL_SHELF, CTL_MENUS, CTL_MUSE, CTL_ANYWHERE, CTL_PAGES
} ctl_page;

/* ChaOS: the button held in the in-game menu to see the game without the
 * menu, as every page and note that names it says it. The key itself is
 * PEEK_KEY in main.c, beside the code that reads it: the two change together,
 * and a remapped button would change both. */
#define CTL_PEEK_NAME "Y"

/* Which way the shelves run - UI Direction, as cards.h stores it. */
typedef enum { CTL_HORIZONTAL, CTL_VERTICAL, CTL_CUBIC } ctl_dir;

/* Room for the longest page and its footer, with a little spare. */
#define CTL_MAX_ROWS 10

/* What a page is called, in its footer. */
const char *ctl_page_name(ctl_page p);

/* ChaOS: every paged screen's footer - the page's name, "2 of 5", and an
 * arrow toward each side there is a page on, none past the ends. */
const char *ctl_pager(const char *name, int page, int pages);

/* ChaOS: one console's Controls ("GB Controls"). The caller fills in what
 * each of the Brick's buttons does there now, Button Mapping included (the
 * console's names, "Turbo A", "None"), and whether rewind is on. Pages of its
 * buttons that do something, then its shortcuts, each under a Button Mapping
 * row that opens that screen (the only live row). */
#define CTL_CC_BUTTONS 10
typedef struct {
	const char *button[CTL_CC_BUTTONS];   /* the Brick's: "A", "L1" */
	const char *value[CTL_CC_BUTTONS];    /* what it presses there */
	bool        rewind;
} ctl_console;

int ctl_console_pages(const ctl_console *cc);
int ctl_console_rows(const ctl_console *cc, int page, menu_row *out);
/* "GB", "SNES", "TG-16": a console in a heading */
const char *ctl_console_short(const char *tag, const char *name);

/* One page's rows, the rule and footer note included, and how many there are.
 * Every button the Brick has is named on some page, and no page names one
 * twice - what tools/controls-check.c holds this to. A button can appear on
 * two pages when it does two things: A opens a game on the shelf and pauses a
 * track in Muse. */
int ctl_rows(ctl_page p, ctl_dir dir, menu_row *out);

/* Whether brightness is FUNCTION with the volume keys, on a device with no
 * brightness keys of its own (the Pixel 2, plat_brightness_on_volume): the
 * pages then name those where the Brick's say F1/F2, and Muse's lock is
 * FUNCTION with both. The Brick's names until it is set. */
void ctl_set_fn_volume(bool on);

/* Whether the device shows the battery on lights of its own and L2+R2 flashes
 * the level there (the Pixel 2, plat_battery_lights): the shelf's page then
 * says so. Not shown until it is set. */
void ctl_set_battery_lights(bool on);

#endif
