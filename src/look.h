/* How a console's games look (ChaOS): its shader and its settings, and the
 * overlay drawn over it.
 *
 * Pure: no SDL, no database, no files except look_write_cfg. The menus that
 * show these settings and main.c, which keeps them, both go through here, and
 * tools/menu-check.c links it to hold the rows to what they should say.
 *
 * Each console has the one shader made for it, drawn by Diatom (ChaOS-diatom,
 * shaders/), and a console no shader suits has none and shows none:
 *
 *   Game Boy, Game Boy Color, Game Boy Advance   PT SkyWalker541 (MIT, by
 *                                                SkyWalker541)
 *   NES, SNES, Genesis, Master System,           crt-lottes-fast (Timothy
 *   TurboGrafx-16                                Lottes, public domain), or
 *                                                the lighter Scanlines
 *                                                (RiskyJumps, public domain):
 *                                                the Style row
 *   Game Gear, Neo Geo Pocket (Color)            lcd3x (Gigaherz, public
 *                                                domain)
 *
 * This file only names their settings: the table below is each shader's own
 * parameter list, with the GLSL names Diatom reads, its defaults and ranges,
 * in its order. What the launcher adds is grouping, words for the values, and
 * which rows matter when.
 */
#ifndef TORTOS_LOOK_H
#define TORTOS_LOOK_H

#include <stdbool.h>
#include <stddef.h>

/* The consoles with a shader, by systems.cfg tag. Any other console has no
 * shader, and its menus show none. */
typedef enum {
	LOOK_NONE, LOOK_GB, LOOK_GBC, LOOK_GBA,
	LOOK_NES, LOOK_SMS, LOOK_MD, LOOK_PCE, LOOK_SFC,
	LOOK_GG, LOOK_NGP, LOOK_NGPC,
	LOOK_CONSOLES
} look_console;

look_console look_console_of(const char *tag);
const char  *look_console_name(look_console c);   /* "gb", for Diatom */

/* The shaders. LS_PT is 0 so that the table's PT rows need not say so. */
typedef enum { LS_PT, LS_CRT, LS_LCD, LS_NONE } look_shader;

look_shader  look_shader_of(look_console c);
const char  *look_shader_name(look_shader s);     /* "PT SkyWalker541" */

/* Every shader's parameters, each shader's in its own order: PT's, then
 * crt-lottes-fast's, then lcd3x's. */
typedef enum {
	LP_GAME_SYSTEM, LP_WHITE_LEVEL, LP_TRANSPARENT_PIXELS, LP_TRANSPARENCY,
	LP_MIN_WHITE_TRANSPARENCY, LP_BACKING_COLOR, LP_BACKING_STRENGTH,
	LP_GLARE_AND_SHEEN, LP_GLARE_STRENGTH, LP_GLARE_SPREAD, LP_SHEEN_STRENGTH,
	LP_SHEEN_DENSITY, LP_SHEEN_HUE, LP_SHEEN_RIPPLE, LP_SHEEN_RIPPLE_SIZE,
	LP_LIGHT_X, LP_LIGHT_Y, LP_LIGHT_DRIFT, LP_DARKEN, LP_PIXEL_EFFECT,
	LP_GAP_COLOR, LP_GRID_WIDTH, LP_GRID_OPACITY, LP_DOT_SIZE, LP_DOT_SOFTNESS,
	LP_DOT_GAP_OPACITY, LP_DOT_MIN_BRIGHTNESS, LP_SHADOW_OPACITY,
	LP_SHADOW_DISTANCE, LP_SHADOW_DIRECTION, LP_BEZEL_SHADOW,
	/* crt-lottes-fast */
	LP_CRT_MASK, LP_CRT_MASK_INTENSITY, LP_CRT_SCANLINE_THINNESS, LP_CRT_SCAN_BLUR,
	LP_CRT_CURVATURE, LP_CRT_TRINITRON, LP_CRT_CORNER, LP_CRT_GAMMA,
	/* lcd3x */
	LP_LCD_SCANLINES, LP_LCD_STRIPES,
	/* a CRT console's Style: crt-lottes-fast (0) or Scanlines (1), and the
	 * Scanlines' own (res-independent-scanlines.glsl) */
	LP_CRT_STYLE, LP_SCAN_AMP, LP_SCAN_LINES_BLACK,
	/* a Game Boy console's Style: PT SkyWalker541 (0) or lcd3x (1), whose
	 * own settings are lcd3x's above */
	LP_PT_STYLE,
	LP_COUNT
} look_param;

