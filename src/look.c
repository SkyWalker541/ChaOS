/* How a console's games look (ChaOS). See look.h. */
#include "look.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

enum { F_WORDS, F_PCT, F_PCT_OFF, F_DARKEN, F_NUM1, F_NUM2, F_SPEED };

static const char *const W_SYSTEM[] = { "Manual", "Game Boy", "Game Boy Color",
                                        "GBA SP", "GBA", NULL };
static const char *const W_TRANSP[] = { "White", "Bright", "All", NULL };
static const char *const W_BACKING[] = { "Off", "Pocket", "Grey", "White", NULL };
static const char *const W_ONOFF[] = { "Off", "On", NULL };
static const char *const W_EFFECT[] = { "Off", "Grid", "LCD Dot", NULL };
static const char *const W_GAP[] = { "Backing", "Black", "White", NULL };
static const char *const W_DIR[] = { "Down right", "Down left", "Up right", "Up left", NULL };

/* The shader's parameter list (PT_SkyWalker541.glsl, and pt_shader.c in
 * ChaOS-diatom, which match): name, default, range and step are its own. */
const look_param_info look_params[LP_COUNT] = {
	[LP_GAME_SYSTEM]            = { "PT_GAME_SYSTEM", "Game System", 2, 0, 4, 1, LG_SCREEN, F_WORDS, W_SYSTEM },
	[LP_WHITE_LEVEL]            = { "PT_MANUAL_WHITE_DETECTION_LEVEL", "White Detection Level", 0.85f, 0, 1, 0.01f, LG_SCREEN, F_PCT, NULL },
	[LP_TRANSPARENT_PIXELS]     = { "PT_TRANSPARENT_PIXELS", "Transparent Pixels", 2, 0, 2, 1, LG_SCREEN, F_WORDS, W_TRANSP },
	[LP_TRANSPARENCY]           = { "PT_TRANSPARENCY_AMOUNT", "Transparency", 0.10f, 0, 1, 0.01f, LG_SCREEN, F_PCT_OFF, NULL },
	[LP_MIN_WHITE_TRANSPARENCY] = { "PT_MINIMUM_WHITE_TRANSPARENCY", "White Transparency", 0.20f, 0, 1, 0.01f, LG_SCREEN, F_PCT, NULL },
	[LP_BACKING_COLOR]          = { "PT_BACKING_COLOR", "Backing Color", 1, 0, 3, 1, LG_SCREEN, F_WORDS, W_BACKING },
	[LP_BACKING_STRENGTH]       = { "PT_BACKING_COLOR_STRENGTH", "Backing Strength", 2, 0, 2, 0.05f, LG_SCREEN, F_PCT, NULL },
	[LP_GLARE_AND_SHEEN]        = { "PT_GLARE_AND_SHEEN", "Glare and Sheen", 0, 0, 1, 1, LG_LIGHT, F_WORDS, W_ONOFF },
	[LP_GLARE_STRENGTH]         = { "PT_GLARE_STRENGTH", "Glare Strength", 0.15f, 0, 1, 0.01f, LG_LIGHT, F_PCT_OFF, NULL },
	[LP_GLARE_SPREAD]           = { "PT_GLARE_SPREAD", "Glare Spread", 0.35f, 0.05f, 1, 0.01f, LG_LIGHT, F_PCT, NULL },
	[LP_SHEEN_STRENGTH]         = { "PT_SHEEN_STRENGTH", "Sheen Strength", 0.12f, 0, 0.5f, 0.01f, LG_LIGHT, F_PCT_OFF, NULL },
	[LP_SHEEN_DENSITY]          = { "PT_SHEEN_BAND_DENSITY", "Sheen Bands", 2, 0.5f, 6, 0.1f, LG_LIGHT, F_NUM1, NULL },
	[LP_SHEEN_HUE]              = { "PT_SHEEN_HUE_TURN", "Sheen Color Turn", 0, 0, 1, 0.02f, LG_LIGHT, F_PCT_OFF, NULL },
	[LP_SHEEN_RIPPLE]           = { "PT_SHEEN_RIPPLE", "Sheen Ripple", 0, 0, 1, 0.05f, LG_LIGHT, F_PCT_OFF, NULL },
	[LP_SHEEN_RIPPLE_SIZE]      = { "PT_SHEEN_RIPPLE_SIZE", "Ripple Size", 0.6f, 0.1f, 3, 0.05f, LG_LIGHT, F_NUM2, NULL },
	[LP_LIGHT_X]                = { "PT_LIGHT_HOME_X", "Light Left to Right", 0.5f, -0.5f, 1.5f, 0.01f, LG_LIGHT, F_PCT, NULL },
	[LP_LIGHT_Y]                = { "PT_LIGHT_HOME_Y", "Light Top to Bottom", 0.45f, -0.5f, 1.5f, 0.01f, LG_LIGHT, F_PCT, NULL },
	[LP_LIGHT_DRIFT]            = { "PT_LIGHT_DRIFT_SPEED", "Light Drift", 0.5f, 0, 4, 0.05f, LG_LIGHT, F_SPEED, NULL },
	[LP_DARKEN]                 = { "PT_DARKEN_PICTURE", "Darken Picture", 0, 0, 100, 1, LG_SCREEN, F_DARKEN, NULL },
	[LP_PIXEL_EFFECT]           = { "PT_PIXEL_EFFECT", "Pixel Effect", 1, 0, 2, 1, LG_PIXELS, F_WORDS, W_EFFECT },
	[LP_GAP_COLOR]              = { "PT_GAP_COLOR", "Gap Color", 0, 0, 2, 1, LG_PIXELS, F_WORDS, W_GAP },
	[LP_GRID_WIDTH]             = { "PT_GRID_LINE_WIDTH", "Grid Line Width", 0.25f, 0, 0.5f, 0.01f, LG_PIXELS, F_PCT, NULL },
	[LP_GRID_OPACITY]           = { "PT_GRID_LINE_OPACITY", "Grid Line Strength", 0.5f, 0, 1, 0.01f, LG_PIXELS, F_PCT_OFF, NULL },
	[LP_DOT_SIZE]               = { "PT_DOT_SIZE", "Dot Size", 0.9f, 0.1f, 0.9f, 0.01f, LG_PIXELS, F_PCT, NULL },
	[LP_DOT_SOFTNESS]           = { "PT_DOT_EDGE_SOFTNESS", "Dot Edge Softness", 1, 0, 1, 0.01f, LG_PIXELS, F_PCT, NULL },
	[LP_DOT_GAP_OPACITY]        = { "PT_DOT_GAP_OPACITY", "Dot Gap Strength", 0.5f, 0, 1, 0.01f, LG_PIXELS, F_PCT_OFF, NULL },
	[LP_DOT_MIN_BRIGHTNESS]     = { "PT_MIN_BRIGHTNESS_FOR_DOTS", "Dots From Brightness", 0.15f, 0, 1, 0.01f, LG_PIXELS, F_PCT, NULL },
	[LP_SHADOW_OPACITY]         = { "PT_DROP_SHADOW_OPACITY", "Drop Shadow", 0.3f, 0, 1, 0.01f, LG_SHADOWS, F_PCT_OFF, NULL },
	[LP_SHADOW_DISTANCE]        = { "PT_SHADOW_DISTANCE", "Shadow Distance", 1, -10, 10, 0.5f, LG_SHADOWS, F_NUM1, NULL },
	[LP_SHADOW_DIRECTION]       = { "PT_SHADOW_DIRECTION", "Shadow Direction", 0, 0, 3, 1, LG_SHADOWS, F_WORDS, W_DIR },
	[LP_BEZEL_SHADOW]           = { "PT_BEZEL_EDGE_SHADOW", "Bezel Edge Shadow", 0.4f, 0, 1, 0.01f, LG_SHADOWS, F_PCT_OFF, NULL },
};

