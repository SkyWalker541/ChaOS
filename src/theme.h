/* The menus' accent color (ChaOS): the panels' border and glow, the heading's
 * rule, the systems row, ChaOS Transfer's and every other launcher menu. A
 * console's own menu keeps that console's color from systems.cfg.
 *
 * Chosen in MENU > Menu Color and kept in the device database as "menu_color",
 * by id, so a renamed or reordered list keeps everyone's choice. ChaOS, the
 * logo's orange, is the default; ChaOS Cyan and Cream are the logo's other
 * two colors; TortOS Cyan is the accent ChaOS started from.
 *
 * Pure, so tools/menu-check.c can link it. */
#ifndef CHAOS_THEME_H
#define CHAOS_THEME_H

typedef struct {
	const char *id;      /* what is stored */
	const char *name;    /* what the row says */
	unsigned    rgb;     /* 0xRRGGBB */
} menu_color;

static const menu_color MENU_COLORS[] = {
	{ "chaos",  "ChaOS Orange", 0xFF7A2Fu },
	{ "cyan",   "ChaOS Cyan",   0x39C5D6u },
	{ "cream",  "Cream",        0xF4EAD5u },
	{ "tortos", "TortOS Cyan",  0x3DD6FFu },
	{ "lime",   "Lime",         0x9BE15Du },
	{ "gold",   "Gold",         0xFFC23Du },
	{ "red",    "Red",          0xFF4F5Au },
	{ "pink",   "Pink",         0xFF6FB5u },
	{ "purple", "Purple",       0xA98BFFu },
	{ "blue",   "Blue",         0x5B8CFFu },
};
#define MENU_COLOR_COUNT ((int)(sizeof MENU_COLORS / sizeof MENU_COLORS[0]))

/* The color stored as `id`, or ChaOS Orange for none or one not known. */
static inline int menu_color_index(const char *id)
{
	int i;
	for (i = 0; id && i < MENU_COLOR_COUNT; i++) {
		const char *a = MENU_COLORS[i].id, *b = id;
		while (*a && *a == *b) { a++; b++; }
		if (!*a && !*b) return i;
	}
	return 0;
}

/* The next or previous color, going round. */
static inline int menu_color_step(int i, int d)
{
	return ((i + d) % MENU_COLOR_COUNT + MENU_COLOR_COUNT) % MENU_COLOR_COUNT;
}

#endif
