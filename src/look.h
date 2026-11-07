/* How a console's games look (ChaOS): its shader and its settings, and the
 * overlay drawn over it.
 *
 * Pure: no SDL, no database, no files except look_write_cfg. The menus that
 * show these settings and main.c, which keeps them, both go through here, and
 * tools/menu-check.c links it to hold the rows to what they should say.
 *
 * The shader is PT SkyWalker541 (MIT, by SkyWalker541), drawn by Diatom
 * (ChaOS-diatom, shaders/pt). This file only names its settings: the table
 * below is the shader's own parameter list, with the GLSL names Diatom reads,
 * its defaults and ranges, in its order. What the launcher adds is grouping,
 * words for the values, and which rows matter when.
 */
#ifndef TORTOS_LOOK_H
#define TORTOS_LOOK_H

#include <stdbool.h>
#include <stddef.h>

/* The consoles PT SkyWalker541 is made for, by systems.cfg tag. Any other
 * console has no shader yet, and its menus show none. */
typedef enum { LOOK_NONE, LOOK_GB, LOOK_GBC, LOOK_GBA } look_console;

look_console look_console_of(const char *tag);
const char  *look_console_name(look_console c);   /* "gb", for Diatom */

/* The shader's parameters, in its own order. */
typedef enum {
	LP_GAME_SYSTEM, LP_WHITE_LEVEL, LP_TRANSPARENT_PIXELS, LP_TRANSPARENCY,
	LP_MIN_WHITE_TRANSPARENCY, LP_BACKING_COLOR, LP_BACKING_STRENGTH,
	LP_GLARE_AND_SHEEN, LP_GLARE_STRENGTH, LP_GLARE_SPREAD, LP_SHEEN_STRENGTH,
	LP_SHEEN_DENSITY, LP_SHEEN_HUE, LP_SHEEN_RIPPLE, LP_SHEEN_RIPPLE_SIZE,
	LP_LIGHT_X, LP_LIGHT_Y, LP_LIGHT_DRIFT, LP_DARKEN, LP_PIXEL_EFFECT,
	LP_GAP_COLOR, LP_GRID_WIDTH, LP_GRID_OPACITY, LP_DOT_SIZE, LP_DOT_SOFTNESS,
	LP_DOT_GAP_OPACITY, LP_DOT_MIN_BRIGHTNESS, LP_SHADOW_OPACITY,
	LP_SHADOW_DISTANCE, LP_SHADOW_DIRECTION, LP_BEZEL_SHADOW,
	LP_COUNT
} look_param;

/* The pages the settings are grouped into, after Game System, which is on the
 * shader's own page because it decides most of the rest. */
typedef enum { LG_SCREEN, LG_PIXELS, LG_SHADOWS, LG_LIGHT, LG_COUNT } look_group;

typedef struct {
	const char *name;    /* the GLSL name, which is what Diatom reads */
	const char *label;   /* the row's label */
	float def, min, max, step;
	look_group group;
	int fmt;             /* how the value reads: see look.c */
	const char *const *words;   /* for a value that is a choice, its names */
} look_param_info;

extern const look_param_info look_params[LP_COUNT];
const char *look_group_label(look_group g);

/* One console's settings. */
typedef struct {
	bool  on;
	float v[LP_COUNT];
} look_settings;

/* What a console starts from, and what Reset puts back: on, and the shader
 * README's recommended Game system and Backing color for it, the rest the
 * shader's own defaults. */
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

/* A group's rows that are shown, in order. Returns how many. */
int look_group_rows(const look_settings *s, look_group g, look_param *out);

/* The file Diatom's SETSHADER reads: console, enabled, every parameter.
 * Returns false when it could not be written. */
bool look_write_cfg(const char *path, look_console c, const look_settings *s);

/* A stored value back into the settings, by GLSL name. False for a name the
 * table does not have, which a newer or older launcher may have left. */
bool look_set_named(look_settings *s, const char *name, float v);

/* The overlay's row text from its file name: "Clean Frame" for
 * "Clean Frame.png". */
void look_overlay_label(const char *file, char *out, size_t n);

#endif