/* PT's settings are grouped into pages, after Game System, which is on the
 * shader's own page because it decides most of the rest. The other shaders
 * have few enough to sit on their own page (LG_PAGE). */
typedef enum { LG_SCREEN, LG_PIXELS, LG_SHADOWS, LG_LIGHT, LG_COUNT,
               LG_PAGE = LG_COUNT } look_group;

typedef struct {
	const char *name;    /* the GLSL name, which is what Diatom reads */
	const char *label;   /* the row's label */
	float def, min, max, step;
	look_group group;
	int fmt;             /* how the value reads: see look.c */
	const char *const *words;   /* for a value that is a choice, its names */
	look_shader shader;  /* whose it is */
} look_param_info;

extern const look_param_info look_params[LP_COUNT];
const char *look_group_label(look_group g);

/* One console's settings. */
typedef struct {
	bool  on;
	float v[LP_COUNT];
	/* Not kept: whether the console's Display Mode may draw at a size that
	 * is no whole-number scale (Aspect, Stretch). PT's Pixel Effect follows
	 * the game's pixels, which are then not all one size, so ChaOS-diatom
	 * draws it only at Integer, and its rows are not shown otherwise. */
	bool  any_size;
} look_settings;

/* What a console starts from, and what Reset puts back: on, and for PT the
 * shader README's recommended Game system and Backing color for it, for
 * crt-lottes-fast a flat screen (ChaOS-diatom's shading.c says why), the rest
 * the shader's own defaults. */
void look_recommended(look_console c, look_settings *out);

/* One step left (-1) or right (+1), `mult` steps at a time while held, kept
 * in range and on the step's grid. A choice wraps; a number stops at its
 * ends. Returns whether the value changed. */
bool look_step(look_settings *s, look_param p, int dir, int mult);

/* The value as the row shows it: "Grid", "35%", "Off", "1.5". */
void look_format(const look_settings *s, look_param p, char *out, size_t n);

/* Does the row matter with the settings as they are? A Grid's line width with
 * LCD Dot chosen does not, and is not shown. */
bool look_param_shown(const look_settings *s, look_param p);

/* A group's rows that are shown, in order, of shader sh's parameters (for
 * LG_PAGE, the rows on its own page). Returns how many. */
int look_group_rows(const look_settings *s, look_shader sh, look_group g, look_param *out);

/* The shader's name as the Style row shows it: a CRT console's choice, or a
 * Game Boy console's. */
const char *look_style_name(look_console c, const look_settings *s);

/* The shader a console's games are drawn with as set: its own, or for a Game
 * Boy console with Style lcd3x, LS_LCD. */
look_shader look_active_shader(look_console c, const look_settings *s);

/* Is p one of the console's settings, kept for it (its shader's, its Style,
 * and for a Game Boy console lcd3x's too)? */
bool look_param_kept(look_console c, look_param p);

/* The file Diatom's SETSHADER reads: console, enabled, the style for a CRT
 * console, every parameter of the shader in use. Returns false when it could
 * not be written. */
bool look_write_cfg(const char *path, look_console c, const look_settings *s);

/* A stored value back into the settings, by GLSL name. False for a name the
 * table does not have, which a newer or older launcher may have left. */
bool look_set_named(look_settings *s, const char *name, float v);

/* The overlay's row text from its file name: "Clean Frame" for
 * "Clean Frame.png". */
void look_overlay_label(const char *file, char *out, size_t n);

#endif