static const char *const GROUP_LABEL[LG_COUNT] = {
	"Screen", "Pixels", "Shadows", "Glare and Sheen"
};

const char *look_group_label(look_group g)
{
	return g >= 0 && g < LG_COUNT ? GROUP_LABEL[g] : "";
}

look_console look_console_of(const char *tag)
{
	if (!tag) return LOOK_NONE;
	if (!strcasecmp(tag, "GB"))  return LOOK_GB;
	if (!strcasecmp(tag, "GBC")) return LOOK_GBC;
	if (!strcasecmp(tag, "GBA")) return LOOK_GBA;
	return LOOK_NONE;
}

const char *look_console_name(look_console c)
{
	return c == LOOK_GB ? "gb" : c == LOOK_GBC ? "gbc" : c == LOOK_GBA ? "gba" : "";
}

/* The shader README's "Recommended Settings": Game system and Backing color
 * per console. GBA is the original, unlit model, whose screen this shader
 * recreates; GBA SP is a step left on the Game System row. */
void look_recommended(look_console c, look_settings *out)
{
	int i;

	out->on = true;
	for (i = 0; i < LP_COUNT; i++) out->v[i] = look_params[i].def;
	switch (c) {
	case LOOK_GB:  out->v[LP_GAME_SYSTEM] = 1; out->v[LP_BACKING_COLOR] = 1; break;
	case LOOK_GBC: out->v[LP_GAME_SYSTEM] = 2; out->v[LP_BACKING_COLOR] = 1; break;
	case LOOK_GBA: out->v[LP_GAME_SYSTEM] = 4; out->v[LP_BACKING_COLOR] = 2; break;
	default: break;
	}
}

static int choice(const look_settings *s, look_param p)
{
	return (int)floorf(s->v[p] + 0.5f);
}

bool look_step(look_settings *s, look_param p, int dir, int mult)
{
	const look_param_info *in = &look_params[p];
	float was = s->v[p], v;
	long k, kmax;

	if (p < 0 || p >= LP_COUNT || !dir) return false;
	if (mult < 1) mult = 1;
	kmax = lroundf((in->max - in->min) / in->step);
	if (in->words) {
		/* A choice goes round, one at a time however long it is held. */
		k = lroundf((was - in->min) / in->step) + (dir > 0 ? 1 : -1);
		if (k < 0) k = kmax;
		if (k > kmax) k = 0;
	} else {
		k = lroundf((was - in->min) / in->step) + (long)dir * mult;
		if (k < 0) k = 0;
		if (k > kmax) k = kmax;
	}
	v = in->min + (float)k * in->step;
	if (v > in->max) v = in->max;
	s->v[p] = v;
	return fabsf(v - was) > in->step * 0.25f;
}

void look_format(const look_settings *s, look_param p, char *out, size_t n)
{
	const look_param_info *in = &look_params[p];
	float v = s->v[p];
	int k;

	switch (in->fmt) {
	case F_WORDS:
		k = choice(s, p) - (int)in->min;
		for (int i = 0; in->words[i]; i++)
			if (i == k) { snprintf(out, n, "%s", in->words[i]); return; }
		snprintf(out, n, "%d", choice(s, p));
		return;
	case F_PCT_OFF:
		if (v <= in->step * 0.5f) { snprintf(out, n, "Off"); return; }
		/* fall through */
	case F_PCT:
		snprintf(out, n, "%d%%", (int)lroundf(v * 100.0f));
		return;
	case F_DARKEN:
		if (v < 0.5f) snprintf(out, n, "Off");
		else          snprintf(out, n, "%d%%", (int)lroundf(v));
		return;
	case F_NUM1:  snprintf(out, n, "%.1f", (double)v); return;
	case F_NUM2:  snprintf(out, n, "%.2f", (double)v); return;
	case F_SPEED:
		if (v <= in->step * 0.5f) snprintf(out, n, "Still");
		else                      snprintf(out, n, "%.2fx", (double)v);
		return;
	}
	snprintf(out, n, "%g", (double)v);
}

bool look_param_shown(const look_settings *s, look_param p)
{
	int effect = choice(s, LP_PIXEL_EFFECT);
	bool light = choice(s, LP_GLARE_AND_SHEEN) != 0;

	switch (p) {
	case LP_WHITE_LEVEL:        return choice(s, LP_GAME_SYSTEM) == 0;
	case LP_BACKING_STRENGTH:   return choice(s, LP_BACKING_COLOR) != 0;
	case LP_GAP_COLOR:          return effect != 0;
	case LP_GRID_WIDTH:
	case LP_GRID_OPACITY:       return effect == 1;
	case LP_DOT_SIZE:
	case LP_DOT_SOFTNESS:
	case LP_DOT_GAP_OPACITY:
	case LP_DOT_MIN_BRIGHTNESS: return effect == 2;
	case LP_SHADOW_DISTANCE:
	case LP_SHADOW_DIRECTION:   return s->v[LP_SHADOW_OPACITY] > 0.005f;
	case LP_GLARE_STRENGTH: case LP_GLARE_SPREAD: case LP_SHEEN_STRENGTH:
	case LP_SHEEN_DENSITY: case LP_SHEEN_HUE: case LP_SHEEN_RIPPLE:
	case LP_LIGHT_X: case LP_LIGHT_Y: case LP_LIGHT_DRIFT:
		return light;
	case LP_SHEEN_RIPPLE_SIZE:  return light && s->v[LP_SHEEN_RIPPLE] > 0.005f;
	default:                    return true;
	}
}

/* The order rows appear in on each page, which is not the shader's: what is
 * changed most comes first. */
static const look_param ORDER[] = {
	LP_BACKING_COLOR, LP_BACKING_STRENGTH, LP_DARKEN, LP_TRANSPARENT_PIXELS,
	LP_TRANSPARENCY, LP_MIN_WHITE_TRANSPARENCY, LP_WHITE_LEVEL,
	LP_PIXEL_EFFECT, LP_GAP_COLOR, LP_GRID_WIDTH, LP_GRID_OPACITY, LP_DOT_SIZE,
	LP_DOT_SOFTNESS, LP_DOT_GAP_OPACITY, LP_DOT_MIN_BRIGHTNESS,
	LP_SHADOW_OPACITY, LP_SHADOW_DISTANCE, LP_SHADOW_DIRECTION, LP_BEZEL_SHADOW,
	LP_GLARE_AND_SHEEN, LP_GLARE_STRENGTH, LP_GLARE_SPREAD, LP_SHEEN_STRENGTH,
	LP_SHEEN_DENSITY, LP_SHEEN_HUE, LP_SHEEN_RIPPLE, LP_SHEEN_RIPPLE_SIZE,
	LP_LIGHT_X, LP_LIGHT_Y, LP_LIGHT_DRIFT,
};

int look_group_rows(const look_settings *s, look_group g, look_param *out)
{
	int n = 0;
	size_t i;

	for (i = 0; i < sizeof ORDER / sizeof ORDER[0]; i++)
		if (look_params[ORDER[i]].group == g && look_param_shown(s, ORDER[i]))
			out[n++] = ORDER[i];
	return n;
}

bool look_write_cfg(const char *path, look_console c, const look_settings *s)
{
	FILE *f = fopen(path, "w");
	int i;

	if (!f) return false;
	fprintf(f, "# Written by ChaOS for Diatom's SETSHADER: PT SkyWalker541\n");
	fprintf(f, "console = %s\nenabled = %d\n", look_console_name(c), s->on ? 1 : 0);
	for (i = 0; i < LP_COUNT; i++)
		fprintf(f, "%s = %g\n", look_params[i].name, (double)s->v[i]);
	return fclose(f) == 0;
}

bool look_set_named(look_settings *s, const char *name, float v)
{
	int i;

	for (i = 0; i < LP_COUNT; i++)
		if (!strcmp(look_params[i].name, name)) {
			if (v < look_params[i].min) v = look_params[i].min;
			if (v > look_params[i].max) v = look_params[i].max;
			s->v[i] = v;
			return true;
		}
	return false;
}

void look_overlay_label(const char *file, char *out, size_t n)
{
	const char *slash = strrchr(file, '/'), *dot;
	size_t len;

	if (slash) file = slash + 1;
	dot = strrchr(file, '.');
	len = dot && dot != file ? (size_t)(dot - file) : strlen(file);
	if (len >= n) len = n - 1;
	memcpy(out, file, len);
	out[len] = '\0';
}
