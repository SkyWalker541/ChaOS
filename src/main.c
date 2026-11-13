/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * TortOS - a custom firmware for the TrimUI Brick that plays eleven
 * 8-, 16- and 32-bit consoles.
 *
 * The whole design metric is speed. The launcher starts behind the boot
 * animation rather than after it, hands games to an emulator that is already
 * running rather than starting one, and never tears its own display down - so
 * coming back from a game is a frame, not a second and a half. Around that sit
 * RetroAchievements, box art the device fetches itself, a file server over
 * Wi-Fi, Bluetooth audio and play time - each one row in one menu, and none of
 * it in the way of starting a game.
 */
#include "atomic.h"
#include "cheevos.h"
#include "clock.h"
#include "config.h"
#include "bt.h"
#include "btvol.h"
#include "bt_menu.h"
#include "db.h"
#include "stats.h"
#include "sort.h"
#include "coverflow.h"
#include "library.h"
#include "platform.h"
#include "texload.h"
#include "artshrink.h"
#include "artscrape.h"
#include "favorites.h"
#include "hare.h"
#include "hareart.h"
#include "idle.h"
#include "notice.h"
#include "rafetch.h"
#include "rahash.h"
#include "net.h"
#include "keyboard.h"
#include "logpack.h"
#include "wifi.h"
#include "audioout.h"
#include "menu.h"
#include "musec.h"
#include "museart.h"
#include "muselib.h"
#include "ss.h"
#include "ssrun.h"
#include "sys_menu.h"
#include "controls.h"
#include "update.h"
#include "remap.h"
#include "shots.h"
#include "itch.h"
#include "ports.h"
#include "pmcat.h"
#include "libpack.h"
#include "romm.h"
#include "cover.h"
#include "pixname.h"

/* ChaOS: the button held in the in-game menu to see the game whole. Its name
 * on screen is CTL_PEEK_NAME (controls.h): change one, change both. */
#define PEEK_KEY IN_Y
#include "cards.h"
#include "game_menu.h"
#include "look.h"
#include "theme.h"
#include "chaos_mark.h"
#include "ui.h"
#include "wifi_menu.h"
#include "update.h"
#include "../tools/tortos-update/sha256.h"

#include <SDL.h>
#include <SDL_image.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#ifdef __linux__
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#endif
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define BATT_LOW_PCT   10   /* show the low-battery dot at or below this */
/* At or below this, off the charger, the dot blinks: the last minutes before
 * BATT_EMPTY_PCT ends the game and powers off, which a steady dot gave no
 * warning of. A device with a voltage for it goes by that instead
 * (plat_battery_critical_mv). */
#define BATT_CRITICAL_PCT 5
/* At or below this, off the charger, the device saves a running game and
 * powers off rather than let the PMIC cut it at its own limit, which loses the
 * game's progress since its last save and leaves the card dirty. A device
 * with a voltage for it goes by that instead (plat_battery_empty_mv). */
#define BATT_EMPTY_PCT  2
#define TEX_KEEP_NEAR   8   /* card textures kept around the cursor, in view  */
#define TEX_KEEP_FAR    4   /* ...and around the cursor of a system you left  */

/* The row is allowed to glide only across cards this policy keeps decoded. If
 * TEX_KEEP_NEAR ever drops below it, a long move would animate over cold cards
 * and stall exactly as it did before CF_WARM_CARDS existed. */
_Static_assert(TEX_KEEP_NEAR >= CF_WARM_CARDS,
               "the row glides across cards the texture policy must keep");

typedef enum { SCREEN_SYSTEMS, SCREEN_GAMES } screen_id;

typedef struct {
	game_list list;
	SDL_Texture **tex;
	int *tw, *th;
	float *cb;      /* where each card's art stops, for its reflection */
	int cursor;
	/* ChaOS: how this console's games look - its shader's settings and its
	 * overlay's file name under Overlays/<folder>/, empty for none. Kept per
	 * console in the device database (look_load). */
	look_settings look;
	char overlay[256];
	/* ChaOS: index into DMODES, the console's Display Mode */
	int dmode;
	/* Index into SORTS. Zero is by name, which is what every shelf was
	 * before this existed and what an untouched one still is. On Muse's
	 * shelf an ml_order instead, whose zero is by artist. */
	int sort;
	coverflow cf;
	/* NULL on a real shelf, where every game belongs to the system whose
	 * shelf it is. Favorites is a shelf of games drawn from many systems, so
	 * each entry has to carry its own - the core to load, the folder the ROM
	 * is under, the accent, and where the save state lives all follow from
	 * it, and every one of them would be wrong if taken from the shelf. */
	int *owner;
	/* Muse's shelf only, NULL on every other: the album card k is. A card's
	 * cover, a fetched cover landing on it, and A on it all go by the album,
	 * and with two orders a card's place is no longer its album's number. */
	int *album;
	/* The letter jump groups by `name` instead of `title`. Muse's shelf in
	 * artist order has the artist in `name`: grouped by album title there,
	 * up and down would land somewhere that looks arbitrary. In album order
	 * the title is the order, and the jump goes by it as a games shelf's
	 * does. */
	bool jump_by_name;
} sysview;


typedef struct {
	systems_cfg sys;

	SDL_Texture *sys_tex[CFG_MAX_SYSTEMS];
	int sys_w[CFG_MAX_SYSTEMS], sys_h[CFG_MAX_SYSTEMS];
	float sys_cb[CFG_MAX_SYSTEMS];
	sysview view[CFG_MAX_SYSTEMS];

	screen_id screen;
	int sys_cursor;
	int menu_w;             /* cached shelf-menu content width; 0 = unmeasured */
	coverflow cf_sys;
	unsigned tint;          /* eased toward the focused system's accent */
	in_state in;
	bool running;
	/* Set for one launch only: the game that comes back after a shutdown
	 * opens with the menu up. Cleared as it is used, so quitting to the
	 * shelf and launching the same game again behaves normally. */
	bool resume_menu;
	int  auto_off;              /* seconds without input, 0 off */
	idle_clock idle;            /* Auto Off's clock - src/idle.h */
	SDL_Renderer *r;
} app;

/* Defined down with the shelf building it belongs to, declared here because
 * the input loop calls it the moment a favorite changes. */
/* The Ports shelf's first card, which is PortMaster rather than a port: a
 * name no script has, so nothing on the card can be mistaken for it */
#define PM_ENTRY_FILE ".chaos-portmaster"
#define PM_ENTRY_NAME "PortMaster"
static bool pm_is_entry(const char *file) { return file && !strcmp(file, PM_ENTRY_FILE); }
static void pm_get(app *a);   /* the ChaOS menu's Get PortMaster */
static romm_conn *romm_get(void);   /* the ChaOS menu's RomM */
static void romm_screen(app *a);

static void refresh_virtual_shelves(app *a);
static void build_muse_shelf(app *a);
static void muse_shelf_attach(app *a);
static void muse_shelf_detach(app *a);
static void muse_shelf_set(app *a);
/* How a Muse screen was left: back one level, Muse closed altogether, or the
 * library rebuilt under it by Rescan Folder, so every album index it held is
 * stale and the shelf has to find itself again. */
typedef enum { MUSE_BACK, MUSE_CLOSE, MUSE_REBUILT } muse_exit;
static muse_exit muse_tracks(app *a, int album, bool now);
/* Muse's shelf borrows the games shelf's cards and its letter jump, both of
 * which live down with the shelf code. */
static void muse_shelf_screen(app *a, bool now);
static int  muse_artist_of(int al);
static int  shelf_letter_jump(sysview *v, int dir);
/* Whether a shelf is Muse's; sorting needs to know, well before Muse's code.
 * And sorting puts Muse's shelf in order by its own. */
static bool is_muse(const system_cfg *s);
static void muse_order_view(sysview *v);
/* The pocket lock's gate and MENU's way in, defined with the lock itself. */
static bool lock_allowed(void);
static void muse_ask_lock(void);
/* Muse's menu opens it, and the menu is up here. */
static void album_art_screen(app *a);
/* Same reason: Over The Hare is a screen up here and the scan is down there. */
static void rescan_all(app *a);
/* And again: Play Time can send you to a game's shelf, which is the input
 * loop's job and lives with it. */
static void enter_system(app *a);
static int  menu_std_width(app *a);

static volatile sig_atomic_t want_quit;
static void on_sigterm(int sig) { (void)sig; want_quit = 1; plat_terminate(); }

/* Boot time is the one number a launcher cannot be careless about, so the
 * phases are timed and logged rather than guessed at. On the project this
 * grew out of, the phase everyone assumed was expensive turned out to be 8%
 * of startup -- which is only knowable by measuring. */
static unsigned t_boot0;
static void t_mark(const char *what)
{
	fprintf(stderr, "boot: %-14s %5u ms\n", what, plat_now_ms() - t_boot0);
}

/* One number for the way back, because this path keeps being asked how fast it
 * is and a number is how the answer stays true. The five phase marks that took
 * it from 213ms to here are in the history if it ever needs breaking down
 * again; the budget they found was 5ms to black, 4ms of bookkeeping, and the
 * rest decoding the focused card's autosave preview off the SD card. */
static unsigned t_back0;

/* Get off the game's last frame before doing anything slow.
 *
 * Everything between EXIT and the first frame of the fade blocks and none of it
 * presents, so the panel holds whatever the launcher drew last - which after a
 * quit is the in-game menu, over a frame of the game that has ended. Measured
 * on the device: 123ms of it, 52 in the LED writes that ran here then (since
 * removed) and 69 rebuilding the focused card off the SD card.
 *
 * One present, black, because the fade that follows starts from black and the
 * two should agree. This is not the buffer-establishing that was tried and
 * reverted: that guarded a stale back buffer, which a swap cannot show. This
 * gets a stale FRONT frame off the glass, which is measurable and was measured.
 */
static void present_black(app *a)
{
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
	SDL_RenderClear(a->r);
	plat_present();
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
}

/* ---------- per-console look (ChaOS) -------------------------------------
 *
 * Every game is drawn at a whole-number scale, which Diatom decides, so the
 * display mode that was here is gone. What a console chooses instead is its
 * shader and its overlay.
 *
 * Keyed on the system's tag, as saves are: "shader.<TAG>.on", one
 * "shader.<TAG>.<PT_NAME>" per setting that differs from the recommended one
 * (so a better recommendation in a later release reaches everyone who never
 * changed it), and the overlay's file name for each Display Mode:
 * "overlay.<TAG>" for Integer, "overlay.<TAG>.aspect" and ".stretch" for the
 * others (ChaOS: an overlay fits one picture size, see overlay_fits). */
static void overlay_key(const app *a, int i, char *key, size_t n);
static bool overlay_fits(const app *a, int sys, int mode, const char *file);
static void look_load_one(app *a, int i);

static bool look_load_param(const char *key, const char *value, void *ctx)
{
	const char *dot = strrchr(key, '.');

	if (dot) look_set_named(ctx, dot + 1, (float)atof(value));
	return true;
}

/* ChaOS: how a console's games fill the screen, by Diatom's names
 * (ChaOS-diatom src/scale.c). Integer first: whole-number steps, square
 * pixels, and the only one at which PT's Grid is drawn. Aspect fills what the
 * game's shape allows; Stretch fills the screen. Every shader draws at all
 * three. Kept per console as "chaos.display.<tag>": a new key, so a mode
 * TortOS kept under "display." is not picked up. */
static const struct { const char *name, *label; } DMODES[] = {
	{ "integer", "Integer" },
	{ "aspect",  "Aspect"  },
	{ "stretch", "Stretch" },
};
#define DMODE_COUNT ((int)(sizeof DMODES / sizeof DMODES[0]))

static void display_load_one(app *a, int i)
{
	char key[CFG_STR + 32], name[16];
	int k;

	a->view[i].dmode = 0;
	snprintf(key, sizeof key, "chaos.display.%s", a->sys.systems[i].tag);
	db_get_str(db_dev(), key, name, sizeof name, DMODES[0].name);
	for (k = 0; k < DMODE_COUNT; k++)
		if (!strcmp(DMODES[k].name, name)) a->view[i].dmode = k;
}

static void display_save(app *a, int i)
{
	char key[CFG_STR + 32];

	snprintf(key, sizeof key, "chaos.display.%s", a->sys.systems[i].tag);
	if (a->view[i].dmode == 0) db_del(db_dev(), key);
	else db_set_str(db_dev(), key, DMODES[a->view[i].dmode].name);
}

static void look_load_one(app *a, int i)
{
	const system_cfg *sc = &a->sys.systems[i];
	sysview *v = &a->view[i];
	char key[CFG_STR + 32];

	look_recommended(look_console_of(sc->tag), &v->look);
	snprintf(key, sizeof key, "shader.%s.on", sc->tag);
	v->look.on = db_get_int(db_dev(), key, 1) != 0;
	/* every stored parameter, by its GLSL name (".on" is no parameter's) */
	snprintf(key, sizeof key, "shader.%s.", sc->tag);
	db_each_prefix(db_dev(), key, look_load_param, &v->look);
	display_load_one(a, i);
	overlay_key(a, i, key, sizeof key);
	db_get_str(db_dev(), key, v->overlay, sizeof v->overlay, "");
}

static void look_load(app *a)
{
	int i;
	for (i = 0; i < a->sys.count; i++) look_load_one(a, i);
}

/* One console's settings, written: what differs from the recommendation is
 * stored, and what matches it is not. */
static void look_save(app *a, int i)
{
	const system_cfg *sc = &a->sys.systems[i];
	const sysview *v = &a->view[i];
	look_console lc = look_console_of(sc->tag);
	look_settings rec;
	char key[CFG_STR + 64], val[32];
	int k;

	look_recommended(lc, &rec);
	snprintf(key, sizeof key, "shader.%s.on", sc->tag);
	db_set_int(db_dev(), key, v->look.on ? 1 : 0);
	for (k = 0; k < LP_COUNT; k++) {
		if (!look_param_kept(lc, (look_param)k)) continue;
		snprintf(key, sizeof key, "shader.%s.%s", sc->tag, look_params[k].name);
		if (fabsf(v->look.v[k] - rec.v[k]) < look_params[k].step * 0.25f) {
			db_del(db_dev(), key);
		} else {
			snprintf(val, sizeof val, "%g", (double)v->look.v[k]);
			db_set_str(db_dev(), key, val);
		}
	}
	overlay_key(a, i, key, sizeof key);
	if (v->overlay[0]) db_set_str(db_dev(), key, v->overlay);
	else               db_del(db_dev(), key);
}

static bool file_nonempty(const char *p);
/* ChaOS: a console's shader settings. `game` is the in-game menu's context,
 * when it is opened from there, and NULL from the shelf. */
static void shader_screen(app *a, int sys, void *game);

/* ---------- overlays (ChaOS) ----------------------------------------------
 *
 * Overlays/<console folder>/ on the card, one folder per console as Roms/ has,
 * made at every boot so there is always somewhere to put one: a PNG the size
 * of the screen (1024x768 on the Brick; another size is scaled to it), drawn
 * over the game with its transparency. Copied on from a computer, or sent
 * with Over The Hare, whose Overlays folder is this one. */
#define OVERLAYS_DIR "Overlays"
#define OVERLAY_MAX  64

static void overlay_dir(const app *a, int sys, char *out, size_t n)
{
	snprintf(out, n, "%s/%s/%s", P_CARD, OVERLAYS_DIR, a->sys.systems[sys].folder);
}

/* The overlays ChaOS ships (ChaOS/overlays/<console folder>/, see
 * res/overlays/SOURCE.md), put in the card's folder for that console, so they
 * sit with the player's own and can be deleted like them.
 *
 * Every one missing from the card is put back once per ChaOS version - at the
 * first start after an install or an update - whether a release before this
 * one shipped it or this one adds it: an overlay deleted by accident comes
 * back with the next update, and until then stays deleted. A file of the same
 * name already there is left as it is, so nothing of the player's is ever
 * overwritten. Only what is missing is copied, so a start with nothing to do
 * reads two folders and writes nothing. */
static void copy_file(const char *from, const char *to);

static int overlay_seed(const app *a, int sys, const char *dest)
{
	char src[LIB_PATH * 2], from[LIB_PATH * 3], to[LIB_PATH * 3];
	struct dirent *e;
	DIR *dir;
	int n = 0;

	snprintf(src, sizeof src, "%s/overlays/%s", P_ROOT, a->sys.systems[sys].folder);
	if (!(dir = opendir(src))) return 0;
	while ((e = readdir(dir))) {
		if (e->d_name[0] == '.') continue;
		snprintf(from, sizeof from, "%s/%s", src, e->d_name);
		snprintf(to, sizeof to, "%s/%s", dest, e->d_name);
		if (access(to, F_OK) == 0) continue;
		copy_file(from, to);
		n++;
	}
	closedir(dir);
	if (n) fprintf(stderr, "overlays: %d put in %s\n", n, dest);
	return n;
}

/* Overlays an older ChaOS shipped and this one does not: the STN and
 * PixelPop designs, which lay a grid over the game. Taken off the card, and
 * out of ChaOS's own folder, where an update leaves them - but only a file
 * of the very size ChaOS shipped, so a player's own of the same name stays. */
static const struct { const char *folder, *file; long size; } OVERLAYS_RETIRED[] = {
	{ "Game Boy",       "DMG STN.png",             378765 },
	{ "Game Boy Color", "DMGBC STN.png",           377916 },
	{ "Game Boy Color", "Cool STN.png",             74586 },
	{ "Game Boy Color", "Warm STN.png",             75144 },
	{ "Game Boy Color", "Pokemon STN.png",         130648 },
	{ "Game Boy Color", "Cool PixelPop.png",        65239 },
	{ "Game Boy Color", "Warm PixelPop.png",        64650 },
	{ "Game Boy Color", "OG Warm PixelPop.png",    411025 },
};

static void overlays_retire(void)
{
	char p[LIB_PATH * 3];
	struct stat st;
	size_t i;
	int k;

	for (i = 0; i < sizeof OVERLAYS_RETIRED / sizeof OVERLAYS_RETIRED[0]; i++)
		for (k = 0; k < 2; k++) {
			snprintf(p, sizeof p, "%s/%s/%s/%s", k ? P_CARD : P_ROOT,
			         k ? OVERLAYS_DIR : "overlays",
			         OVERLAYS_RETIRED[i].folder, OVERLAYS_RETIRED[i].file);
			if (stat(p, &st) == 0 && (long)st.st_size == OVERLAYS_RETIRED[i].size &&
			    unlink(p) == 0)
				fprintf(stderr, "overlays: %s retired\n", p);
		}
}

static void overlay_dirs_ensure(const app *a)
{
	char d[LIB_PATH * 2];
	int i;

	char was[32];
	bool seed;

	snprintf(d, sizeof d, "%s/%s", P_CARD, OVERLAYS_DIR);
	mkdir(d, 0755);
	/* Once a version (see overlay_seed) */
	seed = !db_get_str(db_lib(), "overlays.seeded.version", was, sizeof was, NULL) ||
	       strcmp(was, CHAOS_VERSION) != 0;
	if (seed) overlays_retire();
	for (i = 0; i < a->sys.count; i++) {
		if (!a->sys.systems[i].folder[0]) continue;
		overlay_dir(a, i, d, sizeof d);
		mkdir(d, 0755);
		if (seed) overlay_seed(a, i, d);
	}
	if (seed) db_set_str(db_lib(), "overlays.seeded.version", CHAOS_VERSION);
}

static void overlay_key(const app *a, int i, char *key, size_t n)
{
	int m = a->view[i].dmode;

	if (m <= 0 || m >= DMODE_COUNT)
		snprintf(key, n, "overlay.%s", a->sys.systems[i].tag);
	else
		snprintf(key, n, "overlay.%s.%s", a->sys.systems[i].tag, DMODES[m].name);
}

/* The console's overlay for its Display Mode, read again: after the mode
 * changes, it is that mode's. */
static void overlay_reload(app *a, int i)
{
	char key[CFG_STR + 64];

	overlay_key(a, i, key, sizeof key);
	db_get_str(db_dev(), key, a->view[i].overlay, sizeof a->view[i].overlay, "");
	/* A choice saved before the fit rule, or for a mode it does not frame, is
	 * no overlay: the menu would not offer it, so the game does not get it. */
	if (a->view[i].overlay[0] &&
	    !overlay_fits(a, i, a->view[i].dmode, a->view[i].overlay))
		a->view[i].overlay[0] = '\0';
}

/* ---- which overlays fit (ChaOS) ---------------------------------------------
 *
 * An overlay frames one picture: its window - where it is see-through - is the
 * size and place of the game at one Display Mode. An overlay is offered, and
 * drawn, only at a mode whose picture its window frames to within OVL_TOL px on
 * every side, in either direction. A window much smaller than the picture
 * hides the game's edges under the frame; one much larger leaves a gap of
 * nothing between the frame and the picture. Both are wrong, so both are out.
 *
 * Measured against the collection as it ships: every Integer overlay frames
 * the Integer picture exactly, every Aspect overlay frames Aspect's to within
 * 3 px, and none fits Stretch.
 *
 * The picture's size is known for the handhelds, whose screens are one size
 * whatever the game. A console whose picture changes with the game (NES, SNES
 * and the rest) has no one size to measure against, and every overlay is
 * offered for it, as before. */
#define OVL_TOL 4

static const struct { const char *tag; int w, h; double aspect; } OVL_SCREENS[] = {
	{ "GB",   160, 144, 10.0 / 9.0 },
	{ "GBC",  160, 144, 10.0 / 9.0 },
	{ "GBA",  240, 160, 3.0 / 2.0 },
	{ "GG",   160, 144, 10.0 / 9.0 },
	{ "NGP",  160, 152, 160.0 / 152.0 },
	{ "NGPC", 160, 152, 160.0 / 152.0 },
};

/* Where the game is drawn at `mode`, as ChaOS-diatom works it out
 * (src/scale.c). False for a console with no one picture size. */
static bool overlay_game_rect(const app *a, int sys, int mode, SDL_Rect *r)
{
	const int W = TORTOS_SCREEN_W, H = TORTOS_SCREEN_H;
	size_t i;

	for (i = 0; i < sizeof OVL_SCREENS / sizeof OVL_SCREENS[0]; i++) {
		int w = OVL_SCREENS[i].w, h = OVL_SCREENS[i].h, f;
		double asp = OVL_SCREENS[i].aspect;

		if (strcmp(OVL_SCREENS[i].tag, a->sys.systems[sys].tag)) continue;
		if (!strcmp(DMODES[mode].name, "integer")) {
			f = W / w < H / h ? W / w : H / h;
			if (f < 1) f = 1;
			r->w = w * f; r->h = h * f;
		} else if (!strcmp(DMODES[mode].name, "aspect")) {
			if ((double)W / asp <= (double)H) { r->w = W; r->h = (int)((double)W / asp + 0.5); }
			else                              { r->w = (int)((double)H * asp + 0.5); r->h = H; }
		} else {
			r->w = W; r->h = H;
		}
		r->x = (W - r->w) / 2;
		r->y = (H - r->h) / 2;
		return true;
	}
	return false;
}

/* An overlay's window: the box round every pixel less than half opaque, in
 * screen pixels (an overlay of another size is scaled to the screen). False
 * for one with none, or that will not load. Kept in the library database by
 * file, size and time, so a PNG is read once, not at every menu. */
static bool overlay_window(const char *path, SDL_Rect *win)
{
	struct stat st;
	char key[LIB_PATH * 3 + 32], val[96];
	long long mt, sz;
	SDL_Surface *raw, *img;
	int x, y, x0, y0, x1, y1;

	if (stat(path, &st) != 0) return false;
	snprintf(key, sizeof key, "overlay.window.%s", path);
	if (db_get_str(db_lib(), key, val, sizeof val, NULL) &&
	    sscanf(val, "%lld %lld %d %d %d %d", &mt, &sz, &win->x, &win->y, &win->w, &win->h) == 6 &&
	    mt == (long long)st.st_mtime && sz == (long long)st.st_size)
		return win->w > 0 && win->h > 0;

	win->x = win->y = win->w = win->h = 0;
	if ((raw = IMG_Load(path))) {
		img = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
		SDL_FreeSurface(raw);
		if (img) {
			x0 = img->w; y0 = img->h; x1 = y1 = -1;
			SDL_LockSurface(img);
			for (y = 0; y < img->h; y++) {
				const uint32_t *row = (const uint32_t *)((const uint8_t *)img->pixels +
				                                         (size_t)y * img->pitch);
				for (x = 0; x < img->w; x++)
					if ((row[x] >> 24) < 128) {
						if (x < x0) x0 = x;
						if (x > x1) x1 = x;
						if (y < y0) y0 = y;
						if (y > y1) y1 = y;
					}
			}
			SDL_UnlockSurface(img);
			if (x1 >= x0 && y1 >= y0) {
				win->x = x0 * TORTOS_SCREEN_W / img->w;
				win->y = y0 * TORTOS_SCREEN_H / img->h;
				win->w = (x1 + 1) * TORTOS_SCREEN_W / img->w - win->x;
				win->h = (y1 + 1) * TORTOS_SCREEN_H / img->h - win->y;
			}
			SDL_FreeSurface(img);
		}
	}
	snprintf(val, sizeof val, "%lld %lld %d %d %d %d", (long long)st.st_mtime,
	         (long long)st.st_size, win->x, win->y, win->w, win->h);
	db_set_str(db_lib(), key, val);
	return win->w > 0 && win->h > 0;
}

/* Does the overlay `file` (in the console's folder) frame the console's
 * picture at Display Mode `mode`? */
static bool overlay_fits(const app *a, int sys, int mode, const char *file)
{
	char path[LIB_PATH * 3];
	SDL_Rect g, w;
	int d[4], k;

	if (!overlay_game_rect(a, sys, mode, &g)) return true;
	overlay_dir(a, sys, path, sizeof path);
	snprintf(path + strlen(path), sizeof path - strlen(path), "/%s", file);
	if (!overlay_window(path, &w)) return false;
	d[0] = w.x - g.x;
	d[1] = w.y - g.y;
	d[2] = (g.x + g.w) - (w.x + w.w);
	d[3] = (g.y + g.h) - (w.y + w.h);
	for (k = 0; k < 4; k++)
		if (d[k] < -OVL_TOL || d[k] > OVL_TOL) return false;
	return true;
}

static int overlay_name_cmp(const void *x, const void *y)
{
	return strcasecmp((const char *)x, (const char *)y);
}

/* The PNGs in a console's folder that fit its Display Mode (overlay_fits),
 * by name. Returns how many. */
static int overlay_list(const app *a, int sys, char (*out)[256], int max)
{
	char d[LIB_PATH * 2];
	struct dirent *e;
	DIR *dir;
	int n = 0;

	overlay_dir(a, sys, d, sizeof d);
	if (!(dir = opendir(d))) return 0;
	while ((e = readdir(dir)) && n < max) {
		const char *dot = strrchr(e->d_name, '.');

		if (e->d_name[0] == '.' || !dot || strcasecmp(dot, ".png")) continue;
		if (strlen(e->d_name) >= 256) continue;
		if (!overlay_fits(a, sys, a->view[sys].dmode, e->d_name)) continue;
		snprintf(out[n++], 256, "%s", e->d_name);
	}
	closedir(dir);
	qsort(out, (size_t)n, sizeof *out, overlay_name_cmp);
	return n;
}

/* How many overlays fit the console's Display Mode. */
static int overlay_count(const app *a, int sys)
{
	static char names[OVERLAY_MAX][256];
	return overlay_list(a, sys, names, OVERLAY_MAX);
}

/* The console's overlay as its row says it: its name, or NULL for none - and
 * for one whose file has gone from the card, or that does not fit the
 * Display Mode, which draw as none. */
static const char *overlay_label(const app *a, int sys, char *buf, size_t n)
{
	const sysview *v = &a->view[sys];
	char path[LIB_PATH * 3];

	if (!v->overlay[0]) return NULL;
	overlay_dir(a, sys, path, sizeof path);
	snprintf(path + strlen(path), sizeof path - strlen(path), "/%s", v->overlay);
	if (!file_nonempty(path)) return NULL;
	if (!overlay_fits(a, sys, v->dmode, v->overlay)) return NULL;
	look_overlay_label(v->overlay, buf, n);
	return buf;
}

/* The next overlay, or the one before: None, then each PNG by name. */
static void overlay_cycle(app *a, int sys, int d)
{
	static char names[OVERLAY_MAX][256];
	sysview *v = &a->view[sys];
	int n = overlay_list(a, sys, names, OVERLAY_MAX), at = -1, i;
	char buf[8];

	if (overlay_label(a, sys, buf, sizeof buf))
		for (i = 0; i < n; i++)
			if (!strcmp(names[i], v->overlay)) at = i;
	/* -1 is None: n + 1 places in all. */
	at = ((at + 1 + d) % (n + 1) + (n + 1)) % (n + 1) - 1;
	if (at < 0) v->overlay[0] = '\0';
	else        snprintf(v->overlay, sizeof v->overlay, "%s", names[at]);
	look_save(a, sys);
}

/* The overlay as Diatom's SETBEZEL takes it: the screen's size, straight
 * alpha, as bytes B, G, R, A after an 8-byte DTOV header. Written to /tmp,
 * which is memory. Returns false for no overlay, or one that will not load. */
static bool overlay_write_dtov(const app *a, int sys, const char *out)
{
	const sysview *v = &a->view[sys];
	char path[LIB_PATH * 3];
	SDL_Surface *raw, *conv = NULL, *fit = NULL;
	unsigned char hdr[8] = { 'D', 'T', 'O', 'V',
	                         TORTOS_SCREEN_W & 255, TORTOS_SCREEN_W >> 8,
	                         TORTOS_SCREEN_H & 255, TORTOS_SCREEN_H >> 8 };
	bool ok = false;
	FILE *f;
	int y;

	if (!v->overlay[0] || !overlay_fits(a, sys, v->dmode, v->overlay)) return false;
	overlay_dir(a, sys, path, sizeof path);
	snprintf(path + strlen(path), sizeof path - strlen(path), "/%s", v->overlay);
	if (!(raw = IMG_Load(path))) {
		fprintf(stderr, "overlay: cannot load %s: %s\n", path, IMG_GetError());
		return false;
	}
	conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!conv) return false;
	if (conv->w == TORTOS_SCREEN_W && conv->h == TORTOS_SCREEN_H) {
		fit = conv;
		conv = NULL;
	} else {
		fit = SDL_CreateRGBSurfaceWithFormat(0, TORTOS_SCREEN_W, TORTOS_SCREEN_H, 32,
		                                     SDL_PIXELFORMAT_ARGB8888);
		if (fit) {
			SDL_SetSurfaceBlendMode(conv, SDL_BLENDMODE_NONE);
			SDL_BlitScaled(conv, NULL, fit, NULL);
			fprintf(stderr, "overlay: %s is %dx%d, scaled to %dx%d\n", v->overlay,
			        conv->w, conv->h, TORTOS_SCREEN_W, TORTOS_SCREEN_H);
		}
	}
	if (fit && (f = fopen(out, "wb"))) {
		ok = fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr;
		SDL_LockSurface(fit);
		/* ARGB8888 in a little-endian word is B, G, R, A in memory, which is
		 * the order DTOV wants. */
		for (y = 0; ok && y < fit->h; y++)
			ok = fwrite((const uint8_t *)fit->pixels + (size_t)y * fit->pitch, 4,
			            (size_t)fit->w, f) == (size_t)fit->w;
		SDL_UnlockSurface(fit);
		if (fclose(f) != 0) ok = false;
	}
	if (conv) SDL_FreeSurface(conv);
	if (fit) SDL_FreeSurface(fit);
	return ok;
}

#define LOOK_CFG_PATH  "/tmp/chaos-shader.cfg"
#define LOOK_DTOV_PATH "/tmp/chaos-overlay.dtov"
#define LOOK_SHADED_PATH "/tmp/chaos-shaded.bmp"

/* What Diatom is told about a console's look: the shader file, or "" for no
 * shader, and the overlay file, or "" for none. */
static void look_files(const app *a, int sys, const char **cfg, const char **dtov)
{
	look_console c = look_console_of(a->sys.systems[sys].tag);

	*cfg = c != LOOK_NONE && look_write_cfg(LOOK_CFG_PATH, c, &a->view[sys].look)
	     ? LOOK_CFG_PATH : "";
	*dtov = overlay_write_dtov(a, sys, LOOK_DTOV_PATH) ? LOOK_DTOV_PATH : "";
}

/* ---------- per-system sort order ----------------------------------------- */

/* Keyed on the tag for the same reason display.<TAG> is, and stored beside it.
 * Loading does NOT re-sort: it runs before the scan on a rescan and after it
 * at startup, and the shelf is put in order by sort_all once both are done.
 *
 * Muse's shelf has orders of its own, by artist and by album, stored by their
 * own names under the same key - sort.MUSE. */
static void sort_load(app *a)
{
	char key[CFG_STR + 16], name[CFG_STR];
	int i;

	for (i = 0; i < a->sys.count; i++) {
		snprintf(key, sizeof key, "sort.%s", a->sys.systems[i].tag);
		if (!db_get_str(db_dev(), key, name, sizeof name, NULL)) continue;
		a->view[i].sort = is_muse(&a->sys.systems[i]) ? (int)ml_order_index(name)
		                                              : sort_index(name);
	}
}

static void sort_save(app *a)
{
	char key[CFG_STR + 16];
	int i;

	for (i = 0; i < a->sys.count; i++) {
		snprintf(key, sizeof key, "sort.%s", a->sys.systems[i].tag);
		db_set_str(db_dev(), key,
		           is_muse(&a->sys.systems[i]) ? ml_order_name((ml_order)a->view[i].sort)
		                                       : SORTS[a->view[i].sort].name);
	}
}

/* Put every shelf in its own order. One pass over the systems, and the
 * session rows are folded once per system that needs them rather than once
 * per game - which is why this is a loop here and not a call inside scan_all.
 *
 * The favorites shelf is built elsewhere and keeps its own order: it is a
 * shelf of games from many systems and has no single tag to hang a setting
 * off. */
/* One shelf in its own order.
 *
 * Favorites through sort_apply_owned: its games are several systems', and each
 * one's owner has to come with it and its play time has to be read under its
 * own system's tag. This ran the plain sort on Favorites as well, which only
 * held because Favorites was always sorted by name, the order it is built in -
 * any other order would have launched some favorites under another system's
 * core.
 *
 * Muse by its own orders, in muse_order_view: its cards are albums, and which
 * album each card is has to move with it. */
static void sort_shelf(app *a, int sys)
{
	sysview *v = &a->view[sys];

	if (is_muse(&a->sys.systems[sys])) { muse_order_view(v); return; }
	if (v->owner) {
		const char *tags[CFG_MAX_SYSTEMS];
		int i;

		for (i = 0; i < a->sys.count && i < CFG_MAX_SYSTEMS; i++)
			tags[i] = a->sys.systems[i].tag;
		sort_apply_owned(v->list.items, v->owner, v->list.count, v->sort, tags);
		return;
	}
	/* ChaOS: PortMaster's card stays first on the Ports shelf */
	if (v->list.count > 1 && pm_is_entry(v->list.items[0].file)) {
		sort_apply(v->list.items + 1, v->list.count - 1, v->sort, a->sys.systems[sys].tag);
		return;
	}
	sort_apply(v->list.items, v->list.count, v->sort, a->sys.systems[sys].tag);
}

static void sort_all(app *a)
{
	int i;

	for (i = 0; i < a->sys.count; i++) sort_shelf(a, i);
}

/* ---------- textures ----------------------------------------------------- */

/* Every card texture ends up with an alpha channel, whatever the file had.
 *
 * IMG_LoadTexture keeps the source format, so art saved as PNG color type 2 -
 * RGB, no alpha - becomes an RGB texture. Drawn straight to the screen that is
 * fine, which is why nothing was ever wrong with the shelves. Drawn through
 * SDL_RenderGeometry into an RGBA render target it is wrong on this driver:
 * cards came out missing entirely on a cube face, and system art came out
 * garbled. Measured 2026-09-07 against Toy Story and Uncharted Waters, which
 * carry alpha and drew correctly, and Dr. Franken and Donkey Kong Land III,
 * which do not and did not.
 *
 * Converted here rather than at the call sites: there is one loader, the cost
 * is one format conversion per card at load time, and a texture whose behavior
 * depends on what a scraper happened to save is a trap for whatever gets drawn
 * next. */
/* How far down the surface its opaque pixels reach, as a fraction of height.
 *
 * Art here is squared and centered, so a console photographed low in its frame
 * carries a band of transparency underneath - 102 of 384 rows for the Genesis,
 * against 10 for a Game Boy. Reflecting from the card's edge mirrors that
 * emptiness too, which is why the same reflection setting produced a flush
 * reflection on one card and a gap of about 196px on another.
 *
 * Scanned bottom-up and stopping at the first row with anything in it, so the
 * cost is the padding rather than the image. Alpha over 8 rather than over 0:
 * a PNG's fully transparent region is not always exactly zero. */
static float content_bottom(SDL_Surface *s)
{
	int y, x;

	if (!s || s->h <= 0 || s->format->BytesPerPixel != 4) return 1.0f;
	for (y = s->h - 1; y >= 0; y--) {
		const Uint32 *row = (const Uint32 *)((Uint8 *)s->pixels + y * s->pitch);

		for (x = 0; x < s->w; x++)
			if ((row[x] >> 24) > 8)
				return (float)(y + 1) / (float)s->h;
	}
	return 1.0f;
}

static SDL_Texture *load_image(SDL_Renderer *r, const char *path, int *w, int *h,
                               float *cb)
{
	SDL_Surface *raw = IMG_Load(path), *conv;
	SDL_Texture *t;

	if (cb) *cb = 1.0f;
	if (!raw) return NULL;
	conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!conv) return NULL;
	if (cb) *cb = content_bottom(conv);
	t = SDL_CreateTextureFromSurface(r, conv);
	SDL_FreeSurface(conv);
	if (t) SDL_QueryTexture(t, NULL, NULL, w, h);
	return t;
}

/* Which set is showing. Read once at boot and kept here, because sys_get_tex
 * runs per card per frame and the shelf should not ask the database how to
 * draw itself. */
static int g_cards;
static int g_dir;

/* The ChaOS pixel themes' Transition Speed, one per UI Direction (CARD_DIRS), kept
 * in the device database as pixnames.speed.<direction> so updates leave it.
 * form: a name assembling, or scattering - 0 for Static, where nothing moves;
 * hold: how long a name stays over a game when a system is turned to. */
static const struct { const char *id, *name; int form, hold; } PN_SPEEDS[] = {
	{ "slow",   "Slow",   1400, 1600 },
	{ "normal", "Normal",  950, 1100 },
	{ "fast",   "Fast",    450,  500 },
	{ "static", "Static",    0, 1100 },
};
#define PN_SPEED_COUNT 4
static int g_pn_speed[8];

/* ChaOS: Game List View - Normal, the shelf as the UI Theme and Direction
 * draw it, or List, a system's games as a menu: up and down, left and right
 * by letter, L1 and R1 by a screenful as on the shelf. "game_view" in the
 * device database. Cubic never opens a system, so it has no list. */
static bool g_list_view;
/* The menu's Recently Played switch, from the device database. On by default. */
static bool g_recent_shelf = true;
/* ChaOS: Appearance's other two shelf switches, Favorites and Muse, on by
 * default. Off, Favorites is still kept (Y still marks a game) and Muse is
 * still on SELECT: its shelf is put on the row while SELECT has it open, and
 * taken off again after (muse_open). */
static bool g_fav_shelf = true;
static bool g_muse_shelf = true;
static bool g_muse_attach;          /* SELECT has Muse open with its shelf off */

/* ChaOS: the status bar, the clock top left and the battery top right, each set
 * in Date & Time. Both are small and out of the way by default. */
/* ChaOS: the Appearance page is open. Read by the row list (sys_menu.c) and the
 * key mapping alike, so the page's rows and the keys that act on them agree. */
static bool g_appearance_page;
static void appearance_screen(app *a);

static bool g_status_clock = true;
static bool g_status_clock24;             /* false: 12-hour */
enum { STATUS_BATT_OFF, STATUS_BATT_ICON, STATUS_BATT_PCT, STATUS_BATT_BOTH, STATUS_BATT_COUNT };
static int  g_status_batt = STATUS_BATT_ICON;
static const char *const STATUS_BATT_NAMES[STATUS_BATT_COUNT] = {
	"Off", "Icon", "Percent", "Icon and percent",
};
#define LIST_VIEW_ON() (g_list_view && !CARD_DIRS[g_dir].both)
/* ChaOS: a menu drawn to fill the screen, this far from its edges, for the
 * one draw it is set around (the game List); -1 for the usual */
static int g_menu_fill = -1;

/* ChaOS: the letter popup, with the letter jumps, further down */
static void letter_note(sysview *v);
static void letter_hold(sysview *v, bool held);
static void draw_letter_popup(app *a);
static int shelf_letter_jump(sysview *v, int dir);
static char shelf_initial(const char *s);
static void letter_show(char c);   /* ChaOS: the popup for a letter, any list */

static void pn_speed_load(void)
{
	int d, k;
	char key[64], v[16];

	for (d = 0; d < CARD_DIR_COUNT && d < 8; d++) {
		snprintf(key, sizeof key, "pixnames.speed.%s", CARD_DIRS[d].id);
		db_get_str(db_dev(), key, v, sizeof v, "normal");
		g_pn_speed[d] = 1;
		for (k = 0; k < PN_SPEED_COUNT; k++)
			if (!strcmp(v, PN_SPEEDS[k].id)) g_pn_speed[d] = k;
	}
}

/* Muse: what the Music folder holds, read at boot and on every rescan, and
 * the green its card is drawn in - the iPod's own, sampled from the photograph
 * (tools/genmusecard.py draws both cards from the same value).
 *
 * RECOGNIZED BY ITS TAG, not by what it lacks. Favorites is the shelf with no
 * core and no folder, and the system menu tells them apart that way; Muse has
 * neither either, but its music is not a ROM folder at all, so an empty folder
 * is the honest value and the tag is what says which of the two it is. */
#define MUSE_ACCENT 0x9CD345u
static ml_lib g_muse;
/* Where the scan read it from: the card, which holds Music and Audiobooks. */
static char   g_muse_root[CFG_STR * 2];
/* Bumped by every build of the library, so a Muse screen can tell that MENU's
 * Rescan Folder replaced it while its menu was open - see muse_menu. */
static unsigned g_muse_gen;

static bool is_muse(const system_cfg *s)
{
	return s && !strcmp(s->tag, "MUSE");
}

/* What is known about each album's cover: one per album in g_muse, rebuilt
 * with it. ASKED carries when, because an answer can be lost with the daemon,
 * and an album is then asked about again rather than waited on for good. */
enum { COV_UNKNOWN, COV_ASKED, COV_JPG, COV_PNG, COV_FOLDER, COV_NONE };
typedef struct { unsigned char st; unsigned asked_ms; } cover_state;
static cover_state *g_cov;

/* ---- Muse: books ------------------------------------------------------------ */

/* Which kind Muse's shelf shows, music or books. Eric's, 2026-09-27: one Muse
 * card and a Show row in its menu, rather than a card for books beside it -
 * two ways into one player read as two players. Kept across restarts. */
static bool g_muse_books;

/* Whether each album is a book listened to the end: one per album in g_muse,
 * rebuilt with it, like g_cov. A finished book is marked and not forgotten,
 * and A on it starts it over. Eric's, 2026-09-27. */
static bool *g_book_done;

/* The kind the shelf really shows: the one chosen, unless there is none of it
 * and some of the other. */
static bool muse_books_shown(void)
{
	int books = ml_count(&g_muse, true), music = ml_count(&g_muse, false);

	if (!books) return false;
	if (!music) return true;
	return g_muse_books;
}

/* Where book `al`'s place is kept: the settings, under its folder. */
static void book_key(int al, char *out, size_t n)
{
	const char *p = g_muse.tracks[g_muse.albums[al].first].path;
	const char *slash = strrchr(p, '/');

	snprintf(out, n, "book.%.*s", slash ? (int)(slash - p) : 0, p);
}

/* A place is "<seconds>\t<file>", the file by its name rather than its number
 * so a file added to the folder later moves nothing, or "finished". */
static void book_write(int al, const char *track, double at)
{
	char key[LIB_PATH + 8], val[LIB_PATH + 32];
	const char *file = strrchr(track, '/');

	book_key(al, key, sizeof key);
	snprintf(val, sizeof val, "%.1f\t%s", at, file ? file + 1 : track);
	db_set_str(db_dev(), key, val);
	g_book_done[al] = false;
}

static void book_finish(int al)
{
	char key[LIB_PATH + 8];

	book_key(al, key, sizeof key);
	db_set_str(db_dev(), key, "finished");
	g_book_done[al] = true;
	fprintf(stderr, "muse: finished %s\n", g_muse.albums[al].name);
}

/* Where book `al` starts: its file, and `at` seconds into it. The beginning
 * for a book never started, or finished. */
static int book_place(int al, double *at)
{
	const ml_album *b = &g_muse.albums[al];
	char key[LIB_PATH + 8], val[LIB_PATH + 32];
	const char *tab;
	int i;

	*at = 0;
	book_key(al, key, sizeof key);
	db_get_str(db_dev(), key, val, sizeof val, "");
	if (!(tab = strchr(val, '\t'))) return 0;
	for (i = 0; i < b->n; i++) {
		const char *p = g_muse.tracks[b->first + i].path;
		const char *f = strrchr(p, '/');

		if (!strcmp(f ? f + 1 : p, tab + 1)) { *at = atof(val); return i; }
	}
	return 0;
}

/* KEEPING A BOOK'S PLACE, wherever Muse is polled: on its screens, over the
 * shelf and the menus, and through a game, which is where a book is likely to
 * be heard.
 *
 * Written when the file changes, when it pauses or stops, and every thirty
 * seconds of listening in between, so a Brick that loses its battery goes back
 * half a minute at most. `now` writes whatever there is, for the power button
 * and for a new queue about to replace this one. */
static struct { int al, index; mu_state st; double at; } g_bk = { -1, -1, MU_OFF, 0 };

static void book_keep(bool now)
{
	const mu_now *mn = musec_now();
	const char *track;
	bool ran_out = musec_take_ran_out();
	int al;

	if (!musec_is_book() || !g_book_done) { g_bk.al = -1; return; }
	track = musec_track(mn->index);
	al = ml_album_of(&g_muse, track);
	if (al < 0 || !g_muse.albums[al].book) { g_bk.al = -1; return; }
	if (ran_out) { book_finish(al); g_bk.al = -1; return; }
	if (mn->state != MU_PLAYING && mn->state != MU_PAUSED) return;
	if (now || al != g_bk.al || mn->index != g_bk.index || mn->state != g_bk.st ||
	    fabs(mn->at - g_bk.at) >= 30) {
		book_write(al, track, mn->at);
		g_bk.al = al;
		g_bk.index = mn->index;
		g_bk.st = mn->state;
		g_bk.at = mn->at;
	}
}

/* Muse, polled: what the daemon said, and a book's place kept. Everything that
 * polls Muse comes through here. */
static void muse_poll(void)
{
	musec_poll();
	book_keep(false);
}

/* A book's speed, kept beside its place: the last one chosen on it, 1x for a
 * book never changed. Its own key rather than a third field in the place, so
 * a place written before speeds existed still reads. */
static void book_speed_key(int al, char *out, size_t n)
{
	const char *p = g_muse.tracks[g_muse.albums[al].first].path;
	const char *slash = strrchr(p, '/');

	snprintf(out, n, "bookspeed.%.*s", slash ? (int)(slash - p) : 0, p);
}

static double book_speed(int al)
{
	char key[LIB_PATH + 16], val[16];
	double x;

	book_speed_key(al, key, sizeof key);
	db_get_str(db_dev(), key, val, sizeof val, "1");
	x = atof(val);
	return x >= 0.5 && x <= 2.0 ? x : 1.0;
}

/* Y on a book's Now Playing: the next speed, 1x to 2x in quarters and then
 * 0.75x before coming round - the speeds a listener reaches for, in the order
 * they reach for them. Set on the book as it plays and kept for it. */
static void book_cycle_speed(void)
{
	static const double STEPS[] = { 1.0, 1.25, 1.5, 1.75, 2.0, 0.75 };
	int al = ml_album_of(&g_muse, musec_track(musec_now()->index));
	double now = musec_speed(), next = STEPS[0];
	char key[LIB_PATH + 16], val[16];
	size_t k;

	for (k = 0; k < sizeof STEPS / sizeof STEPS[0]; k++)
		if (fabs(STEPS[k] - now) < 0.01) {
			next = STEPS[(k + 1) % (sizeof STEPS / sizeof STEPS[0])];
			break;
		}
	musec_set_speed(next);
	if (al < 0 || !g_muse.albums[al].book) return;
	book_speed_key(al, key, sizeof key);
	snprintf(val, sizeof val, "%.2f", next);
	db_set_str(db_dev(), key, val);
}

/* A new queue. Whatever book it replaces has its place written first, since
 * the queue is the only record of where that was. */
static void muse_play(const char *const *paths, int n, int start, double at,
                      bool book, double speed, const char *artist, const char *album)
{
	book_keep(true);
	g_bk.al = -1;
	musec_play(paths, n, start, at, book, speed, artist, album);
}

/* The play modes as the player meets them: the name saved in the settings and
 * the mark. In order has no mark: it is what plays when nothing has been asked
 * for, and a mark meaning "nothing special" is noise.
 *
 * The name each mode was SHOWN by went with the notice that showed it - see
 * muse_cycle_mode. Nothing spells a mode out now; the mark beside the track
 * count on Now Playing is the whole of it. */
static const struct { const char *key; int glyph; } MUSE_MODES[MUQ_MODES] = {
	[MUQ_IN_ORDER]   = { "in order",   -1 },
	[MUQ_REPEAT_ALL] = { "repeat all", UI_GLYPH_REPEAT },
	[MUQ_REPEAT_ONE] = { "repeat one", UI_GLYPH_REPEAT_ONE },
	[MUQ_SHUFFLE]    = { "shuffle",    UI_GLYPH_SHUFFLE },
};

/* The cover on the Now Playing screen, kept between visits so that opening it
 * is not a decode: the album it is for and its texture. `done` means this is
 * what the album will show - its art, or the card made for one with none - and
 * nothing more needs asking. */
static struct {
	int          album;
	bool         done;
	SDL_Texture *tex;
} g_np = { .album = -1 };

/* The two faces a turn needs. Kept rather than made per frame: they are a
 * screen each, and a cube turns for a fifth of a second at a time. */
static SDL_Texture *g_face[2];

static SDL_Texture *face_tex(app *a, int i)
{
	if (!g_face[i]) {
		g_face[i] = SDL_CreateTexture(a->r, SDL_PIXELFORMAT_RGBA8888,
		                              SDL_TEXTUREACCESS_TARGET,
		                              TORTOS_SCREEN_W, TORTOS_SCREEN_H);
		if (!g_face[i])
			fprintf(stderr, "cube: face %d not created: %s\n", i, SDL_GetError());
	}
	return g_face[i];
}

/* A system card's file in the chosen set, and the default set's to fall back
 * on. A set may be incomplete and still be worth showing. Favorites is the
 * standing example: it is a shelf, not a console, so a set of hardware
 * photographs has nothing to put there and borrows the default's card rather
 * than falling all the way through to the generated one. */
static bool sys_card_paths(const app *a, int i, char *set, char *def, size_t n)
{
	snprintf(set, n, "%s/cards/%s/%s", P_ROOT, CARD_SETS[g_cards].dir,
	         a->sys.systems[i].card);
	snprintf(def, n, "%s/cards/%s/%s", P_ROOT, CARDS_DEFAULT, a->sys.systems[i].card);
	return g_cards != 0;          /* whether the default is a different file */
}

/* The generated card, for a system with no file in either set. Needs the
 * renderer, so it is made on the main thread. */
static void sys_card_generated(app *a, int i)
{
	a->sys_tex[i] = ui_make_card(a->r, a->sys.systems[i].name, a->sys.systems[i].accent,
	                             &a->sys_w[i], &a->sys_h[i]);
}

/* On the frame, as every system card was until 2026-10-02. Now only when
 * there is no loader thread, as for --shot. */
static void sys_card_load_now(app *a, int i)
{
	char set[CFG_STR * 2], def[CFG_STR * 2];
	bool other = sys_card_paths(a, i, set, def, sizeof set);

	a->sys_tex[i] = load_image(a->r, set, &a->sys_w[i], &a->sys_h[i], &a->sys_cb[i]);
	if (!a->sys_tex[i] && other)
		a->sys_tex[i] = load_image(a->r, def, &a->sys_w[i], &a->sys_h[i], &a->sys_cb[i]);
	if (!a->sys_tex[i]) sys_card_generated(a, i);
}

/* System cards come from the loader threads like covers, under a shelf number
 * no real shelf has (texload uses -1 for an idle worker). All thirteen used to
 * be decoded on the main thread before the first frame - 640x820 PNGs, about
 * 370 ms of the GKD Pixel 2's boot, measured 2026-10-02 - whether or not the
 * system shelf was what came up. */
#define SYS_CARDS_SHELF (-2)

static SDL_Texture *sys_get_tex(void *ctx, int i, int *w, int *h, float *cb)
{
	app *a = ctx;

	/* ChaOS Pixel Cards: drawn, not loaded, and drawn again while its pixels
	 * move - toward its name under the cursor, away from it elsewhere */
	if (CARDS_DRAWN(g_cards)) {
		pn_speed(PN_SPEEDS[g_pn_speed[g_dir]].form);
		a->sys_tex[i] = pn_card(a->r, a->sys_tex[i], a->sys.systems[i].name,
		                        a->sys.systems[i].accent, i == a->sys_cursor);
		*w = a->sys_tex[i] ? PN_W : 0;
		*h = a->sys_tex[i] ? PN_H : 0;
		a->sys_w[i] = *w;
		a->sys_h[i] = *h;
		a->sys_cb[i] = 1.0f;
		if (cb) *cb = 1.0f;
		return a->sys_tex[i];
	}
	if (!a->sys_tex[i]) {
		char set[CFG_STR * 2], def[CFG_STR * 2];
		bool other = sys_card_paths(a, i, set, def, sizeof set);

		/* Ask and draw nothing this frame; cf_draw skips a card with no
		 * texture. No worker means doing it here, as before. */
		if (!texload_want(SYS_CARDS_SHELF, i, set, other ? def : NULL))
			sys_card_load_now(a, i);
	}
	if (!a->sys_tex[i]) {
		*w = *h = 0;
		if (cb) *cb = 1.0f;
		return NULL;
	}
	*w = a->sys_w[i];
	*h = a->sys_h[i];
	if (cb) *cb = a->sys_cb[i];
	return a->sys_tex[i];
}

/* Where a game's autosave preview lives: the frame the player was looking at
 * when they stopped. The directory is named after the ROM's folder under
 * Roms/.
 *
 * The directory and the slot names are TortOS's own. Both were briefly
 * borrowed from the emulator TortOS replaced, and the borrowing cost more than
 * it saved: a name that describes another project invites a reader to treat
 * the contents as dead, and on 2026-08-28 the live autosave tree was deleted
 * by someone auditing the card for exactly that reason. Nothing was lost only
 * because states were being cleared for testing the same hour.
 *
 * Renamed with no migration path, deliberately - nothing has shipped, so there
 * are no cards in the world to be kind to. */
/* Six manual save slots and one autosave.
 *
 * Six because it is enough and it fits the carousel's dot rail without
 * crowding, not because any particular number is correct. An earlier eight was
 * copied from another launcher and had no reasoning of its own behind it.
 *
 * The resume slot is named rather than numbered on disk: `<game>.auto.state`
 * explains itself, where a number needs a reader to already know which slot
 * means auto. Its value is only an internal sentinel and must not collide with
 * 1..GM_SLOTS, so it is derived rather than picked. Both live in game_menu.h
 * now, with the carousel mapping beside them, so a check can reach all three.
 */

static const char *slot_name(int slot)
{
	static char buf[8];
	if (slot == SLOT_AUTO) return "auto";
	snprintf(buf, sizeof buf, "%d", slot);
	return buf;
}

static void preview_path(app *a, int s, const game_entry *g, char *out, size_t n)
{
	const char *base = strrchr(g->file, '/');
	base = base ? base + 1 : g->file;
	snprintf(out, n, "%s/.tortos/%s/%s.%s.bmp",
	         P_SHARED, a->sys.systems[s].folder, base, slot_name(SLOT_AUTO));
}

/* Where Diatom writes the frame it hands over, which is NOT the Auto card's
 * picture.
 *
 * Diatom writes its preview twice: before PAUSED, so the launcher has a frame
 * to dim and draw the menu over, and again at exit beside the state. Pointing
 * both at <rom>.auto.bmp meant every menu open overwrote the Auto card with
 * the current moment, while the card's timestamp still came from the state -
 * so the card contradicted itself and the picture was the convincing half.
 * Reported 2026-09-07: died in Contra, opened the menu, and the Auto card
 * showed the death while loading it went back two levels.
 *
 * One scratch file for the whole device rather than one per game: nothing
 * reads it after the launch that wrote it, and a per-game copy would leave
 * litter beside every save. */
static void pause_preview_path(char *out, size_t n)
{
	snprintf(out, n, "%s/pause.bmp", P_USERDATA);
}

/* The Diatom transport takes explicit paths rather than a slot number, so the
 * state lives beside the preview it belongs to, named the same way.
 *
 * The autosave is named `auto`, not a number. Slots 1..GM_SLOTS are the
 * player's own,
 * reachable from the in-game menu, and are genuinely numbered; the resume slot
 * is not one of them and calling it 9 only meant something to someone who knew
 * which emulator picked that number. Each slot is a state and a preview named
 * alike, so a slot that has a picture has a game behind it. */
static void slot_state_path(app *a, int s, const game_entry *g, int slot,
                            char *out, size_t n)
{
	const char *base = strrchr(g->file, '/');
	base = base ? base + 1 : g->file;
	snprintf(out, n, "%s/.tortos/%s/%s.%s.state",
	         P_SHARED, a->sys.systems[s].folder, base, slot_name(slot));
}

static void slot_preview_path(app *a, int s, const game_entry *g, int slot,
                              char *out, size_t n)
{
	const char *base = strrchr(g->file, '/');
	base = base ? base + 1 : g->file;
	snprintf(out, n, "%s/.tortos/%s/%s.%s.bmp",
	         P_SHARED, a->sys.systems[s].folder, base, slot_name(slot));
}

static void state_path(app *a, int s, const game_entry *g, char *out, size_t n)
{
	slot_state_path(a, s, g, SLOT_AUTO, out, n);
}

/* The launcher owns the paths, so the launcher makes the directories - the
 * emulator writes where it is told and fails where it cannot. A missing mkdir
 * here once cost every preview on the card, silently, because writing to a
 * path in a directory that does not exist fails per-write and looks like
 * nothing happening. */
/* Where the two databases live, and the directories they need. Both callers
 * are in main(): --dump, which runs before video, and the boot path. Having
 * one of them create the directory and the other not is how --dump failed on a
 * fresh tree the first time it was tried. */
static bool db_paths_ready(char *dev, size_t ndev, char *lib, size_t nlib,
                           char *env, size_t nenv)
{
	char d[CFG_STR * 2];

	mkdir(P_USERDATA, 0755);
	mkdir(P_SHARED, 0755);
	snprintf(d, sizeof d, "%s/.tortos", P_SHARED);
	mkdir(d, 0755);

	snprintf(dev, ndev, "%s/tortos.db", P_USERDATA);
	snprintf(lib, nlib, "%s/.tortos/library.db", P_SHARED);
	if (env) snprintf(env, nenv, "%s/boot.env", P_USERDATA);
	return true;
}

static void persist_dir_ensure(app *a, int s)
{
	char d[LIB_PATH * 2];
	snprintf(d, sizeof d, "%s/.tortos", P_SHARED);
	mkdir(d, 0755);
	snprintf(d, sizeof d, "%s/.tortos/%s", P_SHARED, a->sys.systems[s].folder);
	mkdir(d, 0755);
}

/* Card art, in order of preference: the box art the user put in .media/, then
 * the autosave preview -- the last frame they saw, which for a game in
 * progress is a better card than any box -- then a generated slab. */
/* The system an entry on a shelf belongs to. Real shelves answer with
 * themselves; Favorites answers per entry. */
static int shelf_owner(app *a, int sysidx, int item)
{
	sysview *v = &a->view[sysidx];
	if (v->owner && item >= 0 && item < v->list.count) return v->owner[item];
	return sysidx;
}

/* The view whose SYSTEM settings the game under the cursor belongs to.
 *
 * Favorites is a shelf of other systems' games, so the two kinds of setting
 * have to be told apart: a property of the SYSTEM - the core, the ROM folder,
 * the BIOS, the display mode - resolves through the game's owner, while a
 * property of the SHELF - its sort order, its cursor, its own card - belongs
 * to the shelf you are standing on.
 *
 * launch() had that right for three of the four and wrong for the display
 * mode, which it read off the shelf: a Genesis game set to integer on its own
 * shelf played stretched when it was started from Favorites, because the mode
 * it used was the one filed under FAV. Nothing showed it on this card, where
 * every system reads stretch and all the answers agree. Named as a function
 * rather than repeated at each site so the next per-system setting has one
 * obvious thing to call. */

/* Where a game's box art lives.
 *
 * One definition because four places need the same file - the card, the info
 * screen, the replace action, and the return from a game - and the last of
 * those has to test exactly the file the card loads. Four hand-written copies
 * of the format string only have to drift once for it to ask about one file
 * and draw another. */
static void box_art_path(const char *folder, const char *name, char *out, size_t n)
{
	snprintf(out, n, "%s/%s/.media/%s.png", P_ROMS, folder, name);
}

/* Whether it is there. An empty file is not art - the rule the info screen
 * already used, now the rule for everyone. */
static bool has_box_art(const char *folder, const char *name)
{
	char p[LIB_PATH * 3];
	struct stat st;

	box_art_path(folder, name, p, sizeof p);
	return stat(p, &st) == 0 && st.st_size > 0;
}

/* Muse's half of the card machinery, defined with the rest of Muse. */
static void muse_card_want(app *a, int s, int i);
static void cover_shape(SDL_Surface **s);

static SDL_Texture *game_get_tex(void *ctx, int i, int *w, int *h, float *cb)
{
	app *a = ctx;
	int s = a->sys_cursor;
	sysview *v = &a->view[s];
	/* The game's own system, not the shelf's: on Favorites those differ, and
	 * every one of these three lookups would otherwise go to the wrong
	 * folder, the wrong state directory and the wrong accent. */
	int o = shelf_owner(a, s, i);

	if (!v->tex[i] && is_muse(&a->sys.systems[s])) {
		muse_card_want(a, s, i);
	} else if (!v->tex[i]) {
		char art[LIB_PATH * 3], prev[LIB_PATH * 3];

		box_art_path(a->sys.systems[o].folder, v->list.items[i].name,
		             art, sizeof art);
		preview_path(a, o, &v->list.items[i], prev, sizeof prev);
		/* Hand it to the worker and draw nothing this frame. Asking is cheap
		 * and ignores a card already in hand, so callers ask every frame.
		 * With the title and the accent, so that a game with neither file
		 * gets its generated card drawn there too. */
		if (!texload_want_card(s, i, art, prev, v->list.items[i].title,
		                       a->sys.systems[o].accent, false)) {
			/* No worker. Exactly what this did before, on the frame. */
			v->tex[i] = load_image(a->r, art, &v->tw[i], &v->th[i], &v->cb[i]);
			if (!v->tex[i])
				v->tex[i] = load_image(a->r, prev, &v->tw[i], &v->th[i], &v->cb[i]);
			if (!v->tex[i])
				v->tex[i] = ui_make_card(a->r, v->list.items[i].title,
				                         a->sys.systems[o].accent,
				                         &v->tw[i], &v->th[i]);
		}
	}
	if (!v->tex[i]) {
		/* Still coming. cf_draw skips a card with no texture, so the slot is
		 * simply empty for a few frames instead of the shelf stopping. */
		*w = *h = 0;
		if (cb) *cb = 1.0f;
		return NULL;
	}
	*w = v->tw[i];
	*h = v->th[i];
	if (cb) *cb = v->cb[i];
	return v->tex[i];
}

static void faces_stale(void);

/* How long one frame spends turning finished art into textures.
 *
 * It used to take everything waiting. A cover trickles in as each decode
 * finishes, so that was rarely more than one or two, but a shelf of games with
 * no art came back all at once - nothing to read, so nothing to wait for - and
 * a frame took them all: 2 to 6 generated cards, 40-160ms, on the Pixel 2 on
 * 2026-10-28. With the cards drawn on the workers that is an upload each,
 * about 8ms for a full card there, still too many for one frame. So one at
 * least, then as many as fit in this; the rest stay in the result ring,
 * texload_ready keeps the loop drawing, and they land on the next frames. */
#define DRAIN_BUDGET_MS 6

/* Install what the workers finished, on the thread that owns the renderer,
 * to DRAIN_BUDGET_MS a frame. SDL_CreateTextureFromSurface is the only part
 * that needs it; content_bottom is computed here too, because it stops at the
 * first row with anything in it and so costs the padding, not the image.
 *
 * And the cube's two cached faces go stale when anything lands. They are
 * rendered once per step of a turn and kept for its whole 450ms, so a card
 * that arrived mid-turn stayed blank on its face until the turn ended. Staling
 * them costs a render of both faces on a frame where art arrived - a handful
 * per turn, not the render every frame the cache exists to avoid. */
static void texload_drain(app *a)
{
	SDL_Surface *surf;
	int s, i;
	bool landed = false, generated;
	const Uint64 start = SDL_GetPerformanceCounter();
	const Uint64 budget = SDL_GetPerformanceFrequency() * DRAIN_BUDGET_MS / 1000;

	while ((!landed || SDL_GetPerformanceCounter() - start < budget) &&
	       texload_take(&s, &i, &surf, &generated)) {
		sysview *v;

		if (s == SYS_CARDS_SHELF) {
			/* a card asked for before the theme became a drawn one */
			if (i >= 0 && i < a->sys.count && !a->sys_tex[i] && !CARDS_DRAWN(g_cards)) {
				if (surf) {
					a->sys_cb[i] = content_bottom(surf);
					a->sys_tex[i] = SDL_CreateTextureFromSurface(a->r, surf);
					if (a->sys_tex[i])
						SDL_QueryTexture(a->sys_tex[i], NULL, NULL,
						                 &a->sys_w[i], &a->sys_h[i]);
				}
				if (!a->sys_tex[i]) sys_card_generated(a, i);
				landed = true;
			}
			if (surf) SDL_FreeSurface(surf);
			continue;
		}
		if (s < 0 || s >= a->sys.count) { if (surf) SDL_FreeSurface(surf); continue; }
		v = &a->view[s];
		/* The shelf may have moved on, or something else may have filled this
		 * slot while the decode was in the air. */
		if (i < 0 || i >= v->list.count || v->tex[i]) {
			if (surf) SDL_FreeSurface(surf);
			continue;
		}
		/* A card the worker drew is what ui_make_card would have made: no
		 * art to measure the foot of, and blended for its corners. */
		if (surf && generated) {
			v->tex[i] = SDL_CreateTextureFromSurface(a->r, surf);
			if (v->tex[i]) {
				SDL_SetTextureBlendMode(v->tex[i], SDL_BLENDMODE_BLEND);
				v->tw[i] = surf->w;
				v->th[i] = surf->h;
			}
			v->cb[i] = 1.0f;
			SDL_FreeSurface(surf);
			surf = NULL;
		}
		/* A cover is squared and given a card's corners, the way Now
		 * Playing draws it, so one album looks the same in both places. */
		if (surf && is_muse(&a->sys.systems[s])) cover_shape(&surf);
		if (surf) {
			v->cb[i] = content_bottom(surf);
			v->tex[i] = SDL_CreateTextureFromSurface(a->r, surf);
			if (v->tex[i])
				SDL_QueryTexture(v->tex[i], NULL, NULL, &v->tw[i], &v->th[i]);
			SDL_FreeSurface(surf);
		}
		if (!v->tex[i]) {
			/* Neither file was there and the worker drew no card - an album,
			 * which Muse asks for without one, or a game asked for before the
			 * workers' fonts were open - so the generated slab is made here. */
			int o = shelf_owner(a, s, i);
			v->tex[i] = is_muse(&a->sys.systems[s])
			          ? ui_make_cover(a->r, v->list.items[i].title,
			                          a->sys.systems[o].accent, &v->tw[i], &v->th[i])
			          : ui_make_card(a->r, v->list.items[i].title,
			                         a->sys.systems[o].accent,
			                         &v->tw[i], &v->th[i]);
			v->cb[i] = 1.0f;
		}
		landed = true;
	}
	if (landed) faces_stale();
}

static void evict_far(sysview *v, int keep)
{
	for (int i = 0; i < v->list.count; i++) {
		int d = abs(i - v->cursor);
		if (v->list.count >= CF_WINDOW)
			d = d > v->list.count / 2 ? v->list.count - d : d;
		if (v->tex[i] && d > keep) {
			SDL_DestroyTexture(v->tex[i]);
			v->tex[i] = NULL;
		}
	}
}

#define PRIME_HALF_MAX (CF_HALF_WINDOW + 2)

/* Ask for the cards on shelf `s` around `center`, out to `half` either side.
 *
 * FARTHEST FIRST. The workers take the newest request first (texload.c), so
 * they decode in the reverse of the order asked, and the center has to be asked
 * for last to be decoded first. Both callers used to ask for it first or in the
 * middle, on the reasoning that the order asked is the order decoded - true of
 * the queue they were written against, and backwards since it went newest-first.
 * Measured 2026-09-14, cold after a reboot: entering a system put the focused
 * card up 70-160ms after A, only 15-35ms ahead of the rest of its row, when it
 * should have been the first to arrive.
 *
 * Each card is asked for once. On a shelf shorter than the window, wrapping
 * offers the same card from both sides, and the second ask would be refused as
 * already queued - leaving the card wherever the first ask put it.
 *
 * game_get_tex reads a->sys_cursor rather than taking a shelf, so it is swapped
 * around the calls. */
static void prime_around(app *a, int s, int center, int half)
{
	sysview *v = &a->view[s];
	int n = v->list.count, idx[2 * PRIME_HALF_MAX + 1], m = 0;
	int k, j, save = a->sys_cursor;

	if (n <= 0) return;
	if (half > PRIME_HALF_MAX) half = PRIME_HALF_MAX;
	for (k = 0; k <= half; k++) {
		int side;
		for (side = 1; side >= (k ? -1 : 1); side -= 2) {
			int i = center + k * side;
			if (n >= 2) {
				i %= n;
				if (i < 0) i += n;
			} else if (i != 0) {
				continue;
			}
			for (j = 0; j < m && idx[j] != i; j++) ;
			if (j == m) idx[m++] = i;
		}
	}
	a->sys_cursor = s;
	while (m-- > 0) {
		int w, h;
		if (!v->tex[idx[m]]) game_get_tex(a, idx[m], &w, &h, NULL);
	}
	a->sys_cursor = save;
}

/* The visible row and two more either side, so the first scroll finds its
 * neighbors already there instead of sliding in blank. */
static void prime_window(app *a, int s)
{
	prime_around(a, s, a->view[s].cursor, PRIME_HALF_MAX);
}

/* Ask for the whole window around `center`.
 *
 * This used to take a budget and spend one card per frame, because asking
 * meant decoding and a card was 7-27ms against a 16.7ms frame. Since
 * texload.c, asking only queues: game_get_tex hands the worker two paths and
 * returns, a card already in hand is ignored, so the whole window costs about
 * nothing and dribbling it out a frame at a time only made it land later. */
static void prime_toward(app *a, int s, int center)
{
	prime_around(a, s, center, CF_HALF_WINDOW);
}

static void prime_sys_window(app *a)
{
	for (int i = 0; i < a->sys.count; i++) {
		int w, h;
		sys_get_tex(a, i, &w, &h, NULL);
	}
}

/* The system cards the shelf's row draws: the focused one and up to three
 * either side, wrapping round the ends as cf_draw does. `n`th nearest first,
 * so asking in this order decodes the middle of the screen first. Returns
 * false past the last. */
static bool sys_card_visible(const app *a, int n, int *item)
{
	int count = a->sys.count, half = count - 1 < CF_HALF_WINDOW ? count - 1 : CF_HALF_WINDOW;
	int k = (n + 1) / 2, side = n % 2 ? 1 : -1;

	if (count <= 0 || k > half) return false;
	*item = ((a->sys_cursor + side * k) % count + count) % count;
	return true;
}

/* At boot, before the display is up: the loader threads start on the cards
 * the first frame will show while video, input and settings are set up
 * (about 430 ms on the GKD Pixel 2), instead of after. */
static void sys_cards_ask_visible(app *a)
{
	char set[CFG_STR * 2], def[CFG_STR * 2];
	int n, i;

	if (CARDS_DRAWN(g_cards)) return;      /* nothing to load: drawn */
	for (n = 0; sys_card_visible(a, n, &i); n++) {
		bool other = sys_card_paths(a, i, set, def, sizeof set);
		texload_want(SYS_CARDS_SHELF, i, set, other ? def : NULL);
	}
}

/* And just before the first frame, install them as they finish, so it has
 * them all and none fills in after. Normally they are done by now; the cap
 * means a slow card read costs the boot at most `cap_ms`, and the rest fill
 * in as before. */
static void sys_cards_wait_visible(app *a, unsigned cap_ms)
{
	unsigned t0 = plat_now_ms();

	for (;;) {
		bool all = true;
		int n, i;

		/* Everything that is ready, not one budget's worth: there is no
		 * frame on screen yet to keep smooth, and a system card costs more
		 * than DRAIN_BUDGET_MS to upload, so a single drain installed one
		 * card a pass and the rest each waited out the 4ms below. */
		do texload_drain(a); while (texload_ready());
		for (n = 0; sys_card_visible(a, n, &i); n++)
			if (!a->sys_tex[i]) { all = false; break; }
		if (all) return;
		if (plat_now_ms() - t0 >= cap_ms) {
			fprintf(stderr, "boot: system cards not all in after %u ms; "
			                "the rest fill in\n", cap_ms);
			return;
		}
		SDL_Delay(4);
	}
}

/* The two faces only change when the turn crosses into the next item: through
 * a whole 450ms turn, i0 and i1 are fixed and only the angle moves. They were
 * being redrawn every frame regardless, which is about 27 full-screen renders
 * to offscreen targets per turn where one would do, and it cost half the
 * frame rate: 30-42fps while holding a direction, against the 60.2 this shelf
 * held before the cube existed. Measured 2026-09-07.
 *
 * Invalidated at rest rather than tracked in detail. Anything that changes
 * what a face shows - a favorite toggled, a theme changed, a different system
 * entered - happens while nothing is turning, so making the next turn redraw
 * is both correct and free. Guessing at a longer list of things to watch for
 * would be the same work done less reliably. */
static struct { int kind, sys, i0, i1; } g_face_key = { -1, -1, -1, -1 };

static void faces_stale(void)
{
	g_face_key.kind = -1;
}

/* What actually has to be redrawn. A step of one leaves one of the two faces
 * already rendered - stepping forward, the face that was arriving is the one
 * now being left - so it is a pointer swap and a single render rather than
 * two. After the chase clamp every step is exactly one, so this halves the
 * work on every frame that redraws at all.
 *
 * Compared by index rather than by direction, which makes a wrap around the
 * end of the shelf just another step of one. */
enum { FACES_BOTH, FACES_NONE, FACES_NEAR_ONLY, FACES_FAR_ONLY };

static int faces_plan(int kind, int sys, int i0, int i1)
{
	int plan = FACES_BOTH;

	if (g_face_key.kind == kind && g_face_key.sys == sys) {
		if (g_face_key.i0 == i0 && g_face_key.i1 == i1) plan = FACES_NONE;
		else if (g_face_key.i1 == i0)                   plan = FACES_FAR_ONLY;
		else if (g_face_key.i0 == i1)                   plan = FACES_NEAR_ONLY;
	}
	g_face_key.kind = kind; g_face_key.sys = sys;
	g_face_key.i0 = i0; g_face_key.i1 = i1;
	return plan;
}

/* The kept face is in the wrong slot after a step, so the two trade places. */
static void faces_swap(SDL_Texture **fa, SDL_Texture **fb)
{
	SDL_Texture *t = g_face[0];

	g_face[0] = g_face[1];
	g_face[1] = t;
	*fa = g_face[0];
	*fb = g_face[1];
}

/* One shelf's card art, dropped.
 *
 * Every one of these arrays is indexed by POSITION in the list, so reordering
 * the list moves the games and leaves the art behind - each card would wear
 * the picture of whatever used to sit where it now sits. The cube's two cached
 * faces are keyed by index as well, which is what faces_stale is for.
 *
 * Decodes in flight are keyed by position too, and dropping the textures does
 * not reach them. Without the bump, one asked for under the old order landed
 * after the reorder and was installed at its old slot - another game's cover
 * on the card - and then stuck, because a slot with a texture is never asked
 * for again. free_all_textures always bumped; this, its only other way of
 * dropping art, did not, and a Sort By change was the one path through it. */
static void free_view_textures(sysview *v)
{
	int k;

	texload_bump();
	for (k = 0; k < v->list.count; k++) {
		if (v->tex && v->tex[k]) {
			SDL_DestroyTexture(v->tex[k]);
			v->tex[k] = NULL;
		}
		if (v->tw) v->tw[k] = 0;
		if (v->th) v->th[k] = 0;
		if (v->cb) v->cb[k] = 0.0f;
	}
	faces_stale();
}

/* Muse's shelf onto the other kind: music or books, from the first card. The
 * textures go first, while the count is still the old kind's, since every card
 * is about to be a different album. */
static void muse_show(sysview *v, bool books)
{
	g_muse_books = books;
	db_set_str(db_dev(), "muse.show", books ? "books" : "music");
	free_view_textures(v);
	muse_order_view(v);
	v->cursor = 0;
	cf_reset(&v->cf, 0);
}

static void free_all_textures(app *a)
{
	/* Anything in flight was asked for against the shelf as it was. */
	texload_bump();
	for (int i = 0; i < 2; i++)
		if (g_face[i]) { SDL_DestroyTexture(g_face[i]); g_face[i] = NULL; }
	faces_stale();
	for (int i = 0; i < a->sys.count; i++) {
		if (a->sys_tex[i]) { SDL_DestroyTexture(a->sys_tex[i]); a->sys_tex[i] = NULL; }
		for (int k = 0; k < a->view[i].list.count; k++)
			if (a->view[i].tex[k]) {
				SDL_DestroyTexture(a->view[i].tex[k]);
				a->view[i].tex[k] = NULL;
			}
	}
}


/* Whether the radio should come up at boot.
 *
 * Per-device rather than per-card, on the same split as brightness: it is what
 * THIS handheld was last doing. The shipped default seeds it once, so there is
 * no fallback chain left for launch.sh to walk.
 *
 * Written the moment it changes rather than at shutdown, because a handheld
 * is switched off by holding a button or by running the battery flat, and
 * neither of those is a chance to save anything. */
static void wifi_pref_save(bool on)
{
	db_set_int(db_dev(), "wifi", on ? 1 : 0);
	/* launch.sh reads this one before the launcher exists, so the export has
	 * to follow the write rather than wait for a tidy shutdown. */
	db_write_boot_env();
}



/* Diatom watches this file, and it is rewritten per launch: it is the set
 * minus what has already been earned, which only this side knows. Runtime
 * scratch, so per-device rather than shared. */
static void chv_active_path(char *out, size_t n)
{
	snprintf(out, n, "%s/cheevos-active.set", P_USERDATA);
}

/* Auto Off: how long without input before the device powers itself down.
 *
 * Seconds, 0 for off. Eric's ladder, and it is deliberately aggressive at the
 * short end - 30s will fire while you read a dialogue box. That is a sound
 * trade only because resume-into-game exists: powering off costs about two
 * seconds and puts you back in the same game with the menu up. Without that
 * it would be hostile. */
static const int AUTO_OFF[] = { 0, 30, 60, 120, 300, 600 };
#define AUTO_OFF_COUNT ((int)(sizeof AUTO_OFF / sizeof AUTO_OFF[0]))

static int auto_off_load(void)
{
	int v = db_get_int(db_dev(), "autooff", -1);
	return v >= 0 ? v : 120;                  /* the default: two minutes */
}

static void auto_off_save(int seconds)
{
	db_set_int(db_dev(), "autooff", seconds);
}


/* Defined with the Wi-Fi screen it began in, and used here because signing in
 * and syncing are the other two things that make the player wait. */
static void wait_panel(app *a, const char *heading, const char *msg);

/* Pull down what the account already holds for the loaded game, and mark
 * those earned locally so they are not watched, not re-announced, and not
 * submitted again.
 *
 * The account wins on what EXISTS; the local store wins on what is still
 * owed, because it is the only record of anything earned offline. Neither is
 * discarded. Silent when offline or not signed in - that is the ordinary
 * case, and it leaves the device working from what it knows. */
static int ra_merge_unlocks(const int *ids, int n)
{
	int i, added = 0;

	if (n <= 0 || chv_game() <= 0) return 0;

	/* Only ids the set actually has. The account carries entries the set does
	 * not - RetroAchievements' "Unknown Emulator" notice is one, and it went
	 * straight into the store the first time this ran. Recording those adds a
	 * row that can never be displayed and could later be submitted as a
	 * duplicate of something that was never an achievement. */
	for (i = 0; i < n; i++) {
		int k;

		for (k = 0; k < chv_count(); k++)
			if (chv_at(k)->id == ids[i]) break;
		if (k == chv_count()) continue;
		if (chv_note_earned(chv_game(), ids[i], true)) added++;
	}

	if (added) {
		chv_earned_save();
	}
	return added;
}

/* Send what is owed. Called once the game is over and the launcher has the
 * screen back - never from the wait loop.
 *
 * Anything that will not send stays pending and is tried again next time,
 * which is the offline queueing RA's own requirements ask for and the right
 * shape regardless: an unlock earned on a plane is still earned. */
static void ra_flush_unlocks(const char *rom, const char *tag, int cur)
{
	char hash[33] = "";
	int game, id, sent = 0, settled = 0;

	if (!ra_signed_in() || chv_pending_count() == 0 || !net_online()) return;

	/* The hash is what a real client sends alongside an award, and it is the
	 * one for the game just played - so only its own unlocks carry it. */
	ra_hash_rom(rom, tag, hash);

	/* Index 0 every time: a successful send marks the row synced, so the next
	 * pending one becomes index 0. A failure stops the loop rather than
	 * spinning on it - if one will not go, the rest will not either. */
	while (chv_pending_at(0, &game, &id)) {
		int rc = ra_submit_unlock(id, game == cur ? hash : NULL);

		/* 0 is "the account already had it", which settles the row just as
		 * surely as sending it. Only a real failure stops the loop - if one
		 * will not go, the rest will not either. */
		if (rc < 0) break;
		chv_mark_synced(game, id);
		settled++;
		if (rc > 0) sent++;
	}
	/* On `settled`, not on `sent`. A row the account already had is settled
	 * without being sent, and writing only when something was sent left that
	 * mark in memory only - so it came back pending on the next boot and was
	 * retried, forever. Seen on the device: one unlock submitted by hand
	 * outside the launcher, refused as already-held, and asked again at the
	 * end of every game after that. */
	if (settled) {
		chv_earned_save();
		fprintf(stderr, "ra: %d sent, %d already held, %d still queued\n",
		        sent, settled - sent, chv_pending_count());
	}
}

/* ---------- the unlock queue, sent behind the shelf ---------------------- */

/* ra_flush_unlocks, above, on a thread of its own.
 *
 * Quitting a game with something earned used to wait here, on the main thread,
 * between the game's wait loop and the shelf's - so the power button was read
 * by neither. It hashed the ROM, then sent each pending unlock one at a time,
 * each through a request with a 20s timeout: a second or two on a black screen
 * on a good network, and on a bad one a handheld ignoring its own power button
 * for minutes. Now the shelf comes straight back and the sending happens behind
 * it.
 *
 * The worker never touches the cheevos store. The main thread takes a snapshot
 * of what is owed and a copy of the account, hands both over, and later applies
 * the answers itself - chv_mark_synced and chv_earned_save only ever run here.
 * No lock guards the store because nothing but the main thread can reach it.
 *
 * Two things had to be made safe before this could exist. net_post_buf wrote
 * every request's config and response to one file per PROCESS, so a request
 * from here overlapping one from the main thread would have swapped them; it
 * is per call now. And ra_submit_unlock read the account straight from the
 * globals a sign-in rewrites, so the worker sends as a copy instead.
 *
 * If the thread cannot be started, ra_flush_unlocks runs as it always did. */

/* Most unlocks one pass carries. More than that owed - a long spell offline -
 * is sent by the pass that follows, which starts by itself once this one has
 * settled something. */
#define FLUSH_MAX 256

static SDL_Thread *g_fl_thread;
static SDL_mutex  *g_fl_lock;
static SDL_cond   *g_fl_wake;
static bool        g_fl_stop;

/* The job, written here and taken by the worker. */
static bool g_fl_job;                    /* one is waiting */
static bool g_fl_busy;                   /* the worker has one in hand */
static char g_fl_rom[LIB_PATH * 2], g_fl_tag[16];
static char g_fl_user[RA_USER_MAX], g_fl_token[RA_TOKEN_MAX];
static int  g_fl_cur;                    /* whose unlocks carry the ROM hash */
static int  g_fl_game[FLUSH_MAX], g_fl_id[FLUSH_MAX], g_fl_n;

/* The answers, written by the worker and applied here. */
static int  g_fl_rgame[FLUSH_MAX], g_fl_rid[FLUSH_MAX], g_fl_rrc[FLUSH_MAX];
static int  g_fl_rn;
static bool g_fl_finished;               /* the job in hand is done */
static bool g_fl_failed;                 /* it stopped on a real failure */

/* Main thread only. The latest game quit, for the pass that follows; and
 * whether the job in hand has settled anything, which is what guarantees a
 * follow-on pass makes progress rather than spinning.
 *
 * The game is carried, not read from chv_game() when the pass starts. A pass
 * can start from the shelf, where the info screen loads whichever set it is
 * showing - so chv_game() could name another game and hand it this ROM's
 * hash. */
static char g_fl_next_rom[LIB_PATH * 2], g_fl_next_tag[16];
static int  g_fl_next_cur;
static bool g_fl_again;
static int  g_fl_settled;

static int flush_worker(void *unused)
{
	(void)unused;
	SDL_LockMutex(g_fl_lock);
	for (;;) {
		char rom[LIB_PATH * 2], tag[16], user[RA_USER_MAX], token[RA_TOKEN_MAX];
		char hash[33] = "";
		int  game[FLUSH_MAX], id[FLUSH_MAX], n, cur, i;
		bool failed = false, hashed = false;

		while (!g_fl_stop && !g_fl_job) SDL_CondWait(g_fl_wake, g_fl_lock);
		if (g_fl_stop) break;
		memcpy(rom, g_fl_rom, sizeof rom);
		memcpy(tag, g_fl_tag, sizeof tag);
		memcpy(user, g_fl_user, sizeof user);
		memcpy(token, g_fl_token, sizeof token);
		n = g_fl_n;
		cur = g_fl_cur;
		memcpy(game, g_fl_game, (size_t)n * sizeof game[0]);
		memcpy(id, g_fl_id, (size_t)n * sizeof id[0]);
		g_fl_job = false;
		g_fl_busy = true;
		SDL_UnlockMutex(g_fl_lock);

		for (i = 0; i < n; i++) {
			int rc;

			/* Checked before each request, not only between jobs: a launcher
			 * on its way out should not start a fresh 20s wait. */
			SDL_LockMutex(g_fl_lock);
			if (g_fl_stop) { SDL_UnlockMutex(g_fl_lock); break; }
			SDL_UnlockMutex(g_fl_lock);

			/* The hash only for the game just played, and only once, since
			 * it reads the whole ROM. */
			if (game[i] == cur && !hashed) {
				ra_hash_rom(rom, tag, hash);
				hashed = true;
			}
			rc = ra_submit_unlock_as(user, token, id[i],
			                         game[i] == cur ? hash : NULL);

			/* Bounded although it cannot fill: a pass never starts while the
			 * answers to the last one are waiting (see ra_flush_start), so
			 * this holds at most this pass's n. An answer dropped here would
			 * only mean that unlock is sent again next time and settled as
			 * already held. */
			SDL_LockMutex(g_fl_lock);
			if (g_fl_rn < FLUSH_MAX) {
				g_fl_rgame[g_fl_rn] = game[i];
				g_fl_rid[g_fl_rn]   = id[i];
				g_fl_rrc[g_fl_rn]   = rc;
				g_fl_rn++;
			}
			SDL_UnlockMutex(g_fl_lock);

			/* A real failure stops the pass - if one will not go, the rest
			 * will not either. 0, "the account already had it", does not. */
			if (rc < 0) { failed = true; break; }
		}

		SDL_LockMutex(g_fl_lock);
		g_fl_busy = false;
		g_fl_finished = true;
		g_fl_failed = failed;
	}
	SDL_UnlockMutex(g_fl_lock);
	return 0;
}

static bool flush_started(void)
{
	if (g_fl_thread) return true;
	if (!(g_fl_lock = SDL_CreateMutex())) return false;
	if (!(g_fl_wake = SDL_CreateCond())) {
		SDL_DestroyMutex(g_fl_lock);
		g_fl_lock = NULL;
		return false;
	}
	if (!(g_fl_thread = SDL_CreateThread(flush_worker, "tortos-raflush", NULL))) {
		SDL_DestroyCond(g_fl_wake);  g_fl_wake = NULL;
		SDL_DestroyMutex(g_fl_lock); g_fl_lock = NULL;
		return false;
	}
	return true;
}

/* Hand what is owed to the worker and return at once. Main thread. */
static void ra_flush_start(const char *rom, const char *tag, int cur)
{
	int n = 0, game, id;

	if (!ra_signed_in() || chv_pending_count() == 0 || !net_online()) return;
	if (!flush_started()) { ra_flush_unlocks(rom, tag, cur); return; }

	/* Not copied when they already are these buffers: the follow-on pass hands
	 * them straight back in, and snprintf from a buffer into itself is
	 * undefined. */
	if (rom != g_fl_next_rom)
		snprintf(g_fl_next_rom, sizeof g_fl_next_rom, "%s", rom);
	if (tag != g_fl_next_tag)
		snprintf(g_fl_next_tag, sizeof g_fl_next_tag, "%s", tag);
	g_fl_next_cur = cur;

	SDL_LockMutex(g_fl_lock);
	/* Busy until its answers have been APPLIED, not merely until the worker
	 * finishes. A pass can finish during a game, and its answers then wait
	 * for the main loop. Starting another before they are applied would
	 * snapshot those same unlocks as still owed and send them twice, and would
	 * append a second pass's answers onto the first's. */
	if (g_fl_job || g_fl_busy || g_fl_finished || g_fl_rn > 0) {
		/* Note it instead; the pass that follows picks up everything still
		 * owed once these answers are in. */
		g_fl_again = true;
		SDL_UnlockMutex(g_fl_lock);
		return;
	}
	while (n < FLUSH_MAX && chv_pending_at(n, &game, &id)) {
		g_fl_game[n] = game;
		g_fl_id[n]   = id;
		n++;
	}
	snprintf(g_fl_rom, sizeof g_fl_rom, "%s", rom);
	snprintf(g_fl_tag, sizeof g_fl_tag, "%s", tag);
	ra_creds_copy(g_fl_user, sizeof g_fl_user, g_fl_token, sizeof g_fl_token);
	g_fl_cur = cur;
	g_fl_n = n;
	g_fl_settled = 0;
	g_fl_job = true;
	SDL_CondSignal(g_fl_wake);
	SDL_UnlockMutex(g_fl_lock);
}

/* Apply whatever the worker has answered, and start the next pass when one is
 * owed. Main thread, every pass of the main loop - it is a lock and a count
 * when there is nothing to do. Nothing here is drawn, so it asks for no frame. */
static void ra_flush_poll(void)
{
	int  rgame[FLUSH_MAX], rid[FLUSH_MAX], rrc[FLUSH_MAX], rn, i, sent = 0, settled = 0;
	bool finished, failed, again;

	if (!g_fl_thread) return;
	SDL_LockMutex(g_fl_lock);
	rn = g_fl_rn;
	memcpy(rgame, g_fl_rgame, (size_t)rn * sizeof rgame[0]);
	memcpy(rid, g_fl_rid, (size_t)rn * sizeof rid[0]);
	memcpy(rrc, g_fl_rrc, (size_t)rn * sizeof rrc[0]);
	g_fl_rn = 0;
	finished = g_fl_finished;
	failed = g_fl_failed;
	g_fl_finished = false;
	again = g_fl_again;
	if (finished) g_fl_again = false;
	SDL_UnlockMutex(g_fl_lock);

	for (i = 0; i < rn; i++) {
		if (rrc[i] < 0) continue;
		chv_mark_synced(rgame[i], rid[i]);
		settled++;
		if (rrc[i] > 0) sent++;
	}
	/* On settled, not on sent, for the reason ra_flush_unlocks gives. */
	if (settled) {
		g_fl_settled += settled;
		chv_earned_save();
		fprintf(stderr, "ra: %d sent, %d already held, %d still queued\n",
		        sent, settled - sent, chv_pending_count());
	}

	if (!finished) return;
	/* Another pass when a later game was quit during this one, or when this one
	 * ran out of room with more still owed. The second only if it settled
	 * something, so a pass that achieved nothing cannot keep starting itself. */
	if (again || (!failed && g_fl_settled > 0 && chv_pending_count() > 0))
		ra_flush_start(g_fl_next_rom, g_fl_next_tag, g_fl_next_cur);
}

/* Tell the worker to stop, and do not wait for it.
 *
 * Waiting could mean sitting out a request's 20s timeout on the way to exit or
 * power off. It needs no waiting: the worker touches nothing that shutdown
 * frees - no SDL object is destroyed here, it never reaches the store or the
 * database - so the process ending takes it cleanly. What it had not answered
 * stays owed and is sent next time; one it had sent but not yet reported is
 * sent again, and the account's "already has this achievement" settles it. */
static void ra_flush_stop(void)
{
	if (!g_fl_thread) return;
	SDL_LockMutex(g_fl_lock);
	g_fl_stop = true;
	SDL_CondSignal(g_fl_wake);
	SDL_UnlockMutex(g_fl_lock);
	SDL_DetachThread(g_fl_thread);
	g_fl_thread = NULL;
}

/* ---------- what the account already has, read while the game runs ------- */

/* The answer to "which of this game's achievements does the account hold",
 * asked at launch and read as soon as it lands - from the game tick during a
 * game, from the main loop on the shelf.
 *
 * It used to be read only when the game ended, waiting there until it had
 * landed. So a game's first session after signing in showed 0 of N in its own
 * menu while the account held 13, and a quit straight after a launch on a slow
 * network sat out the request between the game's wait loop and the shelf's.
 * Now the menu is right about half a second in, and quitting never waits.
 *
 * The unlocks owed still go out after the answer, not before it: settling what
 * the account already holds first is what stops them being sent as duplicates.
 * So a quit that beats the answer leaves the sending to whichever comes first
 * - the answer landing, or something else needing the one async slot, which
 * gives up on the answer and sends anyway. Duplicates that causes are answered
 * as already held, which settles them just the same. */
static long g_sync_game;                   /* the game asked about, 0 none */
static char g_sync_set[LIB_PATH * 2];      /* its set, to check the answer against */

/* A send waiting for the answer. */
static bool g_sync_flush;
static char g_sync_rom[LIB_PATH * 2], g_sync_tag[16];
static int  g_sync_cur;

static void sync_begin(long game, const char *set_path)
{
	ra_sync_begin(game);
	g_sync_game = ra_sync_pending() ? game : 0;
	snprintf(g_sync_set, sizeof g_sync_set, "%s", set_path ? set_path : "");
}

static void sync_flush_now(void)
{
	if (!g_sync_flush) return;
	g_sync_flush = false;
	ra_flush_start(g_sync_rom, g_sync_tag, g_sync_cur);
}

/* The answer, merged if it is in. Returns how many achievements it added to
 * what this device knew - nonzero means the list Diatom is watching still
 * holds some the account already has. Cheap when nothing has landed: a
 * waitpid that does not wait. */
static int sync_poll(void)
{
	int ids[CHV_MAX], n, rc, added = 0;

	if (!g_sync_game) return 0;
	rc = ra_sync_poll(ids, CHV_MAX, &n);
	if (rc == 0) return 0;

	if (rc > 0 && n > 0) {
		/* Against the set the question was about. During a game that is the
		 * one loaded; on the shelf the info screen loads whichever set it is
		 * showing, and nothing on the shelf depends on which one that is. */
		if (chv_game() != g_sync_game) chv_load(g_sync_set);
		if (chv_game() == g_sync_game) added = ra_merge_unlocks(ids, n);
	}
	g_sync_game = 0;
	sync_flush_now();
	return added;
}

/* Something else needs the one async slot now - a launch, the box art
 * scraper. The answer is dropped, and the next launch of that game asks
 * again. The shelf's update check gives it up too (upbg_abandon). */
static void upbg_abandon(void);
static void sync_abandon(void)
{
	upbg_abandon();
	if (g_sync_game) {
		ra_sync_abandon();
		g_sync_game = 0;
		fprintf(stderr, "ra: dropped an account answer that had not landed\n");
	}
	sync_flush_now();
}

/* Where the launcher's own work happens during a game. Ten times a second,
 * from inside the wait loop, and it must stay cheap: that loop is the power
 * button's watchdog.
 *
 * Its one job is the first play of a game. The set is being found behind the
 * game rather than in front of it, and when it lands the player is told the
 * only way anyone can be told during a game - over Diatom's overlay. */
static char g_pending_set[LIB_PATH * 2];
static char g_pending_active[LIB_PATH * 2];
/* The watch list Diatom was last handed and when, so an account answer that
 * lands early can trim it. 0 when there is no list to trim. */
#define WATCH_RETRIM_MS 5000
static unsigned g_watch_ms;
static char     g_watch_active[LIB_PATH * 2];
/* The set path of the game actually running, so a fetch that lands during a
 * DIFFERENT game can be recognized and dropped. Written at every launch. */
static char g_game_set[LIB_PATH * 2];

/* ---- where the system's sound goes --------------------------------------- */

/* The policy is a setting; the other two facts are read from the device. The
 * RULE that combines them is src/audioout.c, where it can be checked without a
 * handheld - this half only gathers. */
static aout_policy g_aout_policy;
static char g_aout_sent[128];      /* the device Diatom was last told to use */
static bool g_aout_ever;
static unsigned g_aout_gen;        /* which connection it was told on */
static unsigned g_aout_retry_at;   /* earliest next in-game retry of a fallback */
static int      g_aout_retries;    /* spent on the game running now */
static bool     g_aout_fallen;     /* a fallback already noticed, so not new */

static aout_policy aout_load(void)
{
	char v[32];
	/* The default follows the cable; only an explicit "speaker" overrides. */
	db_get_str(db_dev(), "audioout", v, sizeof v, "auto");
	return !strcmp(v, "speaker") ? AOUT_SPEAKER : AOUT_AUTO;
}

static void aout_save(aout_policy p)
{
	db_set_str(db_dev(), "audioout", p == AOUT_SPEAKER ? "speaker" : "auto");
}

/* Which ears hear what, for a player who hears with one. Chosen once; what
 * each program is told depends on where its own sound goes - see
 * aout_channels_for. */
static aout_channels g_channels;

static aout_channels channels_load(void)
{
	char v[16];

	db_get_str(db_dev(), "channels", v, sizeof v, "stereo");
	return aout_channels_parse(v);
}

static void channels_save(aout_channels c)
{
	db_set_str(db_dev(), "channels", aout_channels_wire(c));
}

/* Diatom and Muse each by the device they were just given, because the two
 * can be in different places: with Muse on a headset, Diatom is on the codec,
 * and a side on the headset is still mono on the speaker. Both send only on a
 * change, so this is cheap every tick. */
static void channels_tell(const aout_state *s, const char *diatom_dev,
                          const char *muse_dev)
{
	plat_resident_channels(aout_channels_wire(
		aout_channels_for(g_channels, aout_dest_of(s, diatom_dev))));
	musec_channels(aout_channels_wire(
		aout_channels_for(g_channels, aout_dest_of(s, muse_dev))));
}

/* The connected sink, published by bt_reconnect in launch.sh.
 *
 * A file rather than asking Bluetooth ourselves: asking means forking
 * bluetoothctl out of a 119 MB process, which is what menu_wifi exists to
 * prevent. The shell loop already knows, so it writes and this reads. */
static char g_bt_link[32];    /* the published link's ACL handle, see bt_reconnect */

static const char *aout_bt_sink(void)
{
	static char   sink[128];
	static unsigned last;
	static bool   primed;
	unsigned now = plat_now_ms();
	FILE *f;

	/* Half a second. Plugging a cable in should feel immediate; a stat that
	 * often is nothing, but there is no reason to do it 120 times a second. */
	if (primed && now - last < 500) return sink;
	primed = true;
	last = now;
	sink[0] = g_bt_link[0] = '\0';
	f = fopen("/tmp/tortos_btsink", "r");
	if (f) {
		if (fgets(sink, sizeof sink, f)) sink[strcspn(sink, "\r\n")] = '\0';
		if (fgets(g_bt_link, sizeof g_bt_link, f))
			g_bt_link[strcspn(g_bt_link, "\r\n")] = '\0';
		fclose(f);
	}
	return sink;
}

static aout_state aout_now(void)
{
	aout_state s;

	s.policy  = g_aout_policy;
	s.wired   = plat_headphones_present();
	s.usb     = plat_usb_audio_present();
	s.bt_sink = aout_bt_sink();
	return s;
}

/* Where Diatom actually IS, which is not always where it was told to go: a
 * sink that will not open, or one that died under it, makes the port fall back
 * and report the fallback. A non-empty device is the named sink; an empty one
 * is the codec, where the cable decides what you hear.
 *
 * Falls back to the launcher's own resolution until Diatom has said anything -
 * on the desktop build it never will. */
static aout_dest aout_actual(const aout_state *s)
{
	char at[128];
	const char *muse = musec_sink_now();

	/* Muse holding the headset is where the sound is, whatever Diatom says:
	 * Diatom was moved off it to let Muse have it. */
	if (musec_heard() && muse[0] && strcmp(muse, "default"))
		return strcmp(muse, AOUT_USB_DEVICE) ? AOUT_BT : AOUT_USB;
	if (!plat_resident_audio(at, sizeof at)) return aout_resolve(s);
	if (at[0]) return strcmp(at, AOUT_USB_DEVICE) ? AOUT_BT : AOUT_USB;
	return s->wired ? AOUT_WIRED : AOUT_SPK;
}

/* SETAUDIO, after which Diatom's last report no longer counts until it has
 * answered this one. Reports are only read while a game runs, so at the shelf
 * the one held was from the last game: switching to Speaker there, or with
 * Muse on a headset, the Audio Output row went on saying "bluetooth" while the
 * sound came out of the speaker - #45, 2026-09-26. Until the answer, the row
 * shows what was asked; after it, what happened, fallback included. */
static bool aout_send(const char *dev)
{
	if (!plat_resident_line("SETAUDIO\tdevice=%s", dev)) return false;
	plat_resident_audio_asked();
	return true;
}

/* Send only on a change. SETAUDIO reopens an audio device, which is cheap but
 * not free, and doing it every tick would be a reopen per tick. */
static void aout_tell_diatom(const aout_state *s, const char *dev, bool force,
                             bool muse_has_it)
{
	unsigned gen = plat_resident_generation();

	/* Send when the ASKED-FOR device changes, or when the emulator is a new
	 * one. Never because Diatom ended up somewhere else.
	 *
	 * An earlier version compared against what Diatom REPORTED, so that a
	 * restarted emulator would be re-told. That is unrecoverable on a
	 * fallback: a headset switched off makes Diatom report the default while
	 * this side still wants the sink, the two can never agree, and the resend
	 * fires every tick. Measured 2026-09-05 - 413 route decisions and 335
	 * device reopens in one session, which left the codec parked in SETUP and
	 * the game silent on a speaker that was technically open.
	 *
	 * The restart case is covered by the generation instead, which changes
	 * exactly once per connection rather than continuously. */
	if (!force && g_aout_ever && gen == g_aout_gen && !strcmp(dev, g_aout_sent))
		return;
	g_aout_gen = gen;
	/* Only remember it as sent if it actually went. dsend does nothing when
	 * the socket is not open, and returns false saying so - which on the shelf
	 * before the first game is every time. Recording it as sent anyway meant
	 * the next call compared equal and never retried, so the route the log
	 * announced was one Diatom had never been told about. */
	if (!aout_send(dev)) return;

	snprintf(g_aout_sent, sizeof g_aout_sent, "%s", dev);
	g_aout_ever = true;
	/* Diatom's answer to this lands before a retry judges it. The previous
	 * device's report is still the one held until then. */
	g_aout_retry_at = plat_now_ms() + 5000;
	/* Logged because it only happens on a real change - a cable, a headset,
	 * or the row - and because "where is the sound going" is otherwise
	 * invisible after the fact. Diatom logs its own fallback, so the pair of
	 * lines says both what was asked for and what happened. */
	fprintf(stderr, "audio: %s -> %s%s\n",
	        aout_dest_name(aout_resolve(s)), dev[0] ? dev : "default",
	        muse_has_it ? " (Muse has the headset)" : "");
}

/* The headset's own volume, kept at the Brick's level while sound goes to one.
 *
 * The volume keys move the codec, which a Bluetooth route bypasses, so on a
 * headset they did nothing - and a headset with no buttons of its own, the
 * OpenFit or AirPods, had no volume anywhere. With btplayer registered, BlueZ
 * passes a volume on to the headset over AVRCP (tools/btplayer.c), so the
 * level is mirrored there: in a game the one Diatom last reported, since it
 * owns the keys and this side only applies its levels when the game ends;
 * otherwise this side's own. 0..20 onto AVRCP's 0..127, straight, because
 * AVRCP volume is a plain proportion and the headset applies its own curve.
 *
 * Sent on a change, and again for a new connection so a headset starts at
 * the Brick's level. One way only: a headset's own buttons move the headset
 * and not this level. A send that fails - no control yet, right after a
 * connect - waits two seconds before the next, because each is a mixer open. */
static bool g_game_ticking;     /* inside on_game_tick: a game is running */

static void bt_volume_follow(const char *out, bool fresh)
{
	static int      sent = -1;
	static char     sent_to[64];     /* the headset it went to */
	static unsigned retry_at, looked, sent_at;
	static long     seen;            /* the stamp of the last report read */
	static bool     primed;
	int count = 0, level = plat_resident_volume(&count), v;
	unsigned now = plat_now_ms();
	struct stat st;

	if (!out[0]) { sent = -1; sent_to[0] = '\0'; return; }

	/* The other way first: the headset's OWN buttons, which btplayer passes on
	 * as `<PCM name> <0..127> <stamp>` (tools/btplayer.c). Adopted as the Brick's
	 * level - through Diatom in a game, which owns the level while it runs -
	 * and recorded as sent, so it is not echoed back to the headset. A new
	 * stamp is a new report - not a new inode, which tmpfs takes from a pool
	 * and can hand out again, and presses went missing until the next one.
	 * The report found at start is only noted, or a value left from before a
	 * restart would move the level. A tenth of a second between looks.
	 *
	 * NOT within a second of this side sending that headset a volume: it
	 * reports what it was sent, too - the OpenRun answered every set with its
	 * own announcement, measured 2026-09-26 - and sometimes not quite the same
	 * number (sent 32, back came 28), so adopting the echo stepped the level
	 * back and forth. A real press that close to a key press costs one step. */
	if (now - looked >= 100) {
		FILE *f = stat("/tmp/tortos_btvol", &st) == 0 ? fopen("/tmp/tortos_btvol", "r") : NULL;
		char who[64];
		int  hv, want;
		long stamp;

		looked = now;
		if (f && fscanf(f, "%63s %d %ld", who, &hv, &stamp) == 3 && stamp != seen) {
			bool fresh_report = primed;

			seen = stamp;
			primed = true;
			if (fresh_report && !strcmp(who, out) && hv >= 0 && hv <= 127) {
				bool echo = !strcmp(who, sent_to) && now - sent_at < 1000;
				int  have = g_game_ticking && level >= 0 ? level : plat_volume_get();

				want = (hv * plat_volume_max() + 63) / 127;
				if (!echo && want != have) {
					if (g_game_ticking)
						plat_resident_line("SETLEVEL\tkind=volume\tindex=%d\tcount=%d",
						                   want, plat_volume_max() + 1);
					else
						plat_volume_nudge(want - have);
					sent = (want * 127 + plat_volume_max() / 2) / plat_volume_max();
					snprintf(sent_to, sizeof sent_to, "%s", out);
					fprintf(stderr, "audio: %s's own buttons: %d/127, level %d of %d\n",
					        out, hv, want, plat_volume_max());
				}
			}
		}
		if (f) fclose(f);
		if (!primed) primed = true;               /* no file yet is a start too */
	}
	if (level < 0) { level = plat_volume_get(); count = plat_volume_max() + 1; }
	if (level < 0 || count < 2) return;
	v = (level * 127 + (count - 1) / 2) / (count - 1);
	/* And again for a different headset at the same level: with two connected
	 * the sound can move between them, and a link handle is no help there - it
	 * is reused. */
	if (!fresh && v == sent && !strcmp(out, sent_to)) return;
	if ((int)(now - retry_at) < 0) return;
	if (!btvol_set(out, v)) { retry_at = now + 2000; return; }
	sent = v;
	sent_at = now;
	snprintf(sent_to, sizeof sent_to, "%s", out);
	/* With its cost, because it is a mixer open on the loop that draws. */
	fprintf(stderr, "audio: %s volume %d/127 (level %d of %d) in %u ms\n",
	        out, v, level, count - 1, plat_now_ms() - now);
}

/* Where the system's sound goes, for both players.
 *
 * On the speaker and the jack both simply open the default, which is dmix.
 * A Bluetooth headset cannot be shared - bluealsa 3.1 gives an A2DP PCM to one
 * client at a time, measured 2026-09-25 - so it is HANDED between them, and it
 * goes to whoever may be heard: Muse while it plays, which is exactly when
 * the game has already been quieted, and Diatom otherwise.
 *
 * Release before acquire, or the second open fails and falls back. Towards
 * Muse, Diatom is told first and Muse's SINK retries for a second while Diatom
 * lets go, because at the shelf nothing here reads Diatom's answers. Back
 * towards Diatom, Muse is told first and Diatom is only told once Muse has
 * answered, because Diatom does not retry. */
static void aout_apply(bool force)
{
	static char  link_seen[32];
	aout_state  s    = aout_now();
	const char *out  = aout_device(&s);
	bool        muse = out[0] && musec_heard();
	/* A new connection under the same name - the headset dropped and came
	 * back between two looks at it. Nothing else here can see that, and
	 * whoever fell back to the speaker while it was gone would stay there:
	 * seen 2026-09-25, a song in Muse. So the holder is told again, once.
	 * Harmless when it never left: Diatom and Muse both ignore a device they
	 * are already on. */
	bool        fresh = out[0] && g_bt_link[0] && strcmp(g_bt_link, link_seen);

	/* A USB-C DAC is mixed (the system's "usb" device), so it is not handed
	 * over: Diatom and Muse both have it at once, as both have the codec, and
	 * the headset's volume has nothing to follow. */
	if (aout_resolve(&s) == AOUT_USB) {
		link_seen[0] = '\0';
		aout_tell_diatom(&s, out, force, false);
		musec_sink(out);
		channels_tell(&s, out, out);
		bt_volume_follow("", false);
		return;
	}
	if (!muse) {
		musec_sink("");
		if (out[0] && !musec_sink_settled()) return;    /* Muse is still letting go */
	}
	if (fresh) {
		fprintf(stderr, "audio: %s is a new connection (link %s)\n", out, g_bt_link);
		if (muse) musec_sink_again();
	}
	snprintf(link_seen, sizeof link_seen, "%s", out[0] ? g_bt_link : "");
	aout_tell_diatom(&s, muse ? "" : out, force || (fresh && !muse), muse);
	if (muse) musec_sink(out);
	channels_tell(&s, muse ? "" : out, muse ? out : "");
	bt_volume_follow(out, fresh);
}

/* The hook musec calls just before a PLAY or RESUME, so the track starts on
 * the headset rather than reaching it a tick later. */
static void aout_before_muse(void)
{
	aout_apply(false);
}

/* Muse's own screens, each frame. They route only while music is heard: a
 * headset that connects mid-song takes the song there and then, where before
 * it stayed on the speaker until something new was played - seen 2026-09-25,
 * resume and R1 both went to the speaker right after a pairing. Paused, they
 * still leave the headset where it is; nothing else can be heard on these
 * screens, so handing it back on every pause would only be churn. */
static void muse_screen_poll(void)
{
	muse_poll();
	if (musec_heard()) aout_apply(false);
}

/* A sink that failed once is otherwise never tried again. aout_apply sends on
 * a change of the ASKED-FOR device, and a headset that would not open, then
 * became usable under the same name, is no change at all: 2026-09-24 left
 * Diatom on the speaker through a game with the headset connected and working.
 *
 * Retried where it cannot be seen, and rarely where it can. A failed SETAUDIO
 * is not free: Diatom closes, fails the sink and reopens the default on its
 * main thread. Measured 2026-09-24 in Contra, 17 switches, 15 of them failed
 * fakes: 56.00 fps against 59.95, ~351 frames short, 18 resyncs against 3.
 * About 20 frames - a third of a second of frozen game - per failed try, and
 * that is the lower bound, because a fake name fails before bluealsa is ever
 * asked. A retry a minute would be a stutter a minute. */

/* Whether Diatom is on the default device although it was last told to use a
 * sink. The default and not merely "somewhere else", because the default is
 * the only place a fallback goes, and a report naming another sink is one that
 * has not caught up with the last SETAUDIO yet. Testing for any mismatch fired
 * a retry in the same tick as the send, before Diatom had answered - measured
 * on the device 2026-09-24, and it spent one of the two stalls on nothing.
 *
 * Only true to life while a game runs: at the shelf nothing reads Diatom's
 * reports - they are dropped at the next game start - so there it describes
 * the last game. */
static bool aout_fell_back(void)
{
	char at[128];

	if (!g_aout_ever || !g_aout_sent[0]) return false;
	if (!plat_resident_audio(at, sizeof at)) return false;
	return at[0] == '\0';
}

/* Just before a game is handed over. Unconditional, because the shelf cannot
 * know whether it is needed and it costs nothing when it is not: Diatom
 * returns early on the device it is already using, without reopening. When
 * the sink is still broken, the stall is spent inside the launch. */
static void aout_before_launch(void)
{
	g_aout_retries = 0;
	g_aout_fallen = false;
	g_aout_retry_at = plat_now_ms() + 5000;
	if (!g_aout_ever || !g_aout_sent[0]) return;
	if (aout_fell_back())
		fprintf(stderr, "audio: trying %s again before the launch\n", g_aout_sent);
	aout_send(g_aout_sent);
}

/* Twice per game at most: for a headset whose A2DP stream was not up yet when
 * the game asked for it, or one that dropped out and came back under the same
 * name. Spaced so that Diatom's answer to the first has arrived before the
 * second is judged. */
static void aout_retry_in_game(void)
{
	bool fell = aout_fell_back();

	/* Never in the moment a fallback is noticed. A sink that has just died is
	 * certainly still dead: a headset disconnected mid-game on 2026-09-24 had
	 * its first retry fire within the same second and fail, and only the
	 * second, after bt_reconnect brought it back 18s later, landed. */
	if (fell && !g_aout_fallen) {
		unsigned soonest = plat_now_ms() + 5000;

		if ((int)(soonest - g_aout_retry_at) > 0) g_aout_retry_at = soonest;
	}
	g_aout_fallen = fell;

	if (!fell || g_aout_retries >= 2) return;
	if ((int)(plat_now_ms() - g_aout_retry_at) < 0) return;
	if (!aout_send(g_aout_sent)) return;
	g_aout_retries++;
	g_aout_retry_at = plat_now_ms() + 25000;
	fprintf(stderr, "audio: %s fell back; trying again (%d of 2)\n",
	        g_aout_sent, g_aout_retries);
}

/* Auto Off while a game runs. Diatom holds the clock, because it owns the pad
 * and this process is blocked in plat_resident_wait; the launcher's part is
 * telling it the number and keeping that number true as the charger comes and
 * goes. See on_charger, which lives beside battery_level. */
static bool on_charger(void);
static int  battery_level(void); /* the low-battery disc, sent mid-game below */
static int  g_idle_secs;         /* the Auto Off setting, 0 off */
static bool g_idle_charging;     /* what was true when SETIDLE was last sent */

static void on_game_tick(void)
{
	char msg[96];

	int rc;

	/* Muse, bounded: read what has arrived, and send at most one PLAY when a
	 * track ends - which is what keeps an album going through a game.
	 *
	 * And the game goes quiet while it plays - Diatom's QUIET, followed
	 * here at ten a second, which is how pausing the music or the album
	 * ending brings the game's sound back within a tenth of a second. Only a
	 * change is sent. */
	muse_poll();
	plat_resident_quiet(musec_playing());
	/* The low-battery disc over the game, which Diatom draws as this process
	 * can draw nothing while a game runs, blinking when critical. Sent only
	 * on a change; the level is read every five seconds. */
	plat_resident_low_battery(battery_level());

	/* Play time. Writes at most one unsynced row and usually nothing, and
	 * never an fsync: measured on the card, an fsync is 1.56 ms median but
	 * 16.4 ms at the tail, which is a dropped frame. */
	stats_tick(plat_now_ms());

	/* The charger, followed rather than sampled once at launch. Plugging in
	 * mid-game has to stop the countdown and unplugging has to start a fresh
	 * one, and SETIDLE does both: Diatom restarts its clock on every one it
	 * receives.
	 *
	 * Which is also why this only sends on a CHANGE. Sending it every tick
	 * would restart that clock ten times a second and Auto Off would never
	 * fire at all. */
	/* A cable plugged in mid-game, or a headset that connected or walked out
	 * of range, moves the sound without leaving the game. That is the whole
	 * point of Diatom making this a state rather than a launch argument. */
	g_game_ticking = true;             /* see bt_volume_follow */
	aout_apply(false);
	g_game_ticking = false;
	aout_retry_in_game();

	if (g_idle_secs > 0) {
		bool ch = on_charger();

		if (ch != g_idle_charging) {
			g_idle_charging = ch;
			plat_resident_line("SETIDLE\tms=%d",
			                   ch ? 0 : g_idle_secs * 1000);
		}
	}

	/* The account's answer, as soon as it lands - see sync_poll.
	 *
	 * When it adds achievements this device did not know were earned, the list
	 * Diatom is watching still has them in it, and one of them firing reads as
	 * a fresh unlock. So the list is handed over again without them - but only
	 * early in a session. Reloading a set starts every partly-met achievement
	 * over (Diatom's diatom_cheevos_load re-initializes the runtime), and
	 * WATCH_RETRIM_MS is ten times the measured answer and still the opening of
	 * a session. Later than that the count is still put right; a repeat unlock
	 * is sent and answered as already held.
	 *
	 * Before the return below, which is about a set still being fetched: the
	 * two never share the slot, because this game's question is only asked
	 * once its set has arrived. */
	if (sync_poll() > 0 && g_watch_ms &&
	    plat_now_ms() - g_watch_ms <= WATCH_RETRIM_MS) {
		if (chv_write_active(g_watch_active))
			plat_resident_line("SETCHEEVOS\tpath=%s\tconsole=%d",
			                   g_watch_active, chv_console());
		else                           /* all of it held: watch nothing */
			plat_resident_line("SETCHEEVOS\tpath=\tconsole=%d", chv_console());
		g_watch_ms = 0;
		fprintf(stderr, "cheevos: the account held some already; watching the rest\n");
	}

	if (!g_pending_set[0]) return;

	/* Once. Calling it twice advances the state machine twice, which is a
	 * lookup answered and then thrown away. */
	rc = ra_fetch_step();
	if (rc == 0) return;                        /* still working */
	if (rc < 0) { g_pending_set[0] = '\0'; return; }  /* RA has never seen it */

	/* A FETCH CAN OUTLIVE THE GAME IT WAS FOR, and handing over the wrong set
	 * is not a cosmetic mistake. This runs on the game tick, so the game that
	 * started the fetch may have ended and another may be running - and below,
	 * SETCHEEVOS hands the set to Diatom mid-game, which its protocol makes
	 * the normal path precisely because a download can still be in flight.
	 *
	 * Nothing downstream would catch it. rcheevos would evaluate one game's
	 * conditions against another game's memory, and a condition that matched
	 * by coincidence would unlock an achievement the player never earned, in a
	 * game they were not playing, and on_cheevo_unlocked would queue it for
	 * submission to their account.
	 *
	 * Seen 2026-09-10: launching Advance Guardian Heroes drew the set notice
	 * for A Boy and His Blob, an NES game fetched earlier the same evening.
	 * The active set was not poisoned that time, which is luck, not a guard.
	 *
	 * Dropped rather than canceled: there is no ra_fetch_abort, and
	 * ra_fetch_step only runs while g_pending_set is set, so clearing it on
	 * game end - the fix this looked like it wanted - would abandon a live
	 * state machine with nobody to drive it. The fetch has already written the
	 * set to its own cache by now, so the next launch of THAT game finds it
	 * waiting. A stale fetch becomes a cache warm instead of a hazard. */
	if (strcmp(g_pending_set, g_game_set) != 0) {
		fprintf(stderr, "cheevos: set arrived for a game that is no longer "
		                "running; cached, not applied\n");
		g_pending_set[0] = '\0';
		return;
	}

	if (!chv_load(g_pending_set)) { g_pending_set[0] = '\0'; return; }
	sync_begin(chv_game(), g_pending_set);   /* read on a later tick - sync_poll */

	if (chv_write_active(g_pending_active)) {
		plat_resident_line("SETCHEEVOS	path=%s	console=%d",
		                   g_pending_active, chv_console());
		g_watch_ms = plat_now_ms();
		snprintf(g_watch_active, sizeof g_watch_active, "%s", g_pending_active);
		snprintf(msg, sizeof msg, "%d to earn", chv_count());
	} else {
		snprintf(msg, sizeof msg, "all %d already earned", chv_count());
	}

	/* Rendering is safe here; presenting would not be. Same rule as an
	 * unlock notice, and the same path. */
	{
		char p[CFG_STR * 2];

		snprintf(p, sizeof p, "%s/notice.dtov", P_USERDATA);
		if (notice_render(msg, chv_game_title(), p))
			plat_resident_line("OVERLAY	path=%s	ms=3500", p);
	}
	g_pending_set[0] = '\0';
}

/* Called from inside plat_resident_wait, mid-game, when the launcher owns
 * nothing and can draw nothing. Recording is all that happens here; the
 * telling happens the next time this process has the screen. */
static void on_cheevo_unlocked(int id)
{
	char p[CFG_STR * 2];
	const cheevo *c = NULL;
	int i;

	if (!chv_note_unlock(id)) return;

	/* Written through immediately rather than at exit. A game ends by power
	 * button as often as by menu, and an achievement lost to a flat battery is
	 * one the player has to earn twice. */
	chv_earned_save();

	/* And say so, on screen, now. This process owns no display while a game
	 * runs, so it renders the line and Diatom composites it.
	 * Rendering is safe here; presenting would not be. */
	for (i = 0; i < chv_count(); i++)
		if (chv_at(i)->id == id) { c = chv_at(i); break; }

	{
		char head[64];

		/* Points on the context line, not the name: "First Blood!" is what
		 * the player wants to read, and "1 point" is what it was worth. */
		if (c && c->points > 0)
			snprintf(head, sizeof head, "Unlocked  -  %d point%s",
			         c->points, c->points == 1 ? "" : "s");
		else
			snprintf(head, sizeof head, "Unlocked");

		snprintf(p, sizeof p, "%s/notice.dtov", P_USERDATA);
		if (notice_render(head, c && c->title[0] ? c->title : "Unlocked", p))
			plat_resident_line("OVERLAY\tpath=%s\tms=4000", p);
	}
}

/* Called from inside plat_resident_wait when Diatom says the fast-forward step
 * changed, or that a game resumed fast. Drawn as an unlock notice is -
 * rendered here, composited there - and shorter, since it says a word. The
 * step, not the speed reached: on a heavy SNES or GBA game 3x and 4x both run
 * about 2.5x, and that figure moves from scene to scene. */
static void on_speed(int x)
{
	char p[CFG_STR * 2], body[16];

	if (x > 1) snprintf(body, sizeof body, "%dx", x);
	else       snprintf(body, sizeof body, "Off");
	snprintf(p, sizeof p, "%s/notice.dtov", P_USERDATA);
	if (notice_render("Fast forward", body, p))
		plat_resident_line("OVERLAY\tpath=%s\tms=1500", p);
}

/* ChaOS: rewinding, said over the game the way fast forward is - for as long
 * as it is held, since Diatom says so again each second */
static void on_rewind(void)
{
	char p[CFG_STR * 2];

	snprintf(p, sizeof p, "%s/notice.dtov", P_USERDATA);
	if (notice_render("Rewind", "Rewinding", p))
		plat_resident_line("OVERLAY\tpath=%s\tms=1200", p);
}

/* ChaOS: screenshots. The game being played's folder (shots.h), set at its
 * launch; a frame (a BMP Diatom wrote) filed there as a PNG. Returns whether
 * it was kept. */
static char g_shots_dir[LIB_PATH * 2];

static bool shot_keep(const char *bmp)
{
	char path[LIB_PATH * 2 + 64];
	SDL_Surface *sf;
	bool ok = false;

	if (!bmp || !*bmp || !g_shots_dir[0]) return false;
	if (!shots_new_path(g_shots_dir, time(NULL), path, sizeof path)) return false;
	if ((sf = IMG_Load(bmp))) {
		ok = IMG_SavePNG(sf, path) == 0;
		SDL_FreeSurface(sf);
	}
	if (!ok) unlink(path);
	else sync();
	fprintf(stderr, "screenshot: %s %s\n", ok ? "kept" : "could not keep", path);
	return ok;
}

/* MENU+X in a game: Diatom wrote the frame; filed here, and said over the
 * game as fast forward is */
static void on_shot(const char *bmp)
{
	char p[CFG_STR * 2];
	bool ok = shot_keep(bmp);

	unlink(bmp);
	snprintf(p, sizeof p, "%s/notice.dtov", P_USERDATA);
	if (notice_render("Screenshot", ok ? "Saved" : "Couldn't save", p))
		plat_resident_line("OVERLAY\tpath=%s\tms=1200", p);
}

/* ---------- where you were ------------------------------------------------ */

/* Coming back to the shelf you left is worth four lines of file handling: the
 * launcher restarts after every game, and starting at the beginning of the
 * list every time would undo the point of it being quick. */
static void remember_place(app *a)
{
	char p[CFG_STR * 2];
	sysview *v = &a->view[a->sys_cursor];
	FILE *f;
	snprintf(p, sizeof p, "%s/.last", P_ROOT);
	f = atomic_open(p, 0644);
	if (!f) return;
	fprintf(f, "%s\n%s\n", a->sys.systems[a->sys_cursor].tag,
	        v->list.count ? v->list.items[v->cursor].file : "");
	atomic_commit(f, p);
}

/* Whether a game was running when this process last stopped.
 *
 * `.last` says what you were LOOKING at, which is not the same question - it
 * is written when the shelf moves as well as when a game starts, and it says
 * nothing about whether that game was still up. This is written at launch and
 * cleared only on a clean return to the shelf, so a power-off mid-game and a
 * quit are distinguishable, which is the whole point.
 *
 * Beside `.last` and in the same shape, because it is the same two facts. */
static void playing_path(char *out, size_t n)
{
	snprintf(out, n, "%s/.playing", P_ROOT);
}

static void playing_set(app *a)
{
	char p[CFG_STR * 2];
	sysview *v = &a->view[a->sys_cursor];
	int o = shelf_owner(a, a->sys_cursor, v->cursor);
	FILE *f;

	if (v->list.count == 0) return;
	playing_path(p, sizeof p);
	f = atomic_open(p, 0644);
	if (!f) return;
	/* The GAME's system, not the shelf's - launching from Favorites otherwise
	 * records a system that does not own the ROM, exactly as the launch path
	 * itself had to learn. */
	fprintf(f, "%s\n%s\n", a->sys.systems[o].tag, v->list.items[v->cursor].file);
	atomic_commit(f, p);
}

static void playing_clear(void)
{
	char p[CFG_STR * 2];

	playing_path(p, sizeof p);
	remove(p);
}

/* Point the cursor at whatever was being played, if anything was, and say so.
 * Only when there is a state to come back to: without one, "resume" would
 * mean starting the game from its title screen, which is not what anyone
 * powering off mid-game is asking for. */
static bool playing_restore(app *a)
{
	char p[CFG_STR * 2], tag[64] = { 0 }, file[LIB_PATH] = { 0 }, st[LIB_PATH * 2];
	FILE *f;
	int i, k;

	playing_path(p, sizeof p);
	f = fopen(p, "r");
	if (!f) return false;
	if (fgets(tag, sizeof tag, f)) tag[strcspn(tag, "\r\n")] = 0;
	if (fgets(file, sizeof file, f)) file[strcspn(file, "\r\n")] = 0;
	fclose(f);
	if (!tag[0] || !file[0]) { playing_clear(); return false; }

	for (i = 0; i < a->sys.count; i++) {
		if (strcmp(a->sys.systems[i].tag, tag) != 0) continue;
		for (k = 0; k < a->view[i].list.count; k++) {
			if (strcmp(a->view[i].list.items[k].file, file) != 0) continue;

			state_path(a, i, &a->view[i].list.items[k], st, sizeof st);
			if (access(st, R_OK) != 0) {
				/* The game is gone from under the state, or the autosave
				 * never landed. Clear the marker rather than trying every
				 * boot from here on. */
				playing_clear();
				return false;
			}
			a->sys_cursor = i;
			a->view[i].cursor = k;
			return true;
		}
	}
	playing_clear();                  /* the ROM or its system is no longer here */
	return false;
}

static void restore_place(app *a)
{
	char p[CFG_STR * 2], tag[64] = { 0 }, file[LIB_PATH] = { 0 };
	FILE *f;
	snprintf(p, sizeof p, "%s/.last", P_ROOT);
	f = fopen(p, "r");
	if (!f) return;
	if (fgets(tag, sizeof tag, f)) tag[strcspn(tag, "\r\n")] = 0;
	if (fgets(file, sizeof file, f)) file[strcspn(file, "\r\n")] = 0;
	fclose(f);
	for (int i = 0; i < a->sys.count; i++) {
		if (strcmp(a->sys.systems[i].tag, tag) != 0) continue;
		a->sys_cursor = i;
		for (int k = 0; k < a->view[i].list.count; k++)
			if (strcmp(a->view[i].list.items[k].file, file) == 0) {
				a->view[i].cursor = k;
				break;
			}
		return;
	}
}

/* ---------- drawing ------------------------------------------------------- */

/* A heart, as a fan over the classic parametric curve:
 *
 *     x = 16 sin^3 t
 *     y = 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t
 *
 * Mirrors markdef.heart(), which is the definition - tools/genfavcard.py draws
 * the same shape into res/cards/FAVORITES.png, and the two are meant to match.
 * Change one, change both. Same standing arrangement as draw_shell and CELLS.
 *
 * Fanned from a point BELOW the center rather than from the center itself. The
 * notch between the lobes is the one concave part of the shape, and a fan
 * apex level with it produces slivers that cross the notch and fill it in.
 * Dropping the apex puts every boundary point in view of it. Same primitive as
 * draw_hex for the same reason: SDL has no polygon fill.
 */
#define HEART_N 48

static void draw_heart(SDL_Renderer *r, float cx, float cy, float rad,
                       SDL_Color col)
{
	SDL_Vertex v[HEART_N + 1];
	int idx[HEART_N * 3], i;

	v[0].position.x = cx;
	v[0].position.y = cy + rad * 0.25f;
	for (i = 0; i < HEART_N; i++) {
		float t = (float)(2.0 * M_PI * i / HEART_N);
		float x = 16.0f * powf(sinf(t), 3.0f);
		float y = 13.0f * cosf(t) - 5.0f * cosf(2 * t)
		          - 2.0f * cosf(3 * t) - cosf(4 * t);

		/* The curve spans 32 wide and 30 tall; halving the wider axis makes
		 * `rad` a radius in the same sense the star's was. */
		v[i + 1].position.x = cx + x * rad / 16.0f;
		v[i + 1].position.y = cy - y * rad / 16.0f;
	}
	for (i = 0; i <= HEART_N; i++) {
		v[i].color = col;
		v[i].tex_coord.x = v[i].tex_coord.y = 0;
	}
	for (i = 0; i < HEART_N; i++) {
		idx[i * 3 + 0] = 0;
		idx[i * 3 + 1] = 1 + i;
		idx[i * 3 + 2] = 1 + (i + 1) % HEART_N;
	}
	SDL_RenderGeometry(r, NULL, v, HEART_N + 1, idx, HEART_N * 3);
}

/* ChaOS's logo (src/chaos_mark.h, from tools/genchaosmotion.py) as squares,
 * each `out` of the way along its path out (0 in place, 1 gone), at `alpha`,
 * scaled by `k` about the screen's centre and moved by (ox, oy). */
static void draw_chaos_mark(SDL_Renderer *r, float out, Uint8 alpha, float k,
                            float ox, float oy)
{
	const float cx = TORTOS_SCREEN_W / 2.0f, cy = TORTOS_SCREEN_H / 2.0f;
	int i;

	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	for (i = 0; i < CHAOS_NCELLS; i++) {
		const chaos_cell *c = &CHAOS_CELLS[i];
		float d = c->dist * out;
		SDL_FRect q;

		q.x = cx + ((float)c->x + c->dx * d - cx) * k + ox;
		q.y = cy + ((float)c->y + c->dy * d - cy) * k + oy;
		q.w = q.h = CHAOS_CELL * k;
		SDL_SetRenderDrawColor(r, c->r, c->g, c->b, alpha);
		SDL_RenderFillRectF(r, &q);
	}
}

/* Whether a reading off the charger is at or below a point: by voltage where
 * the device has one, by the gauge's level where it doesn't or where the
 * voltage can't be read. The voltage it went by is left in *mv, 0 if none. */
static bool battery_at_or_below(int pct, int pct_point, int mv_point, int *mv)
{
	*mv = 0;
	if (mv_point > 0 && plat_battery_mv(mv)) return *mv <= mv_point;
	return pct <= pct_point;
}

/* 0 when the battery is fine or charging, 1 when it is low, 2 when it is
 * critical. Read every five seconds. */
static int battery_level(void)
{
	static Uint32 next_check;
	static int level;
	Uint32 now = SDL_GetTicks();
	if (next_check == 0 || now >= next_check) {
		next_check = now + 5000;
		int pct, mv;
		bool charging;
		int was = level, point = plat_battery_critical_mv();

		level = 0;
		/* Once it blinks by voltage it goes on blinking: the voltage can come
		 * back over the point for a reading as the load changes. */
		if (plat_battery(&pct, &charging) && !charging) {
			if ((was == 2 && point > 0) || battery_at_or_below(pct, BATT_CRITICAL_PCT, point, &mv))
				level = 2;
			else if (pct <= BATT_LOW_PCT)
				level = 1;
		}
	}
	return level;
}


/* Too low to go on, cached as battery_level is. Asked by every screen through
 * idle_due, and by the game's wait loop (plat_resident_on_battery_empty), so
 * an empty battery ends things the way the power button does: a game saves
 * on its way out, and power_off follows. Twice in a row, five seconds apart,
 * so one stray reading never powers the device off. */
static bool battery_empty(void)
{
	static unsigned last;
	static int      seen;
	static bool     primed;
	unsigned now = plat_now_ms();

	if (!primed || now - last >= 5000) {
		int  pct, mv = 0;
		bool charging;

		primed = true;
		last = now;
		if (plat_battery(&pct, &charging) && !charging &&
		    battery_at_or_below(pct, BATT_EMPTY_PCT, plat_battery_empty_mv(), &mv))
			seen++;
		else
			seen = 0;
		if (seen == 2 && mv > 0)
			fprintf(stderr, "battery: %d mV, empty; powering off\n", mv);
		else if (seen == 2)
			fprintf(stderr, "battery: %d%%, empty; powering off\n", pct);
	}
	return seen >= 2;
}

/* On the charger, cached. Both halves of Auto Off ask this - the shelf every
 * frame, the in-game tick ten times a second - and every call is two sysfs
 * files opened, read and closed. Two seconds is well inside the shortest
 * timeout anyone can set, so the lag is not observable. Same trick and the
 * same reasoning as battery_level() above.
 *
 * plat_battery leaves its out-parameter alone when it fails, so the answer is
 * written before the call rather than after it. */
static bool on_charger(void)
{
	static unsigned last;
	static bool     charging, primed;
	unsigned now = plat_now_ms();

	/* Elapsed, not a deadline compare: unsigned subtraction is right across
	 * the wrap and `now >= next_check` is not. */
	if (!primed || now - last >= 2000) {
		primed = true;
		last = now;
		charging = false;
		plat_battery(NULL, &charging);
	}
	return charging;
}

/* Has the player been away long enough to power the device off?
 *
 * The gathering half; the policy is idle_check, in src/idle.c, where a check
 * can reach it. This part is the three things only the launcher knows: the
 * setting, whether any button is down, and whether the charger is in.
 *
 * Every screen the launcher draws polls input in its own loop, and Auto Off
 * lived in exactly one of them: the shelf. The other seven simply held the
 * device on - both menus, Wi-Fi, About, Cheevos, the slot strip and the
 * keyboard. A menu left open is not evidence that somebody is there, it is
 * evidence somebody WAS; pausing because something else came up is the most
 * ordinary way there is to walk away from a device still switched on.
 *
 * Called beside each loop's existing power-button check, because that is what
 * the answer means. A device left alone does what the power button does -
 * already the rule for Diatom's IDLE during a game. `grep -n idle_due` is the
 * list of screens that honor it, and it should have no gaps. */
static bool idle_due(app *a)
{
	int b;

	if (battery_empty()) return true;
	a->idle.seconds = a->auto_off;
	for (b = 0; b < IN_COUNT; b++)
		if (a->in.pressed[b] || a->in.down[b]) break;

	/* Music playing HOLDS the clock the way the charger does, rather than
	 * counting as input: an album is somebody using the device with nobody
	 * touching it, and when it stops the countdown starts fresh. */
	return idle_check(&a->idle, plat_now_ms(), b < IN_COUNT,
	                  on_charger() || musec_playing());
}

/* The keyboard takes callbacks rather than an app - it is deliberately
 * general, and knows nothing about shelves or power policy. */
static bool idle_due_ctx(void *ctx) { return idle_due((app *)ctx); }

/* The one piece of chrome: a small accent disc, top right, when the battery is
 * low. Drawn with horizontal spans -- SDL has no circle. */
static void draw_low_battery_dot(SDL_Renderer *r, int cx, int cy);
static void redraw_at(Uint32 t);   /* below, with the rest of the redraw clock */
static void draw_upd_notice(app *a);   /* ChaOS: with the Updates screen */
static bool rewind_load(const char *tag);   /* ChaOS: with Button Mapping */
static int  menu_shelf_width(app *a);
static void upd_poll(void);

/* The battery as the status bar reads it, asked at most every five seconds,
 * since the sysfs read is not free and a shelf draws every frame it moves. */
static bool battery_state(int *pct, bool *charging)
{
	static Uint32 next_check;
	static int got_pct = -1;
	static bool got_chg;
	Uint32 now = SDL_GetTicks();

	if (next_check == 0 || now >= next_check) {
		int p = -1;
		bool c = false;

		next_check = now + 5000;
		got_pct = plat_battery(&p, &c) ? p : -1;
		got_chg = c;
	}
	*pct = got_pct;
	*charging = got_chg;
	return got_pct >= 0;
}

/* The low-battery dot on its own, for the screens with no status line of their
 * own: the shelf and Muse. Steady when low, blinking when critical, a second on
 * and a second off, slow enough not to pull at the eye. Returns how long until
 * it changes by itself, in ms (-1 never), so a screen that only draws when
 * something moves still blinks it. Diatom blinks its own over a game on the
 * same rhythm. */
static unsigned draw_low_battery_chrome(SDL_Renderer *r)
{
	Uint32 now, flip;
	int level = battery_level();

	if (level == 0) return (unsigned)-1;
	if (level == 1) { draw_low_battery_dot(r, TORTOS_SCREEN_W - 34, 34); return (unsigned)-1; }
	now = plat_now_ms();
	flip = 1000 - now % 1000;
	redraw_at(now + flip);
	if ((now / 1000) % 2 == 0) draw_low_battery_dot(r, TORTOS_SCREEN_W - 34, 34);
	return flip;
}

/* The status line in a menu's heading band: the clock from `left`, the battery
 * ending at `right`, both centred on the heading's line at y. Anything that
 * would run into the heading, between head_l and head_r, is left out rather
 * than drawn over it. The low-battery dot sits beside the battery, or at the
 * right edge when no battery is drawn. */
static void draw_menu_status(SDL_Renderer *r, int left, int right, int y,
                             int head_l, int head_r)
{
	TTF_Font *f = ui_font(UI_F_META);
	int mid = y + ui_font_height(UI_F_META) / 2;
	int dot_x = right - 9;
	int level = battery_level();

	if (g_status_clock) {
		char s[16];
		char *pm;

		clock_hhmm(time(NULL), g_status_clock24, s, sizeof s);
		/* Beside a long heading ("Date & Time") "3:39 PM" may not fit where
		 * "15:39" does, and a clock that vanished in 12-hour time read as the
		 * setting hiding it. Shorter rather than gone: "3:39PM", then "3:39". */
		pm = strchr(s, ' ');
		if (pm && left + ui_text_width(f, s) > head_l) memmove(pm, pm + 1, strlen(pm));
		if (pm && left + ui_text_width(f, s) > head_l) *pm = '\0';
		if (left + ui_text_width(f, s) <= head_l)
			ui_text(r, f, s, left, y, -1, UI_TEXT_DIM);
	}

	if (g_status_batt != STATUS_BATT_OFF) {
		int pct = -1;
		bool charging = false;

		if (battery_state(&pct, &charging)) {
			bool icon  = g_status_batt == STATUS_BATT_ICON || g_status_batt == STATUS_BATT_BOTH;
			bool words = g_status_batt == STATUS_BATT_PCT || g_status_batt == STATUS_BATT_BOTH;
			char s[12] = "";
			/* The icon as tall as the numbers beside it (a digit's height
			 * is about 0.72 of the font's ascent), and twice as wide, with a
			 * nub a quarter of its height */
			int bh = TTF_FontAscent(f) * 72 / 100;
			int bw = bh * 2, nub = bh / 4 > 3 ? bh / 4 : 3;
			int tw = 0, gap = icon && words ? bh / 2 : 0;
			int iw = icon ? bw + nub : 0;
			int total, ix;
			SDL_Color fill = pct <= BATT_LOW_PCT ? (SDL_Color){ 224, 72, 72, 255 }
			               : charging ? (SDL_Color){ 120, 200, 120, 255 }
			               : UI_TEXT_SOFT;

			if (words) {
				snprintf(s, sizeof s, "%d%%", pct);
				tw = ui_text_width(f, s);
			}
			total = tw + gap + iw;
			if (total > 0 && right - total > head_r) {
				ix = words ? right - tw - gap : right;
				if (words) ui_text(r, f, s, right, y, 1, UI_TEXT_DIM);
				if (icon) {
					/* A 2px outline and a 2px gap round the fill, as the
					 * text's strokes are about that thick */
					int inner = bw - 8;
					int w = inner * pct / 100, bx = ix - nub - bw;
					SDL_Rect body = { bx, mid - bh / 2, bw, bh };
					SDL_Rect body2 = { bx + 1, mid - bh / 2 + 1, bw - 2, bh - 2 };
					SDL_Rect cap  = { bx + bw, mid - bh / 4, nub, bh / 2 };
					SDL_Rect in   = { bx + 4, mid - bh / 2 + 4, w, bh - 8 };

					SDL_SetRenderDrawColor(r, UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b, 255);
					SDL_RenderDrawRect(r, &body);
					SDL_RenderDrawRect(r, &body2);
					SDL_RenderFillRect(r, &cap);
					SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, 255);
					if (w > 0) SDL_RenderFillRect(r, &in);
					dot_x = bx - 18;
				} else {
					dot_x = right - tw - 18;
				}
			}
		}
	}

	if (level == 1 || (level == 2 && (plat_now_ms() / 1000) % 2 == 0))
		draw_low_battery_dot(r, dot_x, mid);
}

static void draw_low_battery_dot(SDL_Renderer *r, int cx, int cy)
{
	int rad = 9;
	SDL_SetRenderDrawColor(r, 224, 72, 72, 255);
	for (int dy = -rad; dy <= rad; dy++) {
		int dx = (int)(sqrt((double)(rad * rad - dy * dy)) + 0.5);
		SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
	}
}

/* ---------- when the next frame is needed -------------------------------- */

/* The earliest moment something already on screen will look different with
 * nobody touching anything - a card mid-move, the tint still easing toward a
 * new system, a long title about to scroll, the volume line about to go.
 * REDRAW_NEVER means nothing drawn in the last frame changes by itself.
 *
 * Gathered WHILE drawing, because the things that move are the things that
 * know they are moving: cf_draw already returns whether a move is in flight,
 * and a marquee is the only thing that knows where it is in its hold. render
 * resets it and every drawing path that animates pulls it earlier. The main
 * loop then only draws when it has come due, or when there was input.
 *
 * It exists because the shelf was redrawn and presented at the refresh rate
 * whether or not anything had changed. Measured on the Brick 2026-09-13, idle
 * on a shelf: 26.6% of a core and 88 voluntary context switches a second, all
 * of it the main thread, for a picture that was not moving. */
#define REDRAW_NEVER UINT32_MAX
static Uint32 g_redraw_at = 0;

static void redraw_at(Uint32 t) { if (t < g_redraw_at) g_redraw_at = t; }
static void redraw_now(void)    { g_redraw_at = 0; }

static void draw_background(app *a)
{
	SDL_Renderer *r = a->r;
	SDL_SetRenderDrawColor(r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(r);
	/* A wash of the focused system's color along the bottom edge, so the
	 * whole screen belongs to the machine you are looking at. Faint enough to
	 * read as light rather than as a panel. */
	{
		SDL_Rect band = { 0, TORTOS_SCREEN_H - 240, TORTOS_SCREEN_W, 480 };
		ui_glow(r, &band, a->tint, 34, 1.7f);
	}
}

/* How fast a shelf animates, written every frame from what is about to be
 * drawn rather than left behind by whatever drew last.
 *
 * These live on the coverflow so step_anim can read them without knowing
 * which mode is running, and that means they PERSIST. Only the cube set them,
 * nothing set them back, and a horizontal row kept the cube's slower, gentler
 * turn for the rest of the session after a single visit to vertical. State
 * that one of two paths writes is state the other path has to write too.
 *
 * The cube gets 450ms and a curve that starts from rest: it turns a right
 * angle of whole screen, and at the row's 240ms of cubic ease-out the first
 * frame alone covers 17.5 of the 90 degrees, which does not read as turning.
 * The row keeps its own 240ms - anim_ms of 0 means the default - because a
 * card step should feel immediate and 17.5 degrees has no meaning for it. */
static void shelf_pacing(coverflow *cf)
{
	/* `.both`, not `.vertical`. Vertical lays out the same row Horizontal
	 * does and wants the row's pacing; only Cubic still turns a cube, and the
	 * slower smoothed chase is what a turning solid needs. The two share the
	 * `.vertical` flag because they share an INPUT axis, which is a different
	 * question from how they are drawn. */
	if (CARD_DIRS[g_dir].both) {
		cf->anim_ms = 450.0f;
		cf->ease = CF_EASE_SMOOTH;
		cf->chase = true;
		cf->glide = 0.0f;      /* chase already holds pos within a step */
	} else if (CARD_DIRS[g_dir].vertical) {
		/* THE DURATION IS FIXED BUT THE DISTANCE IS NOT. ANIM_MS is 240ms
		 * whatever the travel, and Vertical shows one card at a time, so a
		 * step moves 878px where the horizontal row moves 400. At the shared
		 * default that is 2.2x the speed, which reads as too fast AND as
		 * jerky - the frame rate is unchanged, so covering twice the ground
		 * means half as many frames per pixel.
		 *
		 * 360ms is 2.4 px/ms against the horizontal row's 1.7 - deliberately
		 * quicker than matching, because one card filling the screen carries
		 * no neighbors to read on the way past and the travel is dead time.
		 * 420 matched the row exactly and felt sluggish. Smooth rather than
		 * out-cubic because out-cubic spends its speed at the start, and over
		 * a throw this long that is a lurch before the settle. */
		cf->anim_ms = 360.0f;
		cf->ease = CF_EASE_SMOOTH;
		cf->chase = false;
		/* Two, not the row's eight. Vertical puts ONE card on the screen at a
		 * time, so a card of travel is a whole screen of travel; eight would
		 * be a full-screen blur rather than a departure. */
		cf->glide = 2.0f;
	} else {
		cf->anim_ms = 0.0f;
		cf->ease = CF_EASE_OUT_CUBIC;
		cf->chase = false;
		cf->glide = (float)CF_WARM_CARDS;
	}
}

/* A systems layout carrying the theme's reflection gap.
 *
 * The gap belongs to the art, not to the row: a drawn card rests on the
 * reflective surface and a photographed console floats above it, and one
 * layout draws both. Only system art gets it - box art is card-like whatever
 * the theme is, so the games shelf sits flush. */
static cf_layout sys_layout(const cf_layout *base)
{
	cf_layout lay = *base;

	lay.reflect_gap = CARD_SETS[g_cards].reflect_gap;
	return lay;
}


/* Both: one surface, two axes. Up and down turn to another system, left and
 * right move through that system's games. There is no entering and no going
 * back, because what you are looking at is already the thing you can act on.
 *
 * Every face is a game, on both axes. A vertical turn shows the game you were
 * last on in the system you are leaving, turning into the game you were last
 * on in the one arriving - sysview keeps a cursor per system, so that is free
 * and it means glancing at another system costs you nothing. */
static void draw_games_face(app *a, sysview *v, const system_cfg *s, int idx);

/* Render another system's face.
 *
 * game_get_tex reads a->sys_cursor rather than the view it is handed, so a
 * face drawn for a system that is not the current one would fetch its art
 * from the wrong shelf. Swapped around the call, the way prime_around already
 * does it, rather than rethreading the cursor through every getter. */
static void draw_face_for(app *a, int sys)
{
	int save = a->sys_cursor;

	a->sys_cursor = sys;
	draw_games_face(a, &a->view[sys], &a->sys.systems[sys],
	                a->view[sys].cursor);
	a->sys_cursor = save;
}

/* The systems rail's strip: one band per system, in shelf order.
 *
 * Only the systems rails get one. A games rail's items are all one system's
 * games and carry that system's accent, so a strip of them would be a single
 * color with the cost of asking per pixel - those pass NULL and stay one fill.
 *
 * Bounded rather than trusted: ui_rail walks the ring modulo `count`, which is
 * this array's length, but a rail drawn with a stale count during a rescan
 * would index past it. */
static unsigned rail_sys_hue(void *ctx, int i)
{
	app *a = ctx;

	if (i < 0 || i >= a->sys.count) return 0;
	return a->sys.systems[i].accent;
}

static void draw_both(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	const system_cfg *s = &a->sys.systems[a->sys_cursor];
	int ns = a->sys.count, ng, plan;
	SDL_Texture *fa, *fb;
	float spos, sfrac;
	int s0, s1;

	if (ns <= 0) return;
	ng = v->list.count;

	/* Both coverflows, every frame, here rather than in the callers: this
	 * screen drives two axes and the app still thinks it is on one shelf or
	 * the other, so pacing applied by screen would leave whichever axis is
	 * not "current" without its chase clamp and its curve. */
	shelf_pacing(&a->cf_sys);
	shelf_pacing(&v->cf);

	/* A coverflow that has never been used jumps rather than animating the
	 * first time it is told to move - cf_set_cursor_dir resets an unprimed
	 * one and returns. Every system owns its own, and Both never enters a
	 * system, so without this the first game move after each system switch
	 * is a cut instead of a turn, and it fixes itself only once you have
	 * visited them all. */
	if (!v->cf.primed) cf_reset(&v->cf, v->cursor);

	cf_tick(&a->cf_sys, ns);
	if (ng > 0) cf_tick(&v->cf, ng);
	/* After the ticks, so a turn that finished this frame is not asked to
	 * draw again: this frame already shows where it came to rest. */
	if (a->cf_sys.active || (ng > 0 && v->cf.active)) redraw_now();

	spos = a->cf_sys.pos;
	s0 = (int)floorf(spos);
	sfrac = spos - (float)s0;
	s0 = ((s0 % ns) + ns) % ns;
	s1 = ns > 1 ? (s0 + 1) % ns : s0;

	fa = face_tex(a, 0);
	fb = face_tex(a, 1);

	/* Asks whether a turn is IN FLIGHT, not whether it has visibly moved yet.
	 * The cursor changes on the frame of the press but `pos` has not left
	 * yet, so a frac test sends that first frame down the at-rest path and
	 * draws the DESTINATION flat, one frame, before the turn starts - the
	 * arriving item flashing up before the one being left rotates away. At
	 * frac 0 the cube already draws the near face square on, which is the
	 * item being left, so there is nothing special about the first frame. */
	if (ns > 1 && a->cf_sys.active) {
		if (!fa || !fb) { draw_face_for(a, s0); goto furniture; }
		plan = faces_plan(2, 0, s0, s1);
		if (plan == FACES_NEAR_ONLY || plan == FACES_FAR_ONLY)
			faces_swap(&fa, &fb);
		if (plan == FACES_BOTH || plan == FACES_NEAR_ONLY) {
			SDL_SetRenderTarget(a->r, fa);
			draw_face_for(a, s0);
			SDL_RenderFlush(a->r);
		}
		if (plan == FACES_BOTH || plan == FACES_FAR_ONLY) {
			SDL_SetRenderTarget(a->r, fb);
			draw_face_for(a, s1);
			SDL_RenderFlush(a->r);
		}
		if (plan != FACES_NONE) plat_draw_to_screen();
		cf_draw_cube(a->r, fa, fb, sfrac, TORTOS_SCREEN_W, TORTOS_SCREEN_H,
		             false, a->sys.systems[s0].accent,
		             a->sys.systems[s1].accent);
		goto furniture;
	}

	/* Otherwise the games axis: yaw. */
	if (ng <= 0) { draw_games_face(a, v, s, 0); goto furniture; }
	{
		float gpos = v->cf.pos, gfrac;
		int g0, g1, k;

		g0 = (int)floorf(gpos);
		gfrac = gpos - (float)g0;
		g0 = ((g0 % ng) + ng) % ng;
		g1 = ng > 1 ? (g0 + 1) % ng : g0;

		for (k = -CF_HALF_WINDOW; k <= CF_HALF_WINDOW; k++) {
			int tw = 0, th = 0;
			game_get_tex(a, ((v->cursor + k) % ng + ng) % ng, &tw, &th, NULL);
		}
		if (!v->cf.active || ng < 2 || !fa || !fb) {
			faces_stale();
			draw_games_face(a, v, s, g0);
		} else {
			plan = faces_plan(3, a->sys_cursor, g0, g1);
			if (plan == FACES_NEAR_ONLY || plan == FACES_FAR_ONLY)
				faces_swap(&fa, &fb);
			if (plan == FACES_BOTH || plan == FACES_NEAR_ONLY) {
				SDL_SetRenderTarget(a->r, fa);
				draw_games_face(a, v, s, g0);
				SDL_RenderFlush(a->r);
			}
			if (plan == FACES_BOTH || plan == FACES_FAR_ONLY) {
				SDL_SetRenderTarget(a->r, fb);
				draw_games_face(a, v, s, g1);
				SDL_RenderFlush(a->r);
			}
			if (plan != FACES_NONE) plat_draw_to_screen();
			cf_draw_cube(a->r, fa, fb, gfrac, TORTOS_SCREEN_W,
			             TORTOS_SCREEN_H, true,
			             a->sys.systems[a->sys_cursor].accent,
			             a->sys.systems[a->sys_cursor].accent);
		}
		evict_far(v, TEX_KEEP_NEAR);
	}

furniture:
	/* The ChaOS pixel themes: no face is a console here, so after a turn to a
	 * system its name flies together over the game, stays a moment, and
	 * scatters again - at once if the games start turning. */
	if (CARDS_DRAWN(g_cards)) {
		static int shown = -1;
		static Uint32 at, leave;
		Uint32 now = SDL_GetTicks();
		float t = 0.0f;

		if (a->cf_sys.active) shown = -1;
		else if (shown != a->sys_cursor) { shown = a->sys_cursor; at = now; leave = 0; }
		if (shown >= 0) {
			Uint32 el = now - at;
			const Uint32 form = (Uint32)PN_SPEEDS[g_pn_speed[g_dir]].form;
			const Uint32 hold = (Uint32)PN_SPEEDS[g_pn_speed[g_dir]].hold;
			if (form == 0) {
				/* Static: the name is there at once, and gone at once */
				t = 1.0f;
				if (!leave && ((ng > 0 && v->cf.active) || el >= hold)) leave = now;
				if (leave) t = 0.0f;
			} else {
				t = el >= form ? 1.0f : (float)el / (float)form;
				if (!leave && ((ng > 0 && v->cf.active) || el >= form + hold)) leave = now;
				if (leave) {
					float k = 1.0f - (float)(now - leave) / (float)form;
					if (k < t) t = k;
				}
			}
			if (pn_fixed() > -2.0f) t = 1.0f;          /* --pixnames: formed */
			if (t > 0.0f) {
				pn_overlay(a->r, s->name, s->accent, t, TORTOS_SCREEN_W, TORTOS_SCREEN_H);
				redraw_now();
			}
		}
	}
	/* Two rails, because there are two positions to be in. The system rail
	 * runs down the left on its own axis; the game rail sits under the screen
	 * where it always has. Both from `pos` rather than the cursor, so they
	 * travel with the face that is turning instead of arriving before it. */
	ui_rail_v(a->r, TORTOS_SCREEN_W, TORTOS_SCREEN_H, a->cf_sys.pos,
	          a->sys.count, s->accent, rail_sys_hue, a);
	if (ng > 1)
		ui_rail(a->r, TORTOS_SCREEN_W, TORTOS_SCREEN_H, v->cf.pos, ng,
		        s->accent, NULL, NULL);
	/* What system, and how much of it. Both are context rather than content:
	 * left and right change neither, so neither rides a yawing face. Nothing
	 * else names the system here - every face is a game - so the name is
	 * always drawn, and the position sits opposite it because the pair reads
	 * as one line about the shelf rather than two labels. It says the same
	 * thing the game rail does, in numbers rather than as a length - the
	 * rail is glanceable and the number is exact. */
	{
		char nfit[192], cnt[64];
		int w;

		snprintf(cnt, sizeof cnt, "%d / %d", ng > 0 ? v->cursor + 1 : 0, ng);
		ui_text(a->r, ui_font(UI_F_META), cnt, 24, 700, -1, UI_TEXT_DIM);

		/* THE NAME GETS WHAT THE COUNT LEAVES, rather than a constant 360.
		 * This is the only place Cubic says which system you are on - no face
		 * ever shows a console - so a truncated name there is the one label
		 * that cannot be checked against anything else on screen. "Neo Geo
		 * Pocket Color" did not fit in 360 and read as "Neo Geo Pocket Co...",
		 * which is also the name of a DIFFERENT shelf two steps away.
		 *
		 * A constant was always going to be wrong for some pairing of a long
		 * name and a four-digit count; drawing at one larger text size from
		 * 2026-09-16 made it wrong for a name we ship. 40px is the clear air
		 * between the two, so they never read as one string. */
		{
			int cw = ui_text_width(ui_font(UI_F_META), cnt);
			int room = TORTOS_SCREEN_W - 24 - (24 + cw + 40);

			ui_fit_text(ui_font(UI_F_META), s->name, nfit, sizeof nfit, room);
		}
		w = ui_text_width(ui_font(UI_F_META), nfit);
		ui_text(a->r, ui_font(UI_F_META), nfit,
		        TORTOS_SCREEN_W - 24 - w, 700, -1, UI_TEXT_DIM);
	}
}

static void draw_systems(app *a)
{
	shelf_pacing(&a->cf_sys);
	if (CARD_DIRS[g_dir].both) { draw_both(a); return; }

	SDL_Rect focus;
	const system_cfg *s = &a->sys.systems[a->sys_cursor];
	char line[128];

	/* Systems only. The games shelf keeps the angled row whatever this says:
	 * box art is a wall of many, and one cover per screen would turn picking
	 * a game into paging through a catalog. */
	/* Vertical stacks the same row down the screen. Cubic is excluded: it
	 * does not draw this row at all. */
	cf_layout lay = sys_layout(CARD_DIRS[g_dir].vertical
	                           ? &CF_LAYOUT_SYSTEMS_V : &CF_LAYOUT_SYSTEMS);

	if (CARDS_BIG(g_cards)) {
		/* ChaOS Pixel Names: no cards - the name under the cursor flies
		 * together large in the middle of the screen as the one left
		 * scatters. The shelf still moves underneath, unseen, for the rail
		 * and for everything that reads where it is. */
		int i;
		cf_tick(&a->cf_sys, a->sys.count);
		if (a->cf_sys.active) redraw_now();
		pn_speed(PN_SPEEDS[g_pn_speed[g_dir]].form);
		for (i = 0; i < a->sys.count; i++)
			if (i != a->sys_cursor)
				pn_big(a->r, a->sys.systems[i].name, a->sys.systems[i].accent, false,
				       TORTOS_SCREEN_W, TORTOS_SCREEN_H, TORTOS_SCREEN_H * 44 / 100);
		pn_big(a->r, s->name, s->accent, true,
		       TORTOS_SCREEN_W, TORTOS_SCREEN_H, TORTOS_SCREEN_H * 44 / 100);
	} else {
	cf_focus_rect(&lay, TORTOS_SCREEN_W, TORTOS_SCREEN_H, &focus);
	ui_glow(a->r, &focus, s->accent, 110, 2.4f);
	if (cf_draw(&a->cf_sys, a->r, TORTOS_SCREEN_W, TORTOS_SCREEN_H, a->sys.count,
	            sys_get_tex, a, &lay))
		redraw_now();
	}
	/* ChaOS Pixel Cards: a name still forming or scattering */
	if (pn_animating()) redraw_now();

	/* WHICH SYSTEM THE WORDS BELOW DESCRIBE, and how visible they are.
	 *
	 * Vertical crossfades them: one card fills the screen there, so a name
	 * that changed the instant the button went down sat under a console that
	 * had not started moving - the only thing on screen ahead of the motion.
	 * cf_label answers both questions from the shelf's own position, so the
	 * name swaps while it is invisible and nothing has to remember the old
	 * one. The horizontal row keeps the cursor and the instant change: seven
	 * cards move at once there and the eye is on them, and fading the name
	 * would be a second thing happening for no reason.
	 *
	 * The count follows the name, not the cursor, or a move would show the
	 * old system's name over the new system's game count. */
	const system_cfg *label = s;
	float label_a = 1.0f;

	if (CARD_DIRS[g_dir].vertical) {
		int li = 0;

		label_a = cf_label(&a->cf_sys, a->sys.count, &li);
		if (li >= 0 && li < a->sys.count) label = &a->sys.systems[li];
	}

	/* Art that does not name itself gets named here, in the gap between the
	 * card and the count, which is where the classic cards carry it. */
	if (!CARD_SETS[g_cards].labeled) {
		char nfit[192];

		ui_fit_text(ui_font(UI_F_TITLE), label->name, nfit, sizeof nfit,
		            TORTOS_SCREEN_W - 48);
		/* 618 is not arbitrary: the title renders 52px tall, so it ends at
		 * 670 and leaves 20px before the count at 690. The card is sized to
		 * fit ABOVE this rather than this being pushed down to suit the
		 * card. */
		ui_text(a->r, ui_font(UI_F_TITLE), nfit, TORTOS_SCREEN_W / 2, 618, 0,
		        ui_fade(UI_TEXT, label_a));
	}

	if (is_muse(label)) {
		/* Muse has albums and books rather than games, each counted, and a
		 * kind the card has none of left out. */
		int albums = ml_count(&g_muse, false), books = ml_count(&g_muse, true);
		char al[32] = "", bk[32] = "";

		if (albums) snprintf(al, sizeof al, "%d album%s", albums, albums == 1 ? "" : "s");
		if (books)  snprintf(bk, sizeof bk, "%d book%s", books, books == 1 ? "" : "s");
		snprintf(line, sizeof line, "%s%s%s", al, al[0] && bk[0] ? ", " : "", bk);
	} else {
		const game_list *gl = &a->view[(int)(label - a->sys.systems)].list;
		/* ChaOS: PortMaster's own card isn't a port */
		int gc = gl->count - (gl->count > 0 && pm_is_entry(gl->items[0].file));

		/* A shelf of one read "1 games". Favorites hits it first because it
		 * starts empty and grows one at a time, but any system with a single
		 * ROM has always said it. */
		if (gc > 0 || !strcmp(label->core, PORTS_CORE))
			snprintf(line, sizeof line, "%d %s%s", gc,
			         !strcmp(label->core, PORTS_CORE) ? "port" : "game", gc == 1 ? "" : "s");
		else
			/* The folder is bounded by CFG_STR and this line is not, so
			 * the name is capped rather than the buffer grown: a folder
			 * long enough to overflow a sentence is not one anybody can
			 * read off a shelf either. The cross compiler is the only one
			 * that says so - clang does not. */
			snprintf(line, sizeof line, "no games in Roms/%.96s", label->folder);
	}
	ui_text(a->r, ui_font(UI_F_META), line, TORTOS_SCREEN_W / 2, 690, 0,
	        ui_fade(UI_TEXT_DIM, label_a));
	(CARD_DIRS[g_dir].vertical ? ui_rail_v : ui_rail)
		(a->r, TORTOS_SCREEN_W, TORTOS_SCREEN_H, a->cf_sys.pos, a->sys.count,
		 s->accent, rail_sys_hue, a);
}

/* Milliseconds since the focused game last changed.
 *
 * The marquee's clock, and it lives here rather than in ui.c because only this
 * knows what "the subject" is. Moving the cursor - or changing shelves - is a
 * new subject and starts the wait again, which is what keeps a scan quiet. */
static int shot_phase = -1;        /* --phase, for the shot harness only */

/* The marquee's clock. It restarts when the subject changes, so a newly chosen
 * name is read from its beginning rather than joined halfway through a scroll.
 *
 * A SLOT PER SUBJECT, not one clock. A menu draws over a live shelf and the
 * shelf asks for its phase every frame either way, so one shared clock would
 * be reset twice a frame by two different subjects and nothing would ever
 * move. Two keys because the shelf's subject is a pair - which system, and
 * which game within it - and either changing is a new subject.
 *
 * shot_phase freezes it at a chosen moment. A still of a moving thing needs
 * one; without it the only way to look at a marquee is to film it. That used
 * to be here and not in the menu's copy of this, which is what having two
 * copies costs. */
/* Two note slots because a panel can carry two footer notes and both may be
 * long enough to move. Sharing one would be the bug this enum exists to
 * prevent, a step smaller: each would see the other's key every frame and
 * neither would ever get past the opening hold. */
#define MQ_NOTES 2
/* MQ_FIXED is for rows that move because nothing can ever select them, as
 * distinct from the ones that move because something just did.
 *
 * The other slots are keyed on the cursor, which is right when the row that
 * scrolls is the row you landed on: a new selection is a new subject and its
 * text should start from the beginning. It is wrong for a fact. The game
 * screen's Genre row is unselectable and scrolls whenever it is long, and
 * stepping between Synopsis and Cheevos was restarting it - a cursor move that
 * has nothing to do with the row it interrupted. Reported 2026-09-17, the day
 * Genre became the first long permanently-unselectable row on a screen that
 * also has a cursor.
 *
 * Keyed on the SCREEN instead - its heading and how many rows it has - which
 * is the same value for every such row in a frame, so they still share one
 * clock and still move together rather than resetting each other.
 *
 * It replaced a slot of its own for VALUES, which was the wrong axis to split
 * on: a label and a value never move in the same row - the wider of the two
 * takes the room and the other is laid out around it - so what needed separate
 * clocks was never the two columns. It was the selected row against the rest,
 * and only one row is ever selected. */
enum { MQ_SHELF, MQ_MENU, MQ_FIXED, MQ_NOTE0, MQ_NOTE1, MQ_VSCROLL,
       MQ_HEAD, MQ_NP, MQ_DESC, MQ_SLOTS };

static int      mq_last_a[MQ_SLOTS], mq_last_b[MQ_SLOTS];
static unsigned mq_since[MQ_SLOTS];
static bool     mq_primed[MQ_SLOTS];

/* Start this slot's clock over on the next frame.
 *
 * The keys are what a slot normally restarts on - a new game, a new heading -
 * and they are the right rule for a marquee that lives on a screen you are
 * already looking at. They are the wrong rule for a screen you LEAVE and come
 * back to: the keys are identical, so the clock is not reset, and a synopsis
 * reopened resumed halfway down where it had been left. Reported 2026-09-17,
 * and the only way to read a long one from the top was to wait out the rest.
 *
 * So a screen that has just opened says so, rather than the clock trying to
 * infer it from a key that did not change. */
static void mq_reset(int who)
{
	if (who >= 0 && who < MQ_SLOTS) mq_primed[who] = false;
}

static unsigned mq_phase(int who, int key_a, int key_b)
{
	int      *last_a = mq_last_a, *last_b = mq_last_b;
	unsigned *since  = mq_since;
	bool     *primed = mq_primed;
	unsigned now = plat_now_ms();

	if (shot_phase >= 0) return (unsigned)shot_phase;

	if (!primed[who] || key_a != last_a[who] || key_b != last_b[who]) {
		primed[who] = true;
		last_a[who] = key_a;
		last_b[who] = key_b;
		since[who]  = now;
	}
	return now - since[who];
}

/* The clock a moving ROW runs on: the cursor's when the cursor put it in
 * motion, the screen's when nothing ever will. See MQ_FIXED.
 *
 * The heading is measured rather than compared, because mq_phase's keys are
 * two ints - and a length is enough to notice a different game, which is what
 * a new screen means here. */
static unsigned mq_row_phase(bool selected, const char *heading, int n, int sel)
{
	if (selected) return mq_phase(MQ_MENU, sel, 0);
	return mq_phase(MQ_FIXED, heading ? (int)strlen(heading) : 0, n);
}

static unsigned title_phase(app *a)
{
	return mq_phase(MQ_SHELF, a->sys_cursor, a->view[a->sys_cursor].cursor);
}

/* Everything a games shelf says about one game: its title, its favorite mark
 * and its place in the list. Split out so a cube face draws the same thing the
 * row does - the marquee's seat, the heart's fixed position and the reasoning
 * behind both are delicate enough that a second copy would drift. */
/* ChaOS: whether a game's card is drawn from its title - no box art, no
 * autosave picture - and so already says its name. Asked of the focused card
 * every frame, so remembered against the texture: a new one (a cover just
 * chosen, a game just played) asks again. */
static bool card_is_title(app *a, sysview *v, int idx)
{
	static const void *seen;
	static int seen_sys = -1, seen_idx = -1;
	static bool was;
	int o;
	char prev[LIB_PATH * 3];

	if (idx < 0 || idx >= v->list.count) return false;
	if (seen == v->tex[idx] && seen_sys == a->sys_cursor && seen_idx == idx) return was;
	o = shelf_owner(a, a->sys_cursor, idx);
	preview_path(a, o, &v->list.items[idx], prev, sizeof prev);
	was = !has_box_art(a->sys.systems[o].folder, v->list.items[idx].name) &&
	      access(prev, R_OK) != 0;
	seen = v->tex[idx];
	seen_sys = a->sys_cursor;
	seen_idx = idx;
	return was;
}

static void draw_game_text(app *a, sysview *v, const system_cfg *s, int idx)
{
	const system_cfg *gs = &a->sys.systems[
		shelf_owner(a, a->sys_cursor, idx)];
	char count[64];

	if (v->list.count > 0) {
		game_entry *g = &v->list.items[idx];
		/* A title that fits is centered, as before. One that does not slides,
		 * because truncating it destroys the rest of the name permanently and
		 * sliding it only delays it - see ui_text_marquee.
		 *
		 * THE HEART HAS A FIXED SEAT, far left, whether or not this game
		 * has one. It used to be placed off the title's own width, which put
		 * it somewhere new for every game and - once titles could slide -
		 * would have moved it at the moment a title started sliding. A mark
		 * that jumps when the thing beside it grows is not a mark, it is more
		 * motion.
		 *
		 * Its width is reserved on BOTH sides, so the box stays centered on the
		 * screen and a short title is centered exactly where it always was.
		 * The cost is a narrower box, so a few more titles slide; sliding is
		 * the thing that made a narrow box acceptable. */
		TTF_Font *ft2 = ui_font(UI_F_TITLE);
		int line = ui_font_line(UI_F_TITLE);
		int margin = line / 2;
		int hrad = line * 2 / 5;
		int lead = margin + hrad * 2 + hrad;      /* edge, heart, its gap */
		int boxx = lead;
		int boxw = TORTOS_SCREEN_W - lead * 2;
		int tw = ui_text_width(ft2, g->title);
		int tx = TORTOS_SCREEN_W / 2;
		bool slides = tw > boxw;
		/* Asked for EVERY frame, not only when something slides. It is a
		 * clock that has to watch the cursor to know when to restart, and it
		 * cannot watch it on frames nobody calls it. Reading it only inside
		 * the sliding branch meant a short title in between - which needs no
		 * marquee and so never called this - left the last long one still
		 * recorded as current; coming back to it was not a change, and it
		 * carried on mid-scroll instead of starting over. */
		unsigned phase = title_phase(a);
		/* The one mark left on a card.
		 *
		 * There was a second, a dot on the right, for a game with an autosave
		 * - A continues it rather than starting it. Removed 2026-08-30: it
		 * only ever drew beside the CENTERED title, so it was never the
		 * scan-the-shelf signal it looked like, and it said one bit with no
		 * legend about the one game you could already ask about directly. X
		 * says "resume + 3" now, which is the same fact with a number on it. */
		if (fav_is(gs->tag, g->file)) {
			/* Centered on the title's INK, not its em box. The box reserves a
			 * descender's depth most titles never use, so a mark placed at
			 * the box's middle sits visibly below the letters. Descent is
			 * negative, so half of it lifts. */
			int dy = 40 + line / 2 + (ft2 ? TTF_FontDescent(ft2) / 2 : 0);
			SDL_Color c = { (Uint8)(gs->accent >> 16), (Uint8)(gs->accent >> 8),
			                (Uint8)gs->accent, 255 };

			draw_heart(a->r, (float)(margin + hrad), (float)dy, (float)hrad, c);
		}
		/* ChaOS: the name once. A card drawn from the title already says
		 * it, so the line above is for a card with a picture on it. */
		if (!is_muse(s) && card_is_title(a, v, idx)) {
			/* nothing: the card is the title */
		} else if (slides) {
			ui_text_marquee(a->r, ft2, g->title, boxx, 40, boxw,
			                phase, UI_TEXT);
			/* Not "a title is sliding, so keep drawing". It ping-pongs for
			 * as long as the game is focused, with 1.4s of stillness at the
			 * start and 0.9s at the far end, so ask for the moment it next
			 * moves and sleep through the holds. The phase clock is
			 * plat_now_ms, the same one the main loop compares against.
			 *
			 * Only the holds can be skipped; the travel between them is
			 * motion and has to be drawn. Measured on the Brick 2026-09-13,
			 * idle for 30s on Advanced Busterhawk Gleylancer: 17.4% of a core,
			 * against 1.7% on a title that fits and 26.6% before any of this.
			 *
			 * DECIDED, left that way. Scrolling once and resting would bring a
			 * long title down near 1.7%, and it is not worth doing, because
			 * this is not idle. A marquee only runs while its title is focused
			 * and on screen, so someone looking at one is reading it - the cost
			 * is the feature doing its job while it is being used. And a device
			 * set down on a long title with nobody looking is ended by Auto Off,
			 * so there is no long unattended stretch of it to save. */
			redraw_at(plat_now_ms() + ui_pingpong_wait(tw - boxw, phase));
		} else {
			ui_text(a->r, ft2, g->title, tx, 40, 0, UI_TEXT);
		}
		/* An album is two names, and on Muse's shelf the artist is the one
		 * the letter jump goes by - so it is said, a step quieter, under the
		 * album's. Fitted rather than slid: the album's title already slides
		 * on this clock, and two lines moving at once is a lot of motion. */
		if (is_muse(s)) {
			char afit[192];
			const char *under = g->name;
			int al = v->album && idx >= 0 && idx < v->list.count ? v->album[idx] : -1;

			/* A book says it is finished there, and says nothing rather than
			 * its own name again when it is a folder with no author above it. */
			if (al >= 0 && g_muse.albums[al].book) {
				if (g_book_done && g_book_done[al]) under = "Finished";
				else if (!strcmp(g->name, g->title)) under = "";
			}
			ui_fit_text(ui_font(UI_F_MENU), under, afit, sizeof afit, boxw);
			ui_text(a->r, ui_font(UI_F_MENU), afit, tx, 40 + line + 4, 0,
			        UI_TEXT_DIM);
		}
		/* Where you are in the list, not what this game is - the same kind
		 * of thing the rail says, so it goes where the rail is.
		 *
		 * Horizontally that is centered under the row, which is a band across
		 * the middle of the screen: nothing travels down there.
		 *
		 * Vertically it is bottom left, under the rail's lower end, because
		 * the shelf slides along y a card height and a half per step and a
		 * centered counter had every move drag a card and its reflection
		 * straight across it. This read `!both` for years, lumping Vertical
		 * in with Horizontal, and the comment above it only ever argued the
		 * case for Horizontal - it was written when Vertical was the cube.
		 * `.vertical` means the two share an input axis, not that they are
		 * drawn alike.
		 *
		 * Cubic draws the same string in the same place, from its own caller,
		 * after the cube; baked into a face it would turn away mid-move and two
		 * would show at once, one per face, disagreeing by one. */
		if (!CARD_DIRS[g_dir].both) {
			snprintf(count, sizeof count, "%d / %d", idx + 1, v->list.count);
			if (CARD_DIRS[g_dir].vertical)
				ui_text(a->r, ui_font(UI_F_META), count, 24, 700,
				        -1, UI_TEXT_DIM);
			else
				ui_text(a->r, ui_font(UI_F_META), count,
				        TORTOS_SCREEN_W / 2, 690, 0, UI_TEXT_DIM);
		}
	} else {
		char nfit[192];

		ui_fit_text(ui_font(UI_F_TITLE), s->name, nfit, sizeof nfit,
		            TORTOS_SCREEN_W - 48);
		ui_text(a->r, ui_font(UI_F_TITLE), nfit, TORTOS_SCREEN_W / 2, 40, 0,
		        UI_TEXT);
	}
}

/* One game filling the screen, for a cube face. CF_LAYOUT_SINGLE draws the art
 * and draw_game_text draws everything said about it, so the face is complete
 * and turns as one.
 *
 * It had a counterpart, draw_sys_face, which went when Vertical stopped
 * turning a cube: Cubic draws game faces on both axes, so a face carrying a
 * SYSTEM no longer has anywhere to appear. */
static void draw_games_face(app *a, sysview *v, const system_cfg *s, int idx)
{
	SDL_Rect focus;
	coverflow one;

	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
	SDL_RenderClear(a->r);
	draw_background(a);

	const cf_layout *lay = is_muse(s) ? &CF_LAYOUT_ALBUM_FACE : &CF_LAYOUT_GAME_FACE;

	cf_focus_rect(lay, TORTOS_SCREEN_W, TORTOS_SCREEN_H, &focus);
	ui_glow(a->r, &focus, s->accent, 100, 2.3f);
	if (v->list.count > 0) {
		cf_reset(&one, idx);
		cf_draw(&one, a->r, TORTOS_SCREEN_W, TORTOS_SCREEN_H, v->list.count,
		        game_get_tex, a, lay);
	}
	draw_game_text(a, v, s, idx);
}

/* ChaOS: Game List View's List - a system's games as one of the launcher's
 * menus, so it moves and reads as every other list does: the panel, the
 * sliding plate, a long title running along its row, the count at the foot.
 * A favorite carries a dot. */
static void menu_draw(app *a, const char *heading, const menu_row *rows, int n,
                      int sel, int fixed_w, unsigned accent);
static unsigned menu_accent(void);

static void draw_game_list(app *a, sysview *v, const system_cfg *s)
{
	static menu_row *rows;
	static int cap, last_cursor = -1, last_sys = -1;
	static Uint32 moved_at;
	static char count[CFG_STR + 48];
	int n = v->list.count, i, lo, hi;
	Uint32 now = plat_now_ms();

	if (n + 2 > cap) {
		menu_row *nr = realloc(rows, (size_t)(n + 2) * sizeof *nr);
		if (!nr) return;
		rows = nr;
		cap = n + 2;
	}
	/* favorites asked of the rows near the cursor alone: the rest are off
	 * the panel, and asking all of a long shelf every frame costs */
	lo = v->cursor - 24; hi = v->cursor + 24;
	for (i = 0; i < n; i++) {
		const char *mark = NULL;
		if (i >= lo && i <= hi) {
			const system_cfg *gs = &a->sys.systems[shelf_owner(a, a->sys_cursor, i)];
			if (fav_is(gs->tag, v->list.items[i].file)) mark = "\xE2\x80\xA2";
		}
		/* a value, even an empty one, sets the title against the left as a
		 * list's names are, rather than centred as a lone menu row is */
		rows[i] = (menu_row){ v->list.items[i].title, mark ? mark : "", true };
	}
	/* where in the list, in the heading: a note at the foot would scroll
	 * away with the names */
	snprintf(count, sizeof count, "%s  \xC2\xB7  %d / %d", s->name,
	         n > 0 ? v->cursor + 1 : 0, n);
	/* the whole screen, its border and rounded corners kept a little in */
	g_menu_fill = 10;
	menu_draw(a, count, rows, n, v->cursor, 4096, menu_accent());
	g_menu_fill = -1;

	/* frames while the plate and the list ease to a new row, and while a
	 * long title slides */
	if (v->cursor != last_cursor || a->sys_cursor != last_sys) {
		last_cursor = v->cursor;
		last_sys = a->sys_cursor;
		moved_at = now;
	}
	if (now - moved_at < 500) redraw_now();
	else if (n > 0 && ui_text_width(ui_font(UI_F_MENU), v->list.items[v->cursor].title) > 700)
		redraw_at(now + 33);
}

static void draw_games(app *a)
{
	shelf_pacing(&a->view[a->sys_cursor].cf);
	shelf_pacing(&a->cf_sys);
	if (CARD_DIRS[g_dir].both) { draw_both(a); return; }
	if (LIST_VIEW_ON() && !is_muse(&a->sys.systems[a->sys_cursor])) {
		draw_game_list(a, &a->view[a->sys_cursor], &a->sys.systems[a->sys_cursor]);
		return;
	}

	sysview *v = &a->view[a->sys_cursor];
	const system_cfg *s = &a->sys.systems[a->sys_cursor];
	bool albums = is_muse(s);
	const cf_layout *row = albums ? &CF_LAYOUT_ALBUMS : &CF_LAYOUT_GAMES;
	SDL_Rect focus;

	cf_focus_rect(row, TORTOS_SCREEN_W, TORTOS_SCREEN_H, &focus);
	ui_glow(a->r, &focus, s->accent, 100, 2.3f);
	if (cf_draw(&v->cf, a->r, TORTOS_SCREEN_W, TORTOS_SCREEN_H, v->list.count,
	            game_get_tex, a,
	            !CARD_DIRS[g_dir].vertical ? row
	            : albums ? &CF_LAYOUT_ALBUMS_V : &CF_LAYOUT_GAMES_V))
		redraw_now();
	/* Warm where the move is about to cut to, one card per frame, underneath
	 * the departure that is still being drawn. Eviction waits: the cursor is
	 * already at the destination and evict_far measures from the cursor, so
	 * running it now would free the very cards the departure is drawing. */
	{
		int land = cf_landing(&v->cf, v->list.count);
		if (land >= 0) prime_toward(a, a->sys_cursor, land);
	}
	if (!cf_cutting(&v->cf)) evict_far(v, TEX_KEEP_NEAR);

	draw_game_text(a, v, s, v->cursor);
	(CARD_DIRS[g_dir].vertical ? ui_rail_v : ui_rail)
		(a->r, TORTOS_SCREEN_W, TORTOS_SCREEN_H, v->cf.pos, v->list.count,
		 s->accent, NULL, NULL);
}

/* The shelf, without presenting it: the options menu draws over a live one, so
 * the cards keep their tint easing behind the panel rather than freezing into
 * a still. */
static void draw_shelf(app *a)
{
	/* Install whatever the workers finished, before anything is drawn.
	 *
	 * Here rather than in the main loop, because the main loop is one of
	 * seventeen places that draw the shelf - every menu and overlay shows it
	 * behind itself, each from its own loop. Draining only in the main loop
	 * meant that while any of those was open, a card not yet decoded stayed
	 * blank for as long as the screen was up, and finished decodes piled into
	 * the result ring and were discarded once it filled. Everything that draws
	 * the shelf wants its textures, so the thing that draws the shelf drains.
	 * First, too: the cube sets render targets further down, and nothing here
	 * should depend on that not having happened yet. */
	texload_drain(a);
	/* And any system card still missing is asked for again: a game list
	 * draws none, so nothing else would ask, and a full queue turns an ask
	 * away. Thirteen checks once they are all in. */
	for (int i = 0; i < a->sys.count && !CARDS_BIG(g_cards); i++)
		if (!a->sys_tex[i]) {
			int w, h;
			sys_get_tex(a, i, &w, &h, NULL);
		}
	draw_background(a);
	/* An empty card is only the background here: the welcome draws over it
	 * from render, so a menu opened over it doesn't sit on a second panel. */
	if (a->sys.count > 0) {
		if (a->screen == SCREEN_SYSTEMS) draw_systems(a);
		else draw_games(a);
		/* ChaOS: the letter, over whichever shelf of games it is about - the
		 * games screen, or Cubic, whose faces are games */
		if (a->screen != SCREEN_SYSTEMS || CARD_DIRS[g_dir].both)
			draw_letter_popup(a);
	}
	draw_low_battery_chrome(a->r);
}

static void welcome_draw(app *a);   /* with xfer_screen, below */

static void render(app *a)
{
	g_redraw_at = REDRAW_NEVER;
	draw_shelf(a);
	if (a->sys.count <= 0) welcome_draw(a);
	draw_upd_notice(a);
	plat_draw_osd(a->r);
	/* The tint lands exactly on its target now (see tick_tint), so this is a
	 * test that ends rather than one that is true forever. */
	if (a->sys.count > 0 && a->tint != a->sys.systems[a->sys_cursor].accent)
		redraw_now();
	redraw_at(plat_osd_until());
	plat_present();
}

/* Ease the background tint toward the focused system rather than snapping: the
 * color is meant to feel like the light the machine gives off, and light does
 * not cut. Called from every loop that draws the shelf.
 *
 * And then land it. ui_mix truncates, so an ease this gentle stops short on any
 * channel that is rising: at 60fps each step is about 14% of the gap, and 14% of
 * 7 is under one, so it came to rest up to 7 units below its target and stayed
 * there for good. That never showed, but it meant "has the tint arrived" was
 * never true - and a launcher that only redraws while something is still
 * changing would have redrawn at the refresh rate forever. Within 12 on every
 * channel it takes the target outright: under 5% of a channel, on a dim wash, at
 * the very tail of an ease nobody watches finish. 12 rather than 7 because a
 * faster frame means a smaller step and a wider stall; dt is what sets it. */
static unsigned last_tint_ms;
static void tick_tint(app *a)
{
	unsigned now = plat_now_ms();
	unsigned target = a->sys.systems[a->sys_cursor].accent, next;
	float dt = (float)(now - last_tint_ms) / 1000.0f;
	int dr, dg, db;

	last_tint_ms = now;
	if (dt > 0.1f) dt = 0.1f;
	next = ui_mix(a->tint, target, 1.0f - expf(-dt * 9.0f));
	dr = (int)((next >> 16) & 255) - (int)((target >> 16) & 255);
	dg = (int)((next >> 8) & 255) - (int)((target >> 8) & 255);
	db = (int)(next & 255) - (int)(target & 255);
	a->tint = (abs(dr) <= 12 && abs(dg) <= 12 && abs(db) <= 12) ? target : next;
}

/* ---------- transitions --------------------------------------------------- */

/* Starting a game the slow way: the focused card comes at you and the screen
 * goes with it.
 *
 * Only on the fallback, when there is no resident emulator and Diatom runs as
 * a process of its own. A normal launch has no animation at all, and that is a
 * display-safety rule rather than a taste call - see the resident path in
 * launch(): Diatom presents through fbdev and this process through GL, and the
 * two presenting at once wedges the display engine. On the fallback nothing
 * else is presenting yet, and this process tears its display down straight
 * after. This comment used to say the zoom played while the resident loaded
 * the ROM, which stopped being true when that rule was made. */
static void anim_launch(app *a, unsigned ms)
{
	sysview *v = &a->view[a->sys_cursor];
	unsigned t0 = plat_now_ms(), now;
	SDL_Rect from;
	int tw = 0, th = 0;
	SDL_Texture *card;

	cf_focus_rect(&CF_LAYOUT_GAMES, TORTOS_SCREEN_W, TORTOS_SCREEN_H, &from);
	card = v->list.count ? game_get_tex(a, v->cursor, &tw, &th, NULL) : NULL;

	while ((now = plat_now_ms()) - t0 < ms) {
		float k = (float)(now - t0) / (float)ms;
		float e = k * k;                     /* accelerate away */
		float scale = 1.0f + e * 2.2f;
		SDL_Rect dst;

		/* Asked again every frame until it is there. Card art decodes on a
		 * worker, so A pressed straight after a letter jump can start this
		 * before the focused card has arrived - and asking once, as this did,
		 * zoomed an empty frame for the whole animation. Now a card that lands
		 * partway through joins the zoom where it is. */
		if (!card && v->list.count) {
			texload_drain(a);
			card = game_get_tex(a, v->cursor, &tw, &th, NULL);
		}

		SDL_SetRenderDrawColor(a->r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
		SDL_RenderClear(a->r);
		if (card) {
			dst.w = (int)(from.w * scale);
			dst.h = (int)(from.h * scale);
			dst.x = TORTOS_SCREEN_W / 2 - dst.w / 2;
			dst.y = TORTOS_SCREEN_H / 2 - dst.h / 2;
			SDL_SetTextureAlphaMod(card, (Uint8)(255 * (1.0f - k)));
			SDL_RenderCopy(a->r, card, NULL, &dst);
			SDL_SetTextureAlphaMod(card, 255);
		}
		plat_present();
		SDL_Delay(6);
	}
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
	SDL_RenderClear(a->r);
	plat_present();
}

/* The send-off (ChaOS), the boot played backwards: the logo whole, as the
 * boot left it, then every square flies out along the path it came in by,
 * accelerating and fading, in the order tools/genchaosmotion.py gave them,
 * until only the ground is left.
 *
 * TortOS's timing, measured against the shutdown (2026-08-28): 1.42 s from
 * the Power off press to the system going, so the animation runs about
 * 900 ms and ends without clearing; power_off turns the backlight off right
 * after. Its last frame is the empty ground. */
static void anim_poweroff(app *a)
{
	const unsigned T_HOLD = 160, T_SPREAD = 420, T_FLY = 300;
	const unsigned ms = T_HOLD + T_SPREAD + T_FLY;      /* 880, about TortOS's 900 */
	unsigned t0 = plat_now_ms(), now;

	while ((now = plat_now_ms()) - t0 < ms) {
		float t = (float)(now - t0);
		int i;

		SDL_SetRenderDrawColor(a->r, CHAOS_GROUND_R, CHAOS_GROUND_G, CHAOS_GROUND_B, 255);
		SDL_RenderClear(a->r);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		for (i = 0; i < CHAOS_NCELLS; i++) {
			const chaos_cell *c = &CHAOS_CELLS[i];
			/* Leaves in the boot's arrival order reversed: last in, first out */
			float k = (t - T_HOLD - (1.0f - c->delay) * T_SPREAD) / (float)T_FLY;
			float e, d;
			SDL_FRect q;

			if (k >= 1.0f) continue;
			if (k < 0.0f) k = 0.0f;
			e = k * k * k;                       /* accelerating away */
			d = c->dist * e;
			q.x = c->x + c->dx * d;
			q.y = c->y + c->dy * d;
			q.w = q.h = CHAOS_CELL;
			SDL_SetRenderDrawColor(a->r, c->r, c->g, c->b,
			                       (Uint8)(255.0f * (k < 0.4f ? 1.0f : 1.0f - (k - 0.4f) / 0.6f)));
			SDL_RenderFillRectF(a->r, &q);
		}
		plat_present();
		SDL_Delay(6);
	}
	/* The empty ground, exactly, and no clear after: it is what is on the
	 * panel as the backlight goes. */
	SDL_SetRenderDrawColor(a->r, CHAOS_GROUND_R, CHAOS_GROUND_G, CHAOS_GROUND_B, 255);
	SDL_RenderClear(a->r);
	plat_present();
}

static void power_off(app *a)
{
	remember_place(a);
	book_keep(true);
	plat_request_poweroff();
	anim_poweroff(a);
	/* And dark the moment it ends. The rest of the shutdown is not instant -
	 * on the GKD Pixel 2 it waits for the programs on the card to go before
	 * unmounting it - and the shell stayed lit through all of it. Eric's
	 * call, 2026-10-02, for both devices. */
	plat_backlight(false);
	a->running = false;
}

/* ---------- menus --------------------------------------------------------- */

/* Both menus in TortOS are the same shape - a short list on a slab, over a
 * paused game frame or over the shelf - so they are drawn by one function and
 * cannot drift apart. The slab is sized to its own widest row with the same
 * padding on every side, rather than to a number picked once and left behind
 * by the next label someone adds. */
/* menu_row and the row kinds: src/menu.h. */

#define MENU_RADIUS 20
/* The launcher's one accent: its menus, the systems row and their chrome.
 * ChaOS: chosen in MENU > Menu Color (src/theme.h), ChaOS Orange unless
 * changed; TortOS's was its cyan, 0x3DD6FF, which is still on the list. */
static int g_menu_color;
#define MENU_ACCENT (MENU_COLORS[g_menu_color].rgb)
static unsigned menu_accent(void) { return MENU_ACCENT; }

/* ChaOS: the menu being drawn is a first one, opened straight from the shelf
 * or a game (MENU, or a game's info), rather than one opened from another
 * menu. A submenu shows a back chevron before its heading; a first menu does
 * not. Set around the draw by whoever draws a first menu (menu_style.root). */
static bool g_menu_root;
/* ChaOS: a heading with neither the status line nor the back chevron, for
 * the updater's pictures (--updater-art): drawn once, ahead of time, and shown
 * while nothing runs, so a clock would be frozen and B means nothing. */
static bool g_menu_bare;

/* A chevron: `dir` +1 points right, -1 left; `h` its height; at (cx, cy). Two
 * strokes of a few px, as SDL has no thick line. */
static void draw_chevron(SDL_Renderer *r, float cx, float cy, float h, int dir,
                         SDL_Color c)
{
	const float w = h * 0.5f, t = h * 0.2f > 2.5f ? h * 0.2f : 2.5f;
	float x0 = cx - dir * w / 2, x1 = cx + dir * w / 2;
	SDL_Vertex v[6];
	int idx[12] = { 0, 1, 2, 1, 3, 2,  2, 3, 4, 3, 5, 4 }, i;

	/* top arm then bottom arm, each a quad of thickness t */
	v[0].position = (SDL_FPoint){ x0, cy - h / 2 };
	v[1].position = (SDL_FPoint){ x0 - dir * t, cy - h / 2 + t * 0.6f };
	v[2].position = (SDL_FPoint){ x1, cy };
	v[3].position = (SDL_FPoint){ x1 - dir * t * 1.4f, cy };
	v[4].position = (SDL_FPoint){ x0, cy + h / 2 };
	v[5].position = (SDL_FPoint){ x0 - dir * t, cy + h / 2 - t * 0.6f };
	for (i = 0; i < 6; i++) { v[i].color = c; v[i].tex_coord = (SDL_FPoint){ 0, 0 }; }
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_RenderGeometry(r, NULL, v, 6, idx, 12);
}

/* The unit every menu measurement is in. Row height, padding and the gap
 * between the two columns are all cut from it, so the whole panel scales with
 * the type rather than with a set of numbers that have to be retuned together. */
/* Two colors, `t` of the way from a to b. */
static SDL_Color col_mix(SDL_Color a, SDL_Color b, float t)
{
	if (t <= 0.0f) return a;
	if (t >= 1.0f) return b;
	return (SDL_Color){ (Uint8)(a.r + (b.r - a.r) * t),
	                    (Uint8)(a.g + (b.g - a.g) * t),
	                    (Uint8)(a.b + (b.b - a.b) * t),
	                    (Uint8)(a.a + (b.a - a.a) * t) };
}

/* ChaOS: an on/off switch for a toggle row (menu_row.toggle), right-aligned
 * at `right`, centred on `cy`, sized from the text: a rounded track, filled
 * with the menu's accent when on, and a knob at the end it is set to.
 * Brighter as the row is selected (`sel`, 0 to 1), as a value's text is. */
static int menu_switch_w(int text_h) { return text_h * 17 / 10; }

static void draw_switch(SDL_Renderer *r, int right, float cy, int text_h, bool on,
                        bool live, float sel, unsigned accent)
{
	int w = menu_switch_w(text_h), h = text_h * 9 / 10, k = h - 6;
	SDL_Rect track = { right - w, (int)(cy - h / 2.0f), w, h };
	SDL_Rect knob  = { on ? right - 3 - k : right - w + 3, track.y + 3, k, k };
	SDL_Color acc = { (Uint8)(accent >> 16), (Uint8)(accent >> 8), (Uint8)accent, 255 };
	SDL_Color tc, kc;

	if (!live) sel = 0.0f;
	if (on) {
		tc = acc;
		tc.a = (Uint8)((live ? 170 : 90) + (live ? 85 : 0) * sel);
		kc = (SDL_Color){ 245, 245, 250, (Uint8)(live ? 255 : 150) };
	} else {
		tc = (SDL_Color){ 255, 255, 255, (Uint8)(28 + 22 * sel) };
		kc = col_mix(UI_TEXT_DIM, UI_TEXT_SOFT, sel);
		if (!live) kc.a = 150;
	}
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	ui_round_rect(r, &track, h / 2, tc);
	ui_round_rect(r, &knob, k / 2, kc);
}


static int menu_row_h(void) { return ui_font_line(UI_F_MENU) * 3 / 2; }
/* The air between the panel edge and its content, on each side. menu_draw
 * draws it and menu_std_width subtracts it, and a panel width means nothing
 * unless both use the same number. */
static int menu_pad(void)   { return menu_row_h() * 3 / 4; }

/* The air between the panel and the screen edge, on every side. */
#define MENU_MARGIN 24

/* How many row_h rows menu_draw can show beside `fixed` other full rows,
 * `rules` rules and `notes` notes, before it starts windowing.
 *
 * It windows from the TOP of the list, which is right for a plain menu and
 * wrong for a screen whose last rows are a footer: Play Time overflowed by
 * 42px and menu_draw answered by scrolling the footer off the bottom, so the
 * totals and the key legend were never on screen together. A screen that
 * builds furniture around a list has to know how much list is left, and this
 * is that arithmetic in one place rather than a constant tuned to one text
 * size - Text Size is a setting, and at 125% a hardcoded row count is wrong
 * again. */
static int menu_list_fit(int fixed, int rules, int notes)
{
	int pad = menu_pad(), row_h = menu_row_h();
	int head_h = ui_font_line(UI_F_LABEL) + pad;
	int avail = TORTOS_SCREEN_H - MENU_MARGIN * 2
	          - (head_h + pad / 2)          /* heading down to the first row */
	          - pad;                        /* and the panel's bottom air */

	avail -= fixed * row_h;
	avail -= rules * (pad / 2 + 2);
	avail -= notes * (ui_font_height(UI_F_MENU) + pad / 3);
	return avail < row_h ? 1 : avail / row_h;
}

/* How many BODY lines fit in a panel that is nothing but body - a synopsis
 * card. The same arithmetic menu_list_fit does, asked the other way round: that
 * one reserves notes and answers in rows, and a page with no rows on it at all
 * would get the wrong answer from it.
 *
 * Here rather than in the caller because the two must agree about the heading,
 * the padding and the note height, and a screen that measured itself would
 * drift from the panel that draws it the first time either is retuned. */
static int menu_notes_fit(void)
{
	int pad = menu_pad();
	int head_h = ui_font_line(UI_F_LABEL) + pad;
	int note_h = ui_font_height(UI_F_MENU) + pad / 3;
	int avail = TORTOS_SCREEN_H - MENU_MARGIN * 2
	          - (head_h + pad / 2) - pad;

	return note_h > 0 && avail > note_h ? avail / note_h : 1;
}

/* `fixed_w` is the content width to use, or 0 to size to these rows. The shelf
 * menus pass a width measured across both of them so the panel never resizes;
 * the in-game menu has no values to cycle and sizes to itself. */
/* `accent` is the panel's border and heading rule. The shelf's own menu passes
 * MENU_ACCENT because that menu is TortOS, not whichever card is under the
 * cursor; a menu that belongs to a system passes that system's color. */
/* `vcolors` is an optional array parallel to `rows`: a nonzero entry is the
 * 0xRRGGBB to draw that row's value in, whatever the cursor is doing. It is
 * not a field on menu_row because exactly one screen has any use for it, and
 * putting it there made all 69 initializers in the launcher declare that they
 * do not care. NULL, which is what menu_draw passes, is the ordinary rule. */
/* `visits_all` says the cursor can rest on EVERY row, whatever their `live`
 * says - which is only true of a list that uses `live` for something other
 * than selectability. The achievement list is the one: there `live` means
 * EARNED, and the cursor walks earned and unearned alike. Everywhere else the
 * two meanings coincide and this is false. */
/* `loop_at` is where the rows start repeating: rows[loop_at] is a second copy
 * of rows[0], laid out by the caller with a rule between the copies, so a body
 * too tall for its panel can scroll THROUGH and come round rather than running
 * backwards. 0 for every screen that does not do that, which is all but one.
 *
 * The caller passes it rather than this inferring it, because only the caller
 * knows the content is a repeat - and getting it wrong does not look like a
 * bug, it looks like a scroll that jumps. */
/* How full the next panel's MENU_BAR is, 0 to 1 (src/menu.h). Set by the
 * screen just before it draws, since a row carries no number. */
static float g_menu_bar;
static void menu_bar_fill(float f) { g_menu_bar = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f; }

/* Where the last panel drew its heading (centered on head_x), a row's value
 * (right-aligned to value_x) and its bar: --updater-art hands these to the
 * updater, which puts its own version, percentage and fill there. */
static struct { int head_x, head_y, value_x, value_y, bar_x, bar_y, bar_w, bar_h; } g_menu_seen;

static void menu_draw_ex(app *a, const char *heading, const menu_row *rows,
                         int n, int sel, int fixed_w, unsigned accent,
                         const unsigned *vcolors, bool visits_all, int loop_at)
{
	TTF_Font *fm = ui_font(UI_F_MENU), *fh = ui_font(UI_F_LABEL);
	int row_h = menu_row_h();
	int pad = menu_pad();
	/* A rule is 2px of bar plus the gap the heading's rule gets below it, so
	 * the footer sits off the list by the same amount the first row sits off
	 * the heading. Giving it a whole row_h left it swimming.
	 *
	 * A note is one line of text and nothing else. row_h is 1.5x the line, so a
	 * note in a full row carried half a row of air underneath it and then the
	 * panel's own pad on top of that, which read as the footer floating off the
	 * bottom border. */
	int rule_h = pad / 2 + 2;
	/* A rule inside the body has that air on both sides of its bar. Drawn at
	 * the foot of a rule_h, as the footer's is, the bar sat against whatever
	 * came under it, so a row between it and the footer's rule rode high:
	 * nothing above it and pad/2 below (the welcome and System Update both
	 * put one there). */
	int hr_h = pad + 2;
	int gap = row_h;                 /* between the label and value columns */
	int text_h = fm ? TTF_FontHeight(fm) : row_h;
	int note_h = text_h + pad / 3;
#define ROW_H(r) (ROW_IS_HR(r) ? hr_h : !(r).label ? rule_h \
                  : (ROW_IS_NOTE(r) ? note_h : row_h))
	/* Center the ink, not the em box. The box reserves a descender's depth
	 * below the baseline that labels like "Wi-Fi" and "Bluetooth" never use,
	 * so centering the box leaves the visible line riding high in its row and
	 * reads as a highlight sitting too low. Descent is negative, so half of it
	 * subtracted moves the line down onto the middle of the plate. */
	int ink_off = fm ? -TTF_FontDescent(fm) / 2 : 0;
	/* The heading band runs from the panel's top edge down to the rule, and
	 * the heading is centered inside it rather than hung a fixed distance from
	 * the top - otherwise retuning the heading's size moves it off center,
	 * which is exactly what happened when it grew. `content_off` is the panel
	 * top to the first row, so a panel with no heading just pads instead. */
	int notes_drawn = 0;   /* which marquee slot the next long note takes */
	int scroll = 0;        /* pixels the body is lifted by, cursorless */
	int scroll_h = 0;      /* the body viewport's height, 0 if nothing scrolls */
	int scroll_span = 0;
	int scroll_split = 0;  /* first pinned row: the rule, or n if there is none */
	int scroll_foot = 0;   /* the pinned rows' total height */
	/* A SMALL, FAST GIVE WHEN THE WINDOW SCROLLS - not a full slide.
	 *
	 * REPEAT_RATE_MS is 90, so any animation long enough to watch is
	 * interrupted before it lands; chasing the whole row's travel means
	 * permanently lagging behind the cursor. This takes the hard edge off
	 * instead: at most a third of a row of displacement, decaying fast, so it
	 * is never far from where it belongs and a held button stays honest.
	 *
	 * A decay rather than a timed ease for the same reason - there is no
	 * animation to interrupt, only a distance that shrinks. */
	float win_off = 0.0f;

	/* Where the highlight actually is. The text colors are weighed by how
	 * much of it covers each row rather than by `i == sel`, so a row lights
	 * as the plate arrives and dims as it leaves, and the two rows either
	 * side of a move cross over correctly without anyone tracking which row
	 * was left. */
	float plate_y = -1e9f, plate_h = 0.0f;
	int line_head   = ui_font_line(UI_F_LABEL);
	/* A heading may carry a second line, separated by a newline. Nothing else
	 * passes one; the achievements screen does, because its heading is a game
	 * title AND two counts, and on one line that was wider than the panel and
	 * got both its ends clipped. Splitting keeps both rather than choosing. */
	const char *head2 = heading ? strchr(heading, '\n') : NULL;
	char head1[192];
	int head_lines = head2 ? 2 : 1;
	int head_h      = heading ? line_head * head_lines + pad : 0;
	int content_off = heading ? head_h + pad / 2 : pad;
	bool two_col = false;
	int content_w = 0, i, k, rows_h = 0;
	/* ChaOS: WHAT THE SELECTED ROW DOES, in a line under the rows. A menu
	 * whose rows carry a hint (menu_row.hint) keeps a footer for it, the
	 * same height whichever row is on, so moving the cursor never moves the
	 * panel. A hint used to sit beside its label, where a long one was cut
	 * to "o..." and none could say more than a few words. */
	bool descs = false;
	int desc_h = 0;
	SDL_Rect panel;
	int cx, content_x, content_y;
	/* The list can outgrow the screen from either end - the type scale turns
	 * up, and this menu is meant to gain rows - so the panel is capped to the
	 * screen and the rows window around the selection when they do not all
	 * fit. A menu that runs off the top is worse than one that scrolls. */
	const int margin = g_menu_fill >= 0 ? g_menu_fill : MENU_MARGIN;
	int vis = n, first = 0;

	for (i = 0; i < n; i++)
		if (rows[i].value && !ROW_IS_NOTE(rows[i])) two_col = true;
	for (i = 0; i < n; i++) {
		int w;

		if (!rows[i].label) continue;          /* a rule measures nothing */
		w = ui_text_width(fm, rows[i].label);
		if (two_col && rows[i].value && !ROW_IS_NOTE(rows[i]))
			w += gap + (rows[i].toggle ? menu_switch_w(text_h)
			                           : ui_text_width(fm, rows[i].value));
		if (rows[i].cycles) w += text_h * 3 / 2;   /* the left arrow */
		if (w > content_w) content_w = w;
		/* ChaOS: wide enough for its description too, so it can be read
		 * whole; the screen still caps it, and a longer one slides */
		if (sel >= 0 && rows[i].hint && rows[i].hint[0]) {
			TTF_Font *df = ui_font(UI_F_META);
			int hw = df ? ui_text_width(df, rows[i].hint) : 0;

			if (hw > content_w) content_w = hw;
		}
	}
	if (head2) {
		size_t n1 = (size_t)(head2 - heading);

		if (n1 >= sizeof head1) n1 = sizeof head1 - 1;
		memcpy(head1, heading, n1);
		head1[n1] = '\0';
		head2++;                                    /* past the newline */
	} else if (heading) {
		snprintf(head1, sizeof head1, "%s", heading);
	}

	if (heading) {
		int w = ui_text_width(fh, head1);
		int w2 = head2 ? ui_text_width(fh, head2) : 0;

		if (w2 > w) w = w2;
		if (w > content_w) content_w = w;
	}
	if (fixed_w > 0) content_w = fixed_w;
	if (content_w > TORTOS_SCREEN_W - margin * 2 - pad * 2)
		content_w = TORTOS_SCREEN_W - margin * 2 - pad * 2;

	{
		int sum = 0;
		for (i = 0; i < n; i++) sum += ROW_H(rows[i]);
		rows_h = sum;
	}
	for (i = 0; i < n && sel >= 0; i++)
		if (rows[i].label && rows[i].hint && rows[i].hint[0]) descs = true;
	if (descs) desc_h = rule_h + ui_font_height(UI_F_META) + pad / 3;
	if (content_off + rows_h + desc_h + pad > TORTOS_SCREEN_H - margin * 2) {
		int avail = TORTOS_SCREEN_H - margin * 2 - content_off - pad - desc_h;

		if (sel >= 0) {
			/* THE WINDOW ONLY MOVES WHEN THE CURSOR WOULD LEAVE IT.
			 *
			 * It used to center itself on the selection, which meant almost
			 * every press scrolled the list: the highlight stayed put in the
			 * middle of the panel and the rows jumped under it. That is the
			 * wrong thing to move. The cursor is what the player is aiming,
			 * so the cursor travels and the list holds still until it runs
			 * out of window.
			 *
			 * Static, and keyed on `n` like the highlight's own state, since
			 * menu_draw keeps nothing between frames and every screen shares
			 * it. A different row count is a different menu and starts at the
			 * top rather than inheriting someone else's scroll. */
			static int win_n = -1, win_first;

			vis = avail / row_h;
			if (vis < 1) vis = 1;
			if (vis > n) vis = n;

			if (win_n != n) win_first = 0;
			win_n = n;
			win_first = menu_window_first(rows, n, sel, vis, win_first);
			first = win_first;
		} else {
			/* NO CURSOR MEANS NOTHING CAN SCROLL IT, so it scrolls itself.
			 * Dropping rows off the bottom is what a cursor makes safe: you
			 * can always walk down to them. A card has no walk, so the rows
			 * all stay and the window moves over them instead, on the same
			 * ping-pong a long title runs sideways.
			 *
			 * ONLY THE BODY MOVES. Past the rule is the footer, and on the
			 * achievement card that is the points and whether it is earned -
			 * the two facts a reader wants WHILE reading the description, not
			 * after waiting out a scroll to reach them. The rule is already
			 * where this menu divides a list from its footer, so it is the
			 * split here too and nothing new has to be declared. */
			int body = 0, j;

			/* MENU_RULE only: a MENU_HR is punctuation inside the body,
			 * and treating it as the footer split would pin half a synopsis
			 * to the bottom of the panel. */
			scroll_split = n;
			for (j = 0; j < n; j++)
				if (!rows[j].label && !ROW_IS_HR(rows[j])) {
					scroll_split = j;
					break;
				}
			for (j = scroll_split; j < n; j++) scroll_foot += ROW_H(rows[j]);
			for (j = 0; j < scroll_split; j++) body += ROW_H(rows[j]);

			scroll_h = avail - scroll_foot;
			if (scroll_h < row_h) scroll_h = row_h;
			scroll_span = body - scroll_h;
			if (scroll_span < 0) scroll_span = 0;
			if (loop_at > 0 && loop_at < n) {
				/* One lap is everything before the repeat, so the wrap lands
				 * on the same picture the lap started from. What keeps the
				 * bottom of the viewport covered through a whole lap is that
				 * the second copy is as tall as the first, which is only
				 * worth scrolling at all when it is taller than the panel. */
				int lap = 0;

				for (j = 0; j < loop_at; j++) lap += ROW_H(rows[j]);
				scroll = ui_scrollthrough(lap,
				                          mq_phase(MQ_VSCROLL, n, rows_h));
			} else {
				scroll = ui_pingpong(scroll_span,
				                     mq_phase(MQ_VSCROLL, n, rows_h));
			}
		}
	}

	panel.w = content_w + pad * 2;
	{   /* the heights actually on screen, not vis * row_h */
		int sum = 0, j, last = first + vis - 1;

		for (j = first; j < first + vis && j < n; j++) sum += ROW_H(rows[j]);
		/* A scrolling card is as tall as its viewport plus its pinned footer.
		 * Summing the rows would size the panel to text that is deliberately
		 * not all on it. */
		panel.h = content_off + (scroll_h ? scroll_h + scroll_foot : sum) + pad + desc_h;
		/* A panel that ends in a footer gets a smaller bottom pad. The full
		 * pad is there to keep list ITEMS off the border; a note is already
		 * held off the list by its rule and is meant to sit low.
		 *
		 * The gap under the footer text is pad/6 from centering plus whatever
		 * bottom pad is left. Set by eye on the device 2026-09-04, not derived:
		 * the full pad gives 1.17 and floats, 0.50 is too tight, and 0.75 is
		 * where Eric called it. 5/12 is the subtraction that leaves 0.75.
		 *
		 * A body row is not a footer. A synopsis short enough to fit without
		 * scrolling ends in prose, which is the panel's content and wants the
		 * same pad under it that a list does. */
		if (!scroll_h && !descs && last >= 0 && last < n && rows[last].label &&
		    ROW_IS_NOTE(rows[last]) && !ROW_IS_BODY(rows[last]))
			panel.h -= pad * 5 / 12;
	}
	if (g_menu_fill >= 0) {                   /* ChaOS: the whole screen */
		panel.w = TORTOS_SCREEN_W - margin * 2;
		panel.h = TORTOS_SCREEN_H - margin * 2;
	}
	panel.x = (TORTOS_SCREEN_W - panel.w) / 2;
	panel.y = (TORTOS_SCREEN_H - panel.h) / 2;
	cx = panel.x + panel.w / 2;
	content_x = panel.x + pad;
	content_y = panel.y + content_off;

	/* TortOS's own color, not the focused system's. The menu belongs to the
	 * launcher rather than to whatever card happens to be under the cursor, so
	 * the boot animation, the mark and this chrome are one accent: the device's
	 * first frame and the shelf agree.
	 *
	 * They did not always. The mark carried its own blue, (74,158,255), while
	 * this was (61,214,255), and comments in both files called them the same
	 * color without either having been checked against the other. Unified on
	 * this cyan on 2026-08-28, that being the direction that stays clear of the
	 * eleven system accents; the mark's old blue sat close to Genesis. */
	ui_glow(a->r, &panel, accent, 60, 1.5f);
	ui_panel(a->r, &panel, MENU_RADIUS, accent);

	if (heading) {
		/* Centered in the band by its ink, on the same reasoning as the rows:
		 * the em box carries descender depth that "TortOS" and "NES" do not use.
		 * This read "mostly do not" while the heading was "PlayOS", whose y was
		 * the exception; the shift is by font metrics rather than by the string,
		 * so nothing here changed with the name, it just got exactly true. */
		/* Center the INK between the panel's top and the rule, computed from
		 * the font rather than nudged by a fraction of the descent.
		 *
		 * The heading is capitals and lowercase with nothing below the
		 * baseline, so what should sit in the middle of that band is cap-top
		 * to baseline - not the em box, which carries a descender's depth of
		 * empty space at the bottom. Centering the box left "Wi-Fi" visibly
		 * high on the scanning screen.
		 *
		 * cap comes from 'H': maxy is its height above the baseline. */
		int asc = fh ? TTF_FontAscent(fh) : line_head;
		int cap = asc;
		int block, hy;

		if (fh) {
			int mnx, mxx, mny, mxy, adv;

			if (TTF_GlyphMetrics(fh, 'H', &mnx, &mxx, &mny, &mxy, &adv) == 0)
				cap = mxy;
		}
		/* cap-to-baseline for the first line, plus a whole line for a second */
		block = cap + (head_lines - 1) * line_head;
		/* From the panel's INNER edge, not panel.y. The border is a visible
		 * frame and the eye reads the space inside it, so centering against
		 * the outer edge is arithmetically right and looks high by exactly
		 * the border's width - measured on the Wi-Fi panel as 18px above the
		 * ink against 32 below, which is what Eric saw. */
		hy = panel.y + UI_PANEL_BORDER
		     + (head_h - UI_PANEL_BORDER - block) / 2 - (asc - cap);

		/* Same rule for the heading, and for the same reason: art_screen and
		 * notice.c each fit their own before handing it over, which worked
		 * and meant two callers had to remember. */
		{
			/* A HEADING THAT DOES NOT FIT SCROLLS, like every other line in
			 * this launcher that does not fit. It used to be cut with an
			 * ellipsis and left there, which on the achievement list meant a
			 * game's own name was the one thing on screen you could not read:
			 * "Castlevania III - Dracula's C...". The panel is a fixed width
			 * shared by every menu, and a game title is whatever it is.
			 *
			 * Left-aligned while it moves, for the reason a long note is:
			 * text wider than the box has nothing left to center.
			 *
			 * Both lines share one slot, so when a heading has two they move
			 * together rather than taking turns resetting each other's clock -
			 * the same lesson the value marquee learned on Over The Hare. */
			char h1[192], h2[192];
			int hw = ui_text_width(fh, head1);
			unsigned ph = mq_phase(MQ_HEAD, (int)strlen(head1), content_w);

			g_menu_seen.head_x = cx;
			g_menu_seen.head_y = hy;
			if (hw <= content_w) {
				ui_fit_text(fh, head1, h1, sizeof h1, content_w);
				ui_text(a->r, fh, h1, cx, hy, 0, UI_TEXT_SOFT);
			} else {
				ui_text_marquee(a->r, fh, head1, content_x, hy, content_w,
				                ph, UI_TEXT_SOFT);
			}
			if (head2) {
				if (ui_text_width(fh, head2) <= content_w) {
					ui_fit_text(fh, head2, h2, sizeof h2, content_w);
					ui_text(a->r, fh, h2, cx, hy + line_head, 0, UI_TEXT_SOFT);
				} else {
					ui_text_marquee(a->r, fh, head2, content_x,
					                hy + line_head, content_w, ph,
					                UI_TEXT_SOFT);
				}
			}
		}
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, (Uint8)(accent >> 16),
		                       (Uint8)(accent >> 8), (Uint8)accent, 70);
		SDL_RenderFillRect(a->r, &(SDL_Rect){ content_x, panel.y + head_h,
		                                      content_w, 2 });
		/* ChaOS: the clock and the battery, in the heading's band, so every
		 * menu shows them with no screen of its own. A submenu's back chevron
		 * takes the left, so its clock starts a step further in. */
		if (!g_menu_bare) {
			int hw = ui_text_width(fh, head1);
			int left = panel.x + pad * (g_menu_root ? 1 : 2);
			int right = panel.x + panel.w - pad;

			draw_menu_status(a->r, left, right,
			                 hy + (line_head - ui_font_height(UI_F_META)) / 2,
			                 cx - hw / 2 - pad, cx + hw / 2 + pad);
		}
		/* ChaOS: a submenu says so, and that B goes back, with a chevron
		 * pointing back at the heading's left, in the margin */
		if (!g_menu_root && !g_menu_bare)
			draw_chevron(a->r, (float)(panel.x + pad / 2 + UI_PANEL_BORDER / 2),
			             panel.y + head_h / 2.0f + pad / 4.0f,
			             (float)ui_font_line(UI_F_LABEL) * 0.55f, -1,
			             (SDL_Color){ (Uint8)(accent >> 16), (Uint8)(accent >> 8),
			                          (Uint8)accent, 220 });
	}

	/* THE HIGHLIGHT SLIDES. It used to be drawn inside the row loop at
	 * i == sel, which put it wherever the cursor was that frame and made it
	 * snap. Here it is a rect of its own, eased from where it was toward
	 * where it belongs, and drawn BEFORE the rows so it stays under all of
	 * them - inside the loop, a plate that had moved up would cover the text
	 * of the row already drawn above it.
	 *
	 * The state is static because menu_draw has none and every screen shares
	 * it. `n` is the key: a different row count is a different menu, and the
	 * plate snaps rather than travelling across from wherever the last menu
	 * left it. Two menus of the SAME length in a row will slide between
	 * themselves, which is wrong but invisible - both are lists of the same
	 * shape and the plate is already close to where it should be. */
	if (vis < n) {
		static int      wn = -1, wfirst = -1;
		static float    wcur, wmid;
		static unsigned wlast;
		/* Two chained decays, not one. A single exponential moves furthest on
		 * its first frame and then crawls - 10.5px of a 16px give, then 6.8,
		 * then 4.4 - which is the front-loading that reads as a jerk. Chained
		 * gives an S: 1.8, 2.7, 2.9, 3.0, 2.6, near-equal steps through the
		 * middle where the eye is tracking.
		 *
		 * 55ms and a third of a row would be smoother again at 70ms, but the
		 * lag while a button is held grows with both: 0.28 of a row here,
		 * 0.44 there, and past that the list visibly trails the cursor. */
		const float TAU = 55.0f, CAP = (float)row_h * 0.55f;
		unsigned nw = plat_now_ms();
		float dt = wlast ? (float)(nw - wlast) : 0.0f;

		wlast = nw;
		if (dt > 100.0f) dt = 100.0f;
		if (wn != n) wcur = wmid = 0.0f;
		else if (wfirst != first) {
			int a0 = first < wfirst ? first : wfirst;
			int b0 = first < wfirst ? wfirst : first;
			float d = 0.0f;
			int j2;

			/* A STEP GIVES, A JUMP DOES NOT. Wrapping from the last row to
			 * the first moves the window the whole length of the list, and a
			 * shoulder button pages it by eight; easing across either drags
			 * rows in from the far end and reads as the list skipping. Only
			 * a single row's move is softened. */
			if (b0 - a0 > 1) {
				wcur = wmid = 0.0f;
			} else {
				for (j2 = a0; j2 < b0 && j2 < n; j2++) d += ROW_H(rows[j2]);
				wcur += (first > wfirst) ? d : -d;
				wmid += (first > wfirst) ? d : -d;
				if (wcur >  CAP) wcur =  CAP;
				if (wcur < -CAP) wcur = -CAP;
				if (wmid >  CAP) wmid =  CAP;
				if (wmid < -CAP) wmid = -CAP;
			}
		}
		wn = n; wfirst = first;
		if (dt > 0.0f) {
			float kk = 1.0f - expf(-dt / TAU);

			wmid -= wmid * kk;
			wcur += (wmid - wcur) * kk;
		}
		if (wcur < 0.5f && wcur > -0.5f) wcur = 0.0f;
		win_off = wcur;
	}

	if (sel >= 0 && sel < n && rows[sel].label) {
		static int   pl_n = -1, pl_sel = -1;
		static float pl_y, pl_h, pl_my, pl_mh;
		static unsigned pl_last;
		/* A CHASE, NOT A TIMED SLIDE. REPEAT_RATE_MS is 90 and this used to
		 * take 150, so holding the d-pad meant the plate never finished a
		 * move: it trailed a row behind, and on the wrap - which snaps - it
		 * jumped from halfway to the top. The last row before the wrap was
		 * being selected and never visibly highlighted, which is what it
		 * looked like from the outside.
		 *
		 * A chase has no duration to be interrupted. It closes whatever gap
		 * exists, so under repeat it keeps up and after a single press it
		 * eases. Two stages for the same reason the list uses two: one
		 * exponential moves furthest on its first frame. */
		/* 22ms, not 34. At 34 the plate is only 83% of the way across when
		 * the next repeat arrives, so holding the d-pad it never quite
		 * settles on a row - the last one before a wrap almost highlights and
		 * then the wrap takes it. 22 puts it at 96% within one repeat, which
		 * is the difference between a row being visited and a row being
		 * seen. Its first frame is still only 14px of 48, so a single press
		 * eases rather than snaps. */
		const float TAU = 22.0f;
		int ty_sel = content_y - scroll, jj;
		unsigned nowp = plat_now_ms();
		float dt = pl_last ? (float)(nowp - pl_last) : 0.0f;
		float tgt_h = (float)ROW_H(rows[sel]);
		SDL_Rect plate;

		pl_last = nowp;
		if (dt > 100.0f) dt = 100.0f;

		for (jj = first; jj < sel && jj < n; jj++) ty_sel += ROW_H(rows[jj]);
		ty_sel += (int)win_off;

		if (pl_n != n) {                       /* another menu: no journey */
			pl_y = pl_my = (float)ty_sel; pl_h = pl_mh = tgt_h;
		} else if (pl_sel != sel) {
			/* A step is a move to the next SELECTABLE row, whatever its
			 * index. Distance cannot tell a wrap from a step in a menu whose
			 * top rows are placeholders - the cursor goes 6 to 3 there, and
			 * a step past one placeholder goes 3 to 5. What separates them is
			 * whether anything selectable lies between. */
			int lo2 = sel < pl_sel ? sel : pl_sel;
			int hi2 = sel < pl_sel ? pl_sel : sel;
			bool jump = false;
			int q;

			for (q = lo2 + 1; q < hi2; q++)
				if (rows[q].label && rows[q].live) { jump = true; break; }
			if (jump) { pl_y = pl_my = (float)ty_sel; pl_h = pl_mh = tgt_h; }
		}
		pl_n = n; pl_sel = sel;

		if (dt > 0.0f) {
			float k = 1.0f - expf(-dt / TAU);

			pl_my += ((float)ty_sel - pl_my) * k;
			pl_mh += (tgt_h - pl_mh) * k;
			pl_y  += (pl_my - pl_y) * k;
			pl_h  += (pl_mh - pl_h) * k;
		}

		/* A soft white plate, not the system's color. The accent already
		 * frames the panel; using it again for the cursor made the two
		 * compete, and on a dark red system the plate read as a stain on the
		 * row rather than a highlight under it. White is neutral against all
		 * eleven accents.
		 *
		 * Flat, with no radial glow under it. The glow was brightest at the
		 * row's midpoint and fell off toward both ends, which put a soft blob
		 * behind the middle of every highlighted row and read as a smudge
		 * rather than as a selection. */
		plate.x = panel.x + pad / 2;
		plate.y = (int)(pl_y + 0.5f);
		plate.w = panel.w - pad;
		plate.h = (int)(pl_h + 0.5f);
		ui_round_rect(a->r, &plate, row_h / 4, (SDL_Color){ 255, 255, 255, 34 });

		plate_y = pl_y;
		plate_h = pl_h;
	}

	notes_drawn = 0;
	if (scroll_h)
		SDL_RenderSetClipRect(a->r, &(SDL_Rect){ panel.x, content_y,
		                                         panel.w, scroll_h });
	/* Clipped to the panel, and ONE ROW BEYOND THE WINDOW at each end.
	 * Offsetting the rows opens a gap at one edge and pushes the far row past
	 * the other; without the extra row the gap is empty, and without the clip
	 * the overflow draws outside the panel. Leaving either out is what made
	 * the first attempt at this look like the list was wrapping. */
	if (vis < n) {
		SDL_Rect rc = { panel.x + UI_PANEL_BORDER, content_y,
		                panel.w - UI_PANEL_BORDER * 2,
		                panel.y + panel.h - pad - desc_h - content_y };
		/* ChaOS: a panel stretched to the screen is taller than its rows;
		 * the clip stays at the rows, or the row past the window shows */
		if (g_menu_fill >= 0) {
			int rh = 0, j2;
			for (j2 = first; j2 < first + vis && j2 < n; j2++) rh += ROW_H(rows[j2]);
			if (rh < rc.h) rc.h = rh;
		}
		SDL_RenderSetClipRect(a->r, &rc);
	}
	for (k = 0, i = (first > 0 ? first - 1 : first);
	     i < n && i <= first + vis; k++, i++) {
		int y, ty, j;
		float sel_w;
		SDL_Color lc, vc;

		if (scroll_h && i >= scroll_split) {
			/* Pinned. Measured from the bottom of the viewport rather than
			 * from the top of the rows, so the footer does not move when the
			 * body does. The clip comes off here and stays off. */
			if (i == scroll_split) SDL_RenderSetClipRect(a->r, NULL);
			y = content_y + scroll_h;
			for (j = scroll_split; j < i; j++) y += ROW_H(rows[j]);
		} else {
			y = content_y - scroll + (int)win_off;
			if (i >= first) for (j = first; j < i; j++) y += ROW_H(rows[j]);
			else            for (j = i; j < first; j++) y -= ROW_H(rows[j]);
		}
		/* Centered in ITS OWN row, not in row_h: a note is shorter. */
		ty = y + (ROW_H(rows[i]) - text_h) / 2 + ink_off;

		/* The same bar as the heading's, at the same alpha and width. If one
		 * is ever retuned the other follows, which is the point. */
		if (!rows[i].label) {
			SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(a->r, (Uint8)(accent >> 16),
			                       (Uint8)(accent >> 8), (Uint8)accent, 70);
			SDL_RenderFillRect(a->r, &(SDL_Rect){ content_x,
			                                      y + (ROW_IS_HR(rows[i]) ? pad / 2
			                                                              : rule_h - 2),
			                                      content_w, 2 });
			continue;
		}

		/* A placeholder row still highlights - it is a real place on the list -
		 * and stays one step quieter than a working row, which is the whole
		 * signal that it does nothing yet. One step, though, not two: most of
		 * this list is placeholders, and ranking them against an unselected
		 * working row as well left the entire menu reading as grayed out. */
		/* `w` is how selected this row is RIGHT NOW, not whether it is the
		 * one the cursor points at: 1 once the plate has arrived, 0 before it
		 * sets off, and the two rows crossfade past each other in between. */
		{
			/* How much of the plate is over this row, 0 to 1. */
			float top = y > plate_y ? (float)y : plate_y;
			float bot = (float)(y + ROW_H(rows[i])) < plate_y + plate_h
			            ? (float)(y + ROW_H(rows[i])) : plate_y + plate_h;
			float w = (bot - top) / (float)ROW_H(rows[i]);

			if (w < 0.0f) w = 0.0f;
			if (w > 1.0f) w = 1.0f;

			lc = rows[i].live
			     ? col_mix(UI_TEXT_SOFT, UI_TEXT, w)
			     : col_mix(UI_TEXT_DIM, UI_TEXT_SOFT, w);
			sel_w = w;
		}
		/* The panel's own accent, not a->tint. These were the same value for
		 * as long as Display mode was the only live row, because that row is
		 * in the SYSTEM menu where the accent IS the system tint. The first
		 * live row in the TortOS menu made them diverge and drew a red value
		 * inside a cyan panel. `accent` is already MENU_ACCENT for one menu
		 * and the system tint for the other, which is the answer in both. */
		if (vcolors && vcolors[i])
			vc = (SDL_Color){ (Uint8)(vcolors[i] >> 16),
			                  (Uint8)(vcolors[i] >> 8),
			                  (Uint8)vcolors[i], 255 };
		else
			vc = rows[i].live
			     ? col_mix(UI_TEXT_DIM,
			               (SDL_Color){ (Uint8)(accent >> 16),
			                            (Uint8)(accent >> 8),
			                            (Uint8)accent, 255 }, sel_w)
			     : UI_TEXT_DIM;
		/* ChaOS: a row that opens another screen, with a chevron in the
		 * right margin: the value's color, so it brightens with the row */
		if (rows[i].opens && rows[i].live && !ROW_IS_NOTE(rows[i]))
			draw_chevron(a->r, (float)(content_x + content_w + pad / 2),
			             y + ROW_H(rows[i]) / 2.0f, (float)text_h * 0.5f, +1,
			             rows[i].value ? vc : lc);

		/* Trimmed to the panel, HERE, rather than by every caller.
		 *
		 * content_w is capped to the screen above, but nothing used to fit
		 * the text to it - the label was drawn from the left edge and the
		 * value right-aligned to the right, so a row wider than the cap drew
		 * them straight through each other and out past the border. Seen on
		 * the game info screen: "File" and "Alex Kidd in Miracle World
		 * (World) (Sega Ages).zip" overlapping in the middle of the panel.
		 *
		 * Callers were each remembering to call ui_fit_text, or forgetting.
		 * A rule about how text fits a panel belongs in the thing that draws
		 * the panel, where a screen written next year gets it for free.
		 *
		 * The VALUE yields first: labels are short and fixed ("File", "Size"),
		 * values are whatever the card happens to hold. */
		/* A bar across the text's whole width, in the shape of the
		 * selection plate: a faint track, filled in the panel's color as
		 * far as menu_bar_fill says. A sliver is drawn as a round end, so
		 * a download that has started never looks as if it hasn't. */
		if (ROW_IS_BAR(rows[i])) {
			int bh = text_h / 3 > 6 ? text_h / 3 : 6;
			SDL_Rect track = { content_x, y + (ROW_H(rows[i]) - bh) / 2, content_w, bh };
			SDL_Rect fill = track;

			g_menu_seen.bar_x = track.x;
			g_menu_seen.bar_y = track.y;
			g_menu_seen.bar_w = track.w;
			g_menu_seen.bar_h = track.h;
			fill.w = (int)((float)track.w * g_menu_bar + 0.5f);
			if (fill.w > 0 && fill.w < bh) fill.w = bh;
			SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
			ui_round_rect(a->r, &track, bh / 2, (SDL_Color){ 255, 255, 255, 34 });
			if (fill.w > 0)
				ui_round_rect(a->r, &fill, bh / 2,
				              (SDL_Color){ (Uint8)(accent >> 16), (Uint8)(accent >> 8),
				                           (Uint8)accent, 255 });
			continue;
		}
		if (ROW_IS_NOTE(rows[i])) {
			char note[192];

			/* A note that fits is centered, which is what a caption wants.
			 * One that does not SCROLLS rather than being cut: a note is a
			 * sentence, and Play Time's footer lost the last of four facts
			 * to an ellipsis - "longest 56m 30s · t..." - where the reader
			 * cannot even tell which word went. Same ping-pong and the same
			 * faded edges a long title gets on the shelf.
			 *
			 * Left-aligned in that case, not centered: the text is wider
			 * than the box, so centering it has nothing left to center.
			 *
			 * A body row is left-aligned whether it fits or not. It is one
			 * line of a paragraph, and a paragraph needs one left edge. */
			if (ui_text_width(fm, rows[i].label) <= content_w) {
				bool body = ROW_IS_BODY(rows[i]);

				ui_fit_text(fm, rows[i].label, note, sizeof note, content_w);
				ui_text(a->r, fm, note, body ? content_x : cx, ty,
				        body ? -1 : 0, lc);
			} else {
				int slot = MQ_NOTE0 + (notes_drawn < MQ_NOTES - 1
				                       ? notes_drawn : MQ_NOTES - 1);

				ui_text_marquee(a->r, fm, rows[i].label, content_x, ty,
				                content_w, mq_phase(slot, sel, i), lc);
			}
			notes_drawn++;
		} else if (two_col) {
			char lbl[192], val[192];
			int lw = ui_text_width(fm, rows[i].label);
			int vw = rows[i].value ? ui_text_width(fm, rows[i].value) : 0;
			int room;

			/* WHICHEVER IS LONGER IS THE CONTENT, and the content is what
			 * moves. The other one is a caption, and a caption keeps its
			 * natural width.
			 *
			 * On the play-time list the label is a game name and the value a
			 * duration, so the name scrolls and the number stays put; a
			 * duration pushed off the row it exists to report is no use to
			 * anyone. On the game info screen it is the other way round -
			 * "File" is the caption and the filename is the content - and
			 * the same rule sends the filename to the marquee without
			 * needing to know which screen it is on.
			 *
			 * The condition used to ask whether the label fit in what the
			 * value left, which answers a different question: with a long
			 * value that subtraction goes negative, so a two-letter label
			 * failed it and took the long-LABEL path. The filename was then
			 * drawn ellipsized across the full width with "File" printed on
			 * top of it, which is what sent me looking.
			 *
			 * Only the selected row moves, OR one the cursor can never reach
			 * - menu_run_body steps off any row whose `live` is false, so on
			 * a screen of facts, waiting for selection waits forever. Rows
			 * long enough to move are rare either way, and a page where
			 * everything is moving will not sit still to be read. */
			/* See menu_row_moves. The rule lives in menu.c because the
			 * last time it quietly meant something else, nothing could see
			 * it: the achievement list scrolled a title or cut it depending
			 * on whether the player had earned it. */
			bool moves = menu_row_moves(rows[i], i == sel, visits_all);

			if (rows[i].value) {         /* for --updater-art */
				g_menu_seen.value_x = content_x + content_w;
				g_menu_seen.value_y = ty;
			}

			if (rows[i].toggle && rows[i].value) vw = menu_switch_w(text_h);
			if (lw + (vw ? gap + vw : 0) + (rows[i].cycles ? text_h * 3 / 2 : 0) <= content_w) {
				ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, content_w);
				ui_text(a->r, fm, lbl, content_x, ty, -1, lc);
				if (rows[i].value && rows[i].toggle)
					draw_switch(a->r, content_x + content_w,
					            y + ROW_H(rows[i]) / 2.0f, text_h,
					            !strcasecmp(rows[i].value, "on"), rows[i].live,
					            sel_w, accent);
				else if (rows[i].value)
					ui_text(a->r, fm, rows[i].value,
					        content_x + content_w, ty, 1, vc);
				/* ChaOS: left and right change it - an arrow either side,
				 * on the row the cursor is on */
				if (rows[i].cycles && rows[i].live && rows[i].value &&
				    !rows[i].toggle && sel_w > 0.02f) {
					SDL_Color ac = vc;
					float cy = y + ROW_H(rows[i]) / 2.0f, ah = (float)text_h * 0.5f;

					ac.a = (Uint8)(ac.a * sel_w);
					draw_chevron(a->r, (float)(content_x + content_w - vw) - ah * 1.2f,
					             cy, ah, -1, ac);
					if (!rows[i].opens)
						draw_chevron(a->r, (float)(content_x + content_w + pad / 2),
						             cy, ah, +1, ac);
				}
			} else if (vw > lw) {
				/* The VALUE is the long one. The label keeps its width and
				 * the value takes everything left of the right edge. */
				ui_text(a->r, fm, rows[i].label, content_x, ty, -1, lc);
				room = content_w - lw - gap;
				if (room <= 0) {
					/* No room for it at all; the label alone is the row. */
				} else if (moves) {
					/* One slot for every long value on the screen, never one
					 * per row: keyed per row, a screen of facts with two of
					 * them reset the clock twice a frame and neither ever
					 * moved. Over The Hare showed it 2026-09-14 - "Transferred"
					 * and "Now" both too long, and two captures a second apart
					 * identical. Sharing the clock, they all move.
					 *
					 * WHICH clock depends on why this row moves, not on what
					 * is in it. See MQ_FIXED. */
					ui_text_marquee(a->r, fm, rows[i].value,
					                content_x + content_w - room, ty, room,
					                mq_row_phase(i == sel, heading, n, sel), vc);
				} else {
					ui_fit_text(fm, rows[i].value, val, sizeof val, room);
					ui_text(a->r, fm, val, content_x + content_w, ty, 1, vc);
				}
			} else {
				/* The LABEL is the long one: a game's name, an SSID. */
				room = content_w - (vw ? vw + gap : 0);
				if (rows[i].value) {
					ui_fit_text(fm, rows[i].value, val, sizeof val, content_w);
					ui_text(a->r, fm, val, content_x + content_w, ty, 1, vc);
				}
				if (room <= 0) {
					/* Nothing left for a name. Better a cut label than none. */
					ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, content_w);
					ui_text(a->r, fm, lbl, content_x, ty, -1, lc);
				} else if (moves) {
					ui_text_marquee(a->r, fm, rows[i].label, content_x, ty,
					                room,
					                mq_row_phase(i == sel, heading, n, sel), lc);
				} else {
					ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, room);
					ui_text(a->r, fm, lbl, content_x, ty, -1, lc);
				}
			}
		} else {
			char lbl[192];

			/* A centered list's row, under the two-column rule: the row that
			 * moves slides when it does not fit, rather than losing its end
			 * to an ellipsis. Left-aligned while it slides, the way a long
			 * note is - wider than the box, it has nothing to be centered in.
			 *
			 * This branch only ever cut. Found on Muse's track list
			 * 2026-09-19: "Bird Dream Of The Olympus Mons" ended in "M..."
			 * - and slid the moment a track of that album was playing,
			 * because the "playing" value put the list in two columns. */
			if (ui_text_width(fm, rows[i].label) > content_w &&
			    menu_row_moves(rows[i], i == sel, visits_all)) {
				ui_text_marquee(a->r, fm, rows[i].label, content_x, ty,
				                content_w,
				                mq_row_phase(i == sel, heading, n, sel), lc);
			} else {
				ui_fit_text(fm, rows[i].label, lbl, sizeof lbl, content_w);
				ui_text(a->r, fm, lbl, cx, ty, 0, lc);
			}
		}
	}
	if (vis < n) SDL_RenderSetClipRect(a->r, NULL);

	if (scroll_h) {
		/* Faded where the text runs out of viewport, and faded by laying the
		 * PANEL'S OWN FILL over it, which is the opposite of what the shelf
		 * does. There the background is a coverflow with a vignette, so a
		 * gradient painted on top would show as a band and the glyphs have to
		 * be faded instead. Here the surface under the text is one flat color,
		 * ui_panel's 22,24,32, so the cheap way is also the right one.
		 *
		 * A side fades only while something is hidden on it, so a card sitting
		 * at the top of its travel has a hard top edge and a soft bottom one,
		 * and says by that which way it is about to move. */
		const int fade = row_h;
		int e;

		SDL_RenderSetClipRect(a->r, NULL);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		for (e = 0; e < fade; e++) {
			Uint8 al = (Uint8)(252 - 252 * e / fade);

			SDL_SetRenderDrawColor(a->r, 22, 24, 32, al);
			if (scroll > 0)
				SDL_RenderFillRect(a->r, &(SDL_Rect){
					panel.x + UI_PANEL_BORDER, content_y + e,
					panel.w - UI_PANEL_BORDER * 2, 1 });
			if (scroll < scroll_span)
				SDL_RenderFillRect(a->r, &(SDL_Rect){
					panel.x + UI_PANEL_BORDER, content_y + scroll_h - 1 - e,
					panel.w - UI_PANEL_BORDER * 2, 1 });
		}
		SDL_RenderSetClipRect(a->r, NULL);
	}

	/* ChaOS: the selected row's hint, under a rule at the foot of the panel
	 * (see descs). Centred when it fits, sliding when it doesn't, as a note
	 * does; nothing for a row without one, and the rule stays. */
	if (descs) {
		TTF_Font *df = ui_font(UI_F_META);
		int dh = ui_font_height(UI_F_META);
		int fy = panel.y + panel.h - pad - desc_h;
		int dy = fy + rule_h + (desc_h - rule_h - dh) / 2;
		const char *h = sel >= 0 && sel < n ? rows[sel].hint : NULL;

		SDL_RenderSetClipRect(a->r, NULL);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, (Uint8)(accent >> 16), (Uint8)(accent >> 8),
		                       (Uint8)accent, 70);
		SDL_RenderFillRect(a->r, &(SDL_Rect){ content_x, fy + rule_h - 2, content_w, 2 });
		if (h && h[0] && df) {
			if (ui_text_width(df, h) <= content_w) {
				ui_text(a->r, df, h, cx, dy, 0, UI_TEXT_DIM);
			} else {
				unsigned ph = mq_phase(MQ_DESC, sel, n);

				ui_text_marquee(a->r, df, h, content_x, dy, content_w, ph, UI_TEXT_DIM);
				redraw_at(plat_now_ms() + 33);
			}
		}
	}

	/* Three dim dots where the list carries on, in the same vocabulary the
	 * slot carousel's rail uses. Hung just off the rows rather than centered in
	 * the padding: with a heading above, the padding is already spoken for by
	 * the separator, and the indicator belongs to the list in any case. */
	if (vis < n && !scroll_h) {
		/* A triangle pointing the way the list continues, rather than three
		 * dots that said "there is more" without saying which way. Drawn as
		 * rows because there is no filled-triangle primitive and this needs
		 * no texture. */
		const int tw = row_h / 3, th = tw / 2, off = 8;

		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 138, 143, 163, 200);
		for (k = 0; k < th; k++) {
			int up_w   = tw * (k + 1) / th;      /* apex at the top */
			int down_w = tw * (th - k) / th;     /* apex at the bottom */

			if (first > 0)
				SDL_RenderFillRect(a->r, &(SDL_Rect){ cx - up_w / 2,
				                   content_y - off - th + k, up_w, 1 });
			if (first + vis < n)
				SDL_RenderFillRect(a->r, &(SDL_Rect){ cx - down_w / 2,
				                   content_y + vis * row_h + off + k, down_w, 1 });
		}
	}
}

static void menu_draw(app *a, const char *heading, const menu_row *rows, int n,
                      int sel, int fixed_w, unsigned accent)
{
	menu_draw_ex(a, heading, rows, n, sel, fixed_w, accent, NULL, false, 0);
}

/* The two menus behind MENU, their row indices and their buffers: src/sys_menu.h */

/* Build whichever menu the current screen calls for. Returns the row count, so
 * the input loop never needs to know which of the two it is driving. */
/* One answer per two seconds, for every row that needs it.
 *
 * wifi_status forks wpa_cli, and this launcher is a 119 MB process - forking it
 * copies page tables and costs about thirty milliseconds, not the seven a small
 * process pays. menu_build runs every frame, and the TortOS menu asked three
 * times: once cached for the Wi-Fi row, then twice more, uncached, for Over The
 * Hare and Box Art. Measured on the device that was 12 fps with the menu open,
 * against a loop that delays 8 ms - two thirds of every frame spent forking.
 *
 * The cache existed and said why; the two rows below it defeated it. So the
 * cache moves out here where it covers all of them, and nothing calls
 * wifi_status directly from a per-frame path again. */
static wifi_state menu_wifi(char *ssid, int cap)
{
	static unsigned   next;
	static wifi_state st = WIFI_OFF;
	static char       name[WIFI_SSID_MAX];
	unsigned now = plat_now_ms();

	if (next == 0 || now >= next) {
		st = wifi_status(name, sizeof name, NULL, 0);
		next = now + 2000;
	}
	if (ssid && cap) snprintf(ssid, (size_t)cap, "%s", name);
	return st;
}

#ifndef TORTOS_VERSION
#define TORTOS_VERSION "0.0"       /* set by the makefiles from Makefile's VERSION */
#endif
/* ChaOS's own, from the CHAOS_VERSION file; TORTOS_VERSION stays the TortOS
 * it is built on, which is what the network services are told. */
#ifndef CHAOS_VERSION
#define CHAOS_VERSION "0.0"
#endif

/* ChaOS: the release feed (src/release.h) is ChaOS's, so what it is compared
 * with is the ChaOS version, never the TortOS base under it. */

/* ---- System Update: what the last check said (src/release.h) ---------------
 *
 * Read from the card once, and again after every check: MENU's row asks every
 * frame. The answer kept on the card is shown whatever its age, since "2.2.0
 * available" stays true until a newer check says otherwise; the screen checks
 * again once it is a day old. */
static update_state   g_up_state = UPDATE_UNKNOWN;
static update_release g_up_rel;
static char           g_up_value[40];
static bool           g_up_read;

static void up_path(char *out, size_t n, const char *name)
{
	snprintf(out, n, "%s/.update%s%s", P_CARD, name[0] ? "/" : "", name);
}

static void up_cache_path(char *out, size_t n)
{
	snprintf(out, n, "%s/update-check", P_USERDATA);
}

static void up_set_value(void)
{
	if (g_up_state == UPDATE_WAITING)
		snprintf(g_up_value, sizeof g_up_value, "%s available", g_up_rel.version);
	else if (g_up_state == UPDATE_CURRENT)
		snprintf(g_up_value, sizeof g_up_value, "up to date");
	else
		g_up_value[0] = '\0';
}

static void up_known(void)
{
	char path[CFG_STR * 2];

	if (g_up_read) return;
	g_up_read = true;
	up_cache_path(path, sizeof path);
	if (!update_cache_peek(path, CHAOS_VERSION, &g_up_state, &g_up_rel))
		g_up_state = UPDATE_UNKNOWN;
	up_set_value();
}

/* Gather what the device currently is, and hand it to sys_menu_build.
 *
 * The split: this half is allowed to fork wpa_cli and read the app, the other
 * half is not allowed to do anything but produce rows. That is what lets
 * tools/menu-check.c state a whole device situation in a struct initializer
 * and ask what the menu says about it.
 *
 * Returns the row count, so the input loop never needs to know which of the
 * two menus it is driving. */
static int menu_build(app *a, screen_id screen, int sys,
                      menu_row *out, menu_bufs *b, const char **heading)
{
	char ss[WIFI_SSID_MAX];
	sys_ui u = { 0 };

	/* Asked once. The cache is why - see menu_wifi - and three rows used to
	 * ask separately, which is how it got expensive in the first place. */
	u.wifi  = menu_wifi(ss, sizeof ss);
	u.ssid  = ss;
	u.games = screen == SCREEN_GAMES;
	u.no_wifi = !plat_has_wifi();
	u.no_bt   = !plat_has_bluetooth();
	u.cable_xfer = plat_cable_link(NULL, 0);

	if (u.games) {
		const system_cfg *sc = &a->sys.systems[sys];

		u.sys_name   = sc->name;
		u.sys_core   = sc->core;
		/* By what it HAS, not by its name or its position: a shelf with no
		 * core and no folder is the built one, and a check can hand that
		 * over without inventing a flag day. */
		u.muse       = is_muse(sc);
		u.fav        = !u.muse && sc->core[0] == '\0' && sc->folder[0] == '\0';
		u.game_count = a->view[sys].list.count;
		/* ChaOS: the shader row only for a console with a shader made for
		 * it, and the overlay by name. */
		{
			static char ovl[256];

			u.rewind  = rewind_load(sc->tag);
			u.shader  = look_console_of(sc->tag) == LOOK_NONE ? NULL
			          : a->view[sys].look.on ? "On" : "Off";
			u.overlay = u.fav || u.muse ? NULL : overlay_label(a, sys, ovl, sizeof ovl);
			u.dmode   = DMODES[a->view[sys].dmode].label;
			u.no_overlays = !u.fav && !u.muse && !overlay_count(a, sys);
		}
		u.muse_books = u.muse && muse_books_shown();
		u.muse_both  = u.muse && ml_count(&g_muse, true) && ml_count(&g_muse, false);
		u.muse_playing = u.muse && lock_allowed();
		u.sort       = u.muse ? ml_order_label((ml_order)a->view[sys].sort, u.muse_books)
		                      : SORTS[a->view[sys].sort].label;
	} else {
		aout_state ao = aout_now();

		u.ra_in     = ra_signed_in();
		u.ra_name   = u.ra_in ? ra_user() : NULL;
		u.ss_have   = ss_have_dev();
		u.pm_have   = ports_pm_ready(P_ROMS);
		u.romm      = romm_get()->token[0] ? (romm_get()->user[0] ? romm_get()->user : "signed in")
		                                   : "sign in";
		u.ss_in     = ss_signed_in();
		u.ss_name   = u.ss_in ? ss_user() : NULL;
		u.cards     = CARD_SETS[g_cards].name;
		u.cards_dir = CARD_DIRS[g_dir].name;
		u.name_speed = CARDS_DRAWN(g_cards) ? PN_SPEEDS[g_pn_speed[g_dir]].name : NULL;
		u.list_view = CARD_DIRS[g_dir].both ? NULL : g_list_view ? "List" : "Normal";
		u.recent    = g_recent_shelf ? "On" : "Off";
		u.fav_shelf = g_fav_shelf ? "On" : "Off";
		u.muse_shelf = g_muse_shelf ? "On" : "Off";
		u.night     = plat_volume_night_get() ? "On" : "Off";
		u.night_ok  = plat_volume_night_ok();
		u.appearance = g_appearance_page;
		u.menu_color = MENU_COLORS[g_menu_color].name;
		u.update    = upd_row_value();
		u.auto_off  = a->auto_off;
		{
			static char clk[32];

			clock_label(time(NULL), clk, sizeof clk);
			u.clock = clk;
		}
		u.audio_policy = ao.policy;
		u.audio_dest   = aout_actual(&ao);
		u.audio_channels = g_channels;

		/* The connected headset's name.
		 *
		 * Cached for two seconds, the same way the Wi-Fi row is and for the
		 * same reason: this is rebuilt every menu frame and the answer costs
		 * a fork. The first version read /tmp/tortos_btsink to avoid that,
		 * which is what made this row say "not connected" while the Bluetooth
		 * screen said the opposite - launch.sh writes that file on a
		 * twenty-second poll. One source for both now. */
		{
			static char btname[BT_NAME_MAX];
			static unsigned next_bt;
			unsigned now = plat_now_ms();

			if (next_bt == 0 || now >= next_bt) {
				bt_device bd[BT_MAX];
				int nb = bt_bonded(bd, BT_MAX), i;

				next_bt = now + 2000;
				btname[0] = '\0';
				if (nb > 0 && bt_status() == BT_READY) {
					bt_mark_connected_now(bd, nb);
					for (i = 0; i < nb; i++)
						if (bd[i].connected) {
							snprintf(btname, sizeof btname, "%s", bd[i].name);
							break;
						}
				}
			}
			u.bt_name = btname[0] ? btname : NULL;
		}
	}
	return sys_menu_build(&u, out, b, heading);
}


/* ---------- the WiFi screen ----------------------------------------------- */

/* One frame with a single line on it, drawn before each blocking call.
 *
 * wifi_up can take twenty seconds waiting out the stock service's retry loop,
 * a scan takes several, and an association takes up to twenty more. None of
 * that is asynchronous here, so the loop is not running and input is not read
 * while it happens. Painting the reason first is the difference between a
 * device that is working and a device that has hung: the picture is identical
 * otherwise, and the second reading is the one that gets a power cycle. */
/* One line on a panel over the shelf, for the moments something is happening
 * and there is nothing yet to show. The heading is a parameter because it was
 * not: this began as the Wi-Fi screen's own and said "Wi-Fi" over every
 * message it was given, so signing in to RetroAchievements and fetching a set
 * both announced themselves as Wi-Fi. */
static void wait_panel(app *a, const char *heading, const char *msg)
{
	menu_row row = { msg, NULL, false };

	draw_shelf(a);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
	SDL_RenderFillRect(a->r, NULL);
	menu_draw(a, heading, &row, 1, -1, 0, MENU_ACCENT);
	plat_draw_osd(a->r);
	plat_present();
}

/* MENU CLOSES THE WHOLE MENU, from any depth in it. B is back one level.
 * Eric's call, 2026-09-23: MENU used to mean exactly what B meant, so from
 * Play Time or Wi-Fi it went back one screen instead of out, the same button
 * doing one thing to open the menu and another inside it.
 *
 * A menu screen that sees MENU raises this and leaves; every screen under it
 * then leaves on its next pass, until the one MENU opened is gone. It is put
 * down again only OUTSIDE the menus - the shelf's loop, game_menu, Muse - and
 * never when a menu opens: a screen that opens a second one after the first
 * came back has to see it still up, or that second one would stay open. */
static bool g_menu_closing;

/* True when a menu screen should leave: B, MENU, or a MENU pressed deeper in. */
static bool menu_leaving(app *a)
{
	if (a->in.pressed[IN_MENU]) g_menu_closing = true;
	return a->in.pressed[IN_BACK] || g_menu_closing;
}

/* A yes/no panel, drawn like wait_panel but answerable.
 *
 * Defaults to "no": the selection starts on the safe row, so a stray press of
 * the button that opened the panel cannot also confirm it. Returns false for
 * BACK, for MENU, and for a power-off, because everything except a deliberate
 * press on the destructive row means "not that".
 *
 * Power is handled here rather than left to the caller. A confirm can sit on
 * screen indefinitely, which makes it exactly the kind of place the device gets
 * put down and the power button pressed. */
static bool confirm_panel(app *a, const char *heading, const char *msg,
                          const char *yes_label)
{
	menu_row rows[3];
	int sel = 2;

	rows[0] = (menu_row){ msg, NULL, false };
	rows[1] = (menu_row){ yes_label, NULL, true };
	rows[2] = (menu_row){ "Cancel", NULL, true };

	for (;;) {
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; return false; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return false; }
		if (menu_leaving(a)) return false;

		/* Only the two answerable rows are reachable; row 0 is the question. */
		if (in_repeat(&a->in, IN_UP) || in_repeat(&a->in, IN_DOWN))
			sel = (sel == 1) ? 2 : 1;
		if (a->in.pressed[IN_ACCEPT]) return sel == 1;

		tick_tint(a);
		draw_shelf(a);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw(a, heading, rows, 3, sel, 0, MENU_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(8);
	}
}

/* ---- the menu runner -------------------------------------------------- */

/* Wi-Fi is the largest menu: the switch, WIFI_MAX_NETS networks, a rule and a
 * note. Distinct from MENU_MAX_ROWS, which is the system menu's own count and
 * is much smaller. */
#define MENU_RUN_ROWS (WIFI_MAX_NETS + 8)

typedef enum {
	MENU_STAY,   /* keep the screen up */
	MENU_DONE    /* leave it */
} menu_result;

/* How a screen ended, for the callers that have to act differently. The
 * in-game menu is the one that does: B and MENU mean Continue there, and it
 * has to send RESUME - but the runner owns those two buttons and flushes the
 * input on the way out, so asking a->in afterwards would always say no. */
typedef enum {
	MENU_LEFT_BACK,     /* B or MENU, the ordinary way out */
	MENU_LEFT_SCREEN,   /* the screen's own handler said so */
	MENU_LEFT_GONE      /* quit, power, or nothing left to show */
} menu_exit;

/* Fill `rows`, name the screen, and return how many rows. MUST NOT touch the
 * renderer or the event loop, and the part that actually decides what the rows
 * SAY must be a separate function in a file the check can link - src/wifi_menu.c
 * and src/sys_menu.c are those files. The design has failed if that stops
 * being true.
 *
 * Reading the device from here is allowed and unavoidable: a row that reports
 * the network has to ask every frame. What makes it checkable is that the
 * asking is confined to this half - wifi.c is stubbed on a host build, and the
 * system menu's asking lives in menu_build rather than in sys_menu_build.
 *
 * Called every frame, so it is also where live state is reflected - there is no
 * separate "refresh".
 *
 * The heading comes from here because it is a function of the same state the
 * rows are: the system menu is titled with the system whose rows it is showing.
 * Passing it to the runner separately meant keeping two things in step that are
 * really one thing. */
typedef int (*menu_build_fn)(void *ctx, menu_row *rows, int max,
                             const char **heading);

/* How the panel is drawn, as opposed to what is in it. Everything here needs
 * the renderer or the app, which is exactly why it is not in the build
 * function. */
typedef struct {
	/* 0: measure these rows. The system menu passes menu_shelf_width, a width
	 * measured once across every system and every display mode, because a
	 * panel that resizes while you cycle a value on it reads as a glitch. */
	int      fixed_w;
	unsigned accent;
	/* The shelf's animated tint instead of `accent`, read per frame. The
	 * system menu on a game shelf belongs to that system, and tick_tint is
	 * still moving toward its color while the menu is open. */
	bool     follow_tint;
	/* What goes behind the panel. NULL is the shelf, tinted and dimmed, which
	 * is what every screen reached from the shelf wants. The in-game menu
	 * draws the paused game instead, because the shelf is not what is under
	 * it. Called before the panel, every frame. */
	void   (*backdrop)(app *a, void *ctx);
	/* What the power button and the idle timer mean here. NULL powers the
	 * device off. The in-game menu stops the GAME instead: the device is not
	 * going anywhere, and the wait loop below it has to be told, or it goes
	 * straight back to waiting on a game nobody is running. */
	menu_result (*on_power)(app *a, void *ctx);
	/* ChaOS: a first menu, not a submenu: no back chevron (g_menu_root). */
	bool     root;
	/* ChaOS: holding Y hides the panel, so the game behind an in-game menu
	 * can be seen as it now looks. The backdrop leaves out its dimming
	 * while Y is held too (gm_backdrop). */
	bool     peek;
} menu_style;

/* SELECT: Muse, over whatever is on screen. Declared here, after menu_style,
 * because it borrows two of its hooks. */
static bool muse_open(app *a, const menu_style *over, void *ctx);

/* Read every frame through the caller's pointer, so a screen that keeps its
 * style inside its own context can change it between frames. The info screen
 * does: its accent belongs to the system that OWNS the game, and toggling a
 * favorite on the Favorites shelf can hand it a different one. */

/* A key the runner did not consume, and the row it happened on. Free to open a
 * confirm, a keyboard or a wait panel: each runs its own loop and returns.
 * `sel` indexes the rows the last build produced. */
typedef menu_result (*menu_key_fn)(app *a, void *ctx, in_button key, int sel);

/* The loop itself. Called only through menu_run below, which owns the flush on
 * either side of it. */
static menu_exit menu_run_body(app *a, const menu_style *st,
                               menu_build_fn build, menu_key_fn on_key,
                               void *ctx)
{
	menu_row rows[MENU_RUN_ROWS];
	const char *heading = NULL;
	int sel = 0, n = 0, b;

	while (a->running && !want_quit) {
		n = build(ctx, rows, MENU_RUN_ROWS, &heading);
		if (n > MENU_RUN_ROWS) n = MENU_RUN_ROWS;
		if (n <= 0) return MENU_LEFT_GONE;

		/* The list is rebuilt every frame and can shrink under the cursor -
		 * a rescan finding fewer networks, a row going away. Clamp first,
		 * then land on something live. */
		if (sel >= n) sel = n - 1;
		if (sel < 0) sel = 0;
		if (!rows[sel].live) sel = menu_step_sel(rows, n, sel, +1);

		plat_input_poll(&a->in);
		/* Where the sound goes, followed in the menus too: a headset connected
		 * from the Bluetooth screen, or a cable plugged in, only took the sound
		 * once the menus were left - seen 2026-09-26. It sends only on a change.
		 * And Muse is read here as well: its answers - where it now is, and the
		 * end of a track, which is when the next is sent - waited for the menus
		 * to be left, so the row read stale and an album stopped at the end of a
		 * song while a menu was open. #45. */
		muse_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; return MENU_LEFT_GONE; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			if (!st->on_power) { power_off(a); return MENU_LEFT_GONE; }
			if (st->on_power(a, ctx) == MENU_DONE) return MENU_LEFT_GONE;
		}
		if (menu_leaving(a))
			return MENU_LEFT_BACK;
		/* The in-game menu too: its game is paused underneath, and Muse
		 * borrows its backdrop and its power rule for as long as it is up.
		 * Rebuilt on the way back, because anything may have changed while
		 * Muse covered it - a Bluetooth headset connecting, for one. */
		if (a->in.pressed[IN_SELECT]) {
			if (muse_open(a, st, ctx)) return MENU_LEFT_GONE;
			continue;
		}

		if (in_repeat(&a->in, IN_UP))   sel = menu_step_sel(rows, n, sel, -1);
		if (in_repeat(&a->in, IN_DOWN)) sel = menu_step_sel(rows, n, sel, +1);

		/* Volume and brightness keep working here, as they do everywhere.
		 *
		 * They were missed when this runner was written, so the Wi-Fi screen
		 * spent a day as the one screen in the launcher where the volume keys
		 * did nothing. That is the exact failure this runner exists to make
		 * impossible, arriving by the one route it could still come from: the
		 * runner forgetting, rather than a screen forgetting. Every other loop
		 * in this file carries these four lines by hand. */
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		/* Left and right reach the screen on REPEAT, not just on press, so a
		 * row whose value cycles can be held down. Everything else is edge
		 * triggered: holding A on a row that opens a screen should open it
		 * once. Sent before the press loop below, which skips them. */
		if (on_key && in_repeat(&a->in, IN_LEFT))
			if (on_key(a, ctx, IN_LEFT, sel) == MENU_DONE)
				return MENU_LEFT_SCREEN;
		if (on_key && in_repeat(&a->in, IN_RIGHT))
			if (on_key(a, ctx, IN_RIGHT, sel) == MENU_DONE)
				return MENU_LEFT_SCREEN;
		if (!a->running) return MENU_LEFT_GONE;

		/* Everything the runner did not consume goes to the screen, so a
		 * screen-specific key needs no runner change to exist. */
		for (b = 0; b < IN_COUNT; b++) {
			if (!a->in.pressed[b]) continue;
			if (b == IN_LEFT || b == IN_RIGHT) continue;
			if (b == IN_UP || b == IN_DOWN || b == IN_BACK || b == IN_MENU
			    || b == IN_POWER || b == IN_VOLUP || b == IN_VOLDN
			    || b == IN_BRIGHTUP || b == IN_BRIGHTDN) continue;
			if (on_key && on_key(a, ctx, (in_button)b, sel) == MENU_DONE)
				return MENU_LEFT_SCREEN;
			if (!a->running) return MENU_LEFT_GONE;
			/* The screen it opened was left with MENU: out of this one too,
			 * without drawing it again first. */
			if (g_menu_closing) return MENU_LEFT_BACK;
			/* A handler may have run a modal and changed everything under
			 * us; rebuild before drawing rather than drawing stale rows. */
			break;
		}

		tick_tint(a);
		if (st->backdrop) {
			st->backdrop(a, ctx);
		} else {
			draw_shelf(a);
			SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
			SDL_RenderFillRect(a->r, NULL);
		}
		if (!(st->peek && a->in.down[PEEK_KEY])) {
			g_menu_root = st->root;
			menu_draw(a, heading, rows, n, sel, st->fixed_w,
			          st->follow_tint ? a->tint : st->accent);
			g_menu_root = false;
		}
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(8);
	}
	return MENU_LEFT_GONE;
}

/* One screen, one loop. Owns polling, power, idle, quit, back, volume,
 * brightness, the cursor and the whole frame, so that no screen can forget any
 * of them - which every menu defect this project has had came down to.
 *
 * The flush is here rather than in the body: the press that opens a screen must
 * not also close it, and the press that closes one must not arrive again at
 * whatever is underneath. This file does that by hand about twenty times, and
 * the body has nine ways out, so putting it at each of them would eventually
 * mean missing one. */
static menu_exit menu_run(app *a, const menu_style *st,
                          menu_build_fn build, menu_key_fn on_key, void *ctx)
{
	menu_exit how;

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	how = menu_run_body(a, st, build, on_key, ctx);

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return how;
}

static void wifi_backdrop(void *ctx)
{
	app *a = ctx;
	draw_shelf(a);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 150);
	SDL_RenderFillRect(a->r, NULL);
}

static menu_result wifi_key(app *a, void *ctx, in_button key, int sel)
{
	wifi_ui *w = ctx;
	char status[96], ssid[WIFI_SSID_MAX], ip[64];
	int k = sel - 1;                    /* row 0 is the switch */
	bool net_row = sel >= 1 && sel <= w->n;

	if (key == IN_Y && w->on) { wifi_begin_scan(w); return MENU_STAY; }

	/* Forget a saved network. Only saved rows offer it - there is nothing to
	 * forget about one the device has never joined, and a button that silently
	 * does nothing on some rows is the same defect as a menu entry wired to
	 * nothing.
	 *
	 * Forgetting the CONNECTED network is allowed, deliberately. Refusing would
	 * be safer - it drops the device off the LAN, taking Over The Hare and ssh
	 * with it, and a handheld has no keyboard to climb back out with. But
	 * wanting to forget the network you are on is reasonable, and the honest
	 * place to say what it costs is the confirm. */
	if (key == IN_X && w->on && net_row && w->nets[k].known) {
		char cur[WIFI_SSID_MAX], msg[160];
		bool joined = wifi_status(cur, sizeof cur, NULL, 0) == WIFI_CONNECTED
		              && !strcmp(cur, w->nets[k].ssid);

		if (joined)
			snprintf(msg, sizeof msg,
			         "Forget %s? You are connected to it and will lose "
			         "the network.", w->nets[k].ssid);
		else
			snprintf(msg, sizeof msg, "Forget %s?", w->nets[k].ssid);

		if (confirm_panel(a, "Wi-Fi", msg, "Forget")) {
			snprintf(status, sizeof status,
			         wifi_forget(w->nets[k].ssid) ? "Forgot %s"
			                                      : "Could not forget %s",
			         w->nets[k].ssid);
			wait_panel(a, "Wi-Fi", status);
			SDL_Delay(1400);
			if (w->on) wifi_begin_scan(w);
		}
		return MENU_STAY;
	}

	if (key != IN_ACCEPT) return MENU_STAY;

	/* The switch. Saved on every change rather than on the way out: the way out
	 * of a handheld is often the power button. */
	if (sel == 0) {
		if (w->on) {
			wifi_down();
			wifi_pref_save(false);
			w->on = false;
			w->n = 0;
		} else {
			wait_panel(a, "Wi-Fi", "Turning Wi-Fi on...");
			w->on = wifi_up();
			wifi_pref_save(w->on);
			if (!w->on) {
				wait_panel(a, "Wi-Fi", "Wi-Fi did not come up");
				SDL_Delay(1800);
			}
			if (w->on) wifi_begin_scan(w);
		}
		return MENU_STAY;
	}

	if (net_row) {
		char psk[80] = "";
		bool ok;

		/* A saved network already has its passphrase in wpa_supplicant.conf,
		 * so asking again would be asking the user to retype something the
		 * device is holding. An open network has none to ask for. */
		if (w->nets[k].secured && !w->nets[k].known) {
			kb_result kr = kb_prompt(a->r, &a->in, w->nets[k].ssid,
			                         psk, (int)sizeof psk, MENU_ACCENT,
			                         wifi_backdrop, idle_due_ctx, a);
			if (kr == KB_POWER) { power_off(a); return MENU_DONE; }
			if (kr != KB_ACCEPT) return MENU_STAY;
		}

		wait_panel(a, "Wi-Fi", "Connecting...");
		ok = wifi_connect(w->nets[k].ssid, psk[0] ? psk : NULL);
		/* Wiped as soon as it has been handed over. It still exists in the
		 * supplicant's config, which is the point, but there is no reason for
		 * a copy to sit in the launcher's stack afterwards. */
		memset(psk, 0, sizeof psk);

		if (ok) {
			/* Connecting is turning it on, whatever the switch said. */
			wifi_pref_save(true);
			w->on = true;
			wifi_status(ssid, sizeof ssid, ip, sizeof ip);
			snprintf(status, sizeof status, "Connected to %s", ssid);
		} else {
			snprintf(status, sizeof status, "Could not connect to %s",
			         w->nets[k].ssid);
		}
		wait_panel(a, "Wi-Fi", status);
		SDL_Delay(1800);
		/* Out on success, because the job is done and the menu row behind this
		 * screen already names the network - staying in the list makes you back
		 * out by hand to see the result. On failure stay, because the next
		 * thing wanted is another try or another network, and both are here. */
		if (ok) return MENU_DONE;
		wifi_begin_scan(w);
	}
	return MENU_STAY;
}

static void wifi_screen(app *a)
{
	wifi_ui w = { 0 };

	/* Entering does not switch the radio on. It used to, which made the screen
	 * impossible to leave in the off state: you opened it to turn wifi OFF and
	 * the act of opening it turned wifi on. */
	w.on = wifi_status(NULL, 0, NULL, 0) != WIFI_OFF;
	if (w.on) wifi_begin_scan(&w);

	menu_run(a, &(menu_style){ .accent = MENU_ACCENT,
	                           .fixed_w = menu_std_width(a) },
	         wifi_build, wifi_key, &w);
}


/* ---------- About --------------------------------------------------------- */


/* Facts about the machine, which is a different thing from settings. The
 * address in particular has nowhere else to live: the Wi-Fi screen names the
 * network, and short of asking the router there is no way to find out what
 * address the device took. */
/* Signing in to RetroAchievements. Two prompts and a request; the account
 * screen is deliberately not a screen, because there is nothing to look at
 * until there is something to say.
 *
 * The password is used to get a token and then wiped. Nothing stores it -
 * ra.cfg holds the token, which is what every later call uses anyway. */
static void ra_signin_screen(app *a)
{
	char user[RA_USER_MAX] = "", pass[96] = "", err[160] = "";
	menu_row row;
	kb_result kr;
	bool ok;

	snprintf(user, sizeof user, "%s", ra_user());

	if (!net_online()) {
		row = (menu_row){ "Not on a network. Connect Wi-Fi first.", NULL, false };
		wifi_backdrop(a);
		menu_draw(a, "RetroAchievements", &row, 1, -1, 0, MENU_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(1600);
		return;
	}

	kr = kb_prompt(a->r, &a->in, "RetroAchievements User", user,
	               (int)sizeof user, MENU_ACCENT, wifi_backdrop, idle_due_ctx, a);
	if (kr == KB_POWER) { power_off(a); return; }
	if (kr != KB_ACCEPT || !user[0]) return;

	kr = kb_prompt(a->r, &a->in, "Password", pass,
	               (int)sizeof pass, MENU_ACCENT, wifi_backdrop, idle_due_ctx, a);
	if (kr == KB_POWER) { memset(pass, 0, sizeof pass); power_off(a); return; }
	if (kr != KB_ACCEPT || !pass[0]) { memset(pass, 0, sizeof pass); return; }

	wait_panel(a, "RetroAchievements", "Signing in...");
	ok = ra_sign_in(user, pass, err, sizeof err);
	/* Wiped the moment it has been used, the same as the Wi-Fi passphrase.
	 * The token it bought is the thing worth keeping. */
	memset(pass, 0, sizeof pass);

	if (ok) {
		ra_creds_save();
		snprintf(err, sizeof err, "Signed in as %s", ra_user());
	} else if (!err[0]) {
		snprintf(err, sizeof err, "Sign-in failed");
	}

	row = (menu_row){ err, NULL, ok };
	wifi_backdrop(a);
	menu_draw(a, "RetroAchievements", &row, 1, -1, 0, MENU_ACCENT);
	plat_draw_osd(a->r);
	plat_present();
	SDL_Delay(1800);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* The ScreenScraper account. The same shape as the screen above, and the
 * differences are the interesting part.
 *
 * There is nothing to trade the password for. RetroAchievements returns a
 * token and the password is dropped here and never stored; ScreenScraper signs
 * every request with the password itself, so what ss_sign_in keeps is a
 * reusable credential and the only "sign-in" available is asking them whether
 * they answer to it. The password is still wiped from THIS screen's stack the
 * moment it has been handed over, because a buffer that outlives its use is
 * the one thing a screen can get wrong on its own.
 *
 * The third state is the one RetroAchievements does not have: a build made
 * without SS_DEVID cannot sign anyone in, and says so rather than taking a
 * password it has no way to use. See src/ss.h. */
static void ss_signin_screen(app *a)
{
	char user[SS_USER_MAX] = "", pass[SS_PASS_MAX] = "", err[160] = "";
	menu_row row;
	kb_result kr;
	bool ok;

	snprintf(user, sizeof user, "%s", ss_user());

	if (!ss_have_dev() || !net_online()) {
		row = (menu_row){ !ss_have_dev()
		                  ? "This build has no ScreenScraper key."
		                  : "Not on a network. Connect Wi-Fi first.", NULL, false };
		wifi_backdrop(a);
		menu_draw(a, "ScreenScraper", &row, 1, -1, 0, MENU_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(1600);
		return;
	}

	kr = kb_prompt(a->r, &a->in, "ScreenScraper User", user,
	               (int)sizeof user, MENU_ACCENT, wifi_backdrop, idle_due_ctx, a);
	if (kr == KB_POWER) { power_off(a); return; }
	if (kr != KB_ACCEPT || !user[0]) return;

	kr = kb_prompt(a->r, &a->in, "Password", pass,
	               (int)sizeof pass, MENU_ACCENT, wifi_backdrop, idle_due_ctx, a);
	if (kr == KB_POWER) { memset(pass, 0, sizeof pass); power_off(a); return; }
	if (kr != KB_ACCEPT || !pass[0]) { memset(pass, 0, sizeof pass); return; }

	wait_panel(a, "ScreenScraper", "Signing in...");
	ok = ss_sign_in(user, pass, err, sizeof err);
	memset(pass, 0, sizeof pass);

	/* ss_sign_in stores the account itself, because it is the half that knows
	 * whether the reply was real. Nothing to save here. */
	if (ok) snprintf(err, sizeof err, "Signed in as %s", ss_user());
	else if (!err[0]) snprintf(err, sizeof err, "Sign-in failed");

	row = (menu_row){ err, NULL, ok };
	wifi_backdrop(a);
	menu_draw(a, "ScreenScraper", &row, 1, -1, 0, MENU_ACCENT);
	plat_draw_osd(a->r);
	plat_present();
	SDL_Delay(1800);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* ChaOS: the ScreenScraper page, MENU > ScreenScraper: the player's account
 * and Sign Out. Reached only in a build with a developer key (the menu row
 * says "not in this build" and stays shut otherwise). The account is kept in
 * the device database, on the card outside ChaOS's own files, so an Update or
 * a Full Install leaves it where it is. */
enum { SSM_NONE, SSM_ACCOUNT, SSM_SIGN_OUT };

typedef struct {
	app  *a;
	int   ids[8];
	char  acct[SS_USER_MAX + 8];
} ssm_ctx;

static int ssm_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	ssm_ctx *c = ctx;
	int n = 0;

	(void)max;
	*heading = "ScreenScraper";
	snprintf(c->acct, sizeof c->acct, "%s", ss_signed_in() ? ss_user() : "Sign In");
	c->ids[n] = SSM_ACCOUNT;
	rows[n++] = (menu_row){ "Account", c->acct, true };
	if (ss_signed_in()) {
		c->ids[n] = SSM_SIGN_OUT;
		rows[n++] = (menu_row){ "Sign Out", NULL, true };
	}
	c->ids[n] = SSM_NONE; rows[n++] = MENU_RULE;
	c->ids[n] = SSM_NONE;
	rows[n++] = MENU_NOTE("Kept through every update");
	return n;
}

static menu_result ssm_key(app *a, void *ctx, in_button key, int sel)
{
	ssm_ctx *c = ctx;
	int id = sel >= 0 && sel < 8 ? c->ids[sel] : SSM_NONE;

	if (key != IN_ACCEPT) return MENU_STAY;
	if (id == SSM_ACCOUNT) {
		ss_signin_screen(a);
	} else if (id == SSM_SIGN_OUT &&
	           confirm_panel(a, "ScreenScraper", "Sign out of ScreenScraper?", "Sign Out")) {
		ss_creds_clear();
		ss_creds_save();
	}
	return MENU_STAY;
}

/* For screenshots (--ss): the page as it stands */
static void ss_shot(app *a)
{
	ssm_ctx c;
	menu_row rows[8];
	const char *heading;
	int n;

	memset(&c, 0, sizeof c);
	c.a = a;
	n = ssm_build(&c, rows, 8, &heading);
	menu_draw(a, heading, rows, n, 0, 0, MENU_ACCENT);
}

static void ss_screen(app *a)
{
	ssm_ctx c;

	if (!ss_have_dev()) return;     /* the row is shut then; this is belt and braces */
	memset(&c, 0, sizeof c);
	c.a = a;
	menu_run(a, &(menu_style){ .accent = MENU_ACCENT, .fixed_w = 0 }, ssm_build, ssm_key, &c);
}

/* Over The Hare: the address, the PIN, and what is happening.
 *
 * The whole screen is a waiting room. It exists so somebody can read two
 * things off a handheld and type them into a laptop, and then watch enough to
 * know it is working. Everything real happens in hare.c and in a browser
 * somewhere else on the network.
 *
 * The server lives exactly as long as this screen. Closing it stops
 * listening, forgets the PIN and drops every session - which is the honest
 * answer to "how long is my ROM folder on the network for". */
/* Bytes, in a unit a person reads rather than the one the counter is in.
 *
 * The shot harness was handed "41 MB out" as a literal while this screen could
 * only ever print kilobytes - a picture of a screen that did not exist, which
 * is the exact failure the shared row builder was meant to stop. Sharing the
 * layout is not enough if the fixture claims the content can be something it
 * cannot.
 *
 * Decimal, matching human() in res/web/app.js. These two describe the same
 * bytes to the same person a foot apart, so they have to divide by the same
 * thing, and decimal is what every file manager but Windows Explorer shows -
 * 1024 under a KB/MB label is the one combination that is wrong everywhere. */
static void human_bytes(char *out, size_t n, unsigned long long b)
{
	if (b < 1000ull)              snprintf(out, n, "%llu B", b);
	else if (b < 1000ull * 1000)  snprintf(out, n, "%llu KB", b / 1000);
	else if (b < 1000ull * 1000 * 1000)
		snprintf(out, n, "%.1f MB", (double)b / (1000 * 1000));
	else snprintf(out, n, "%.1f GB", (double)b / (1000ull * 1000 * 1000));
}

/* The address and the PIN go in the HEADING, not in rows.
 *
 * They were four rows of equal weight, and rendered it was obvious that the
 * two things this screen exists to be read off were in the dimmest color the
 * launcher has: menu_draw paints a value UI_TEXT_DIM unless its row is both
 * live and selected, and nothing on a status screen is either. The heading is
 * UI_F_LABEL at UI_TEXT_SOFT - larger and brighter - and takes two lines when
 * it contains a newline, which is exactly two things worth copying.
 *
 * Everything else is status and belongs below the rule. */
static void hare_head(char *out, size_t n, const char *addr, const char *pin)
{
	snprintf(out, n, "%s\nPIN %s", addr, pin);
}

static int hare_rows(menu_row *out, const char *who, const char *moved,
                     const char *now)
{
	out[0] = (menu_row){ "Browsers", who,   false };
	/* "In / out", not "Transferred": the longest label on the panel beside
	 * "12 KB in / 41 MB out" scrolled on every real session - just browsing
	 * moves bytes both ways, so both numbers are never zero - and a still
	 * caught it mid-word. Rendered 2026-09-30, this fits even at
	 * "1.2 GB / 413.5 MB". */
	out[1] = (menu_row){ "In / out", moved, false };
	out[2] = (menu_row){ "Now",      now,   false };
	return 3;
}

/* One frame of it, for the shot harness. The rows come from the same
 * function the screen uses, so a picture of this cannot quietly stop being a
 * picture of that. */
void hare_preview(app *a, const char *addr, const char *pin, const char *who,
                  const char *moved, const char *now)
{
	menu_row rows[3];
	char head[192];
	int n = hare_rows(rows, who, moved, now);

	hare_head(head, sizeof head, addr, pin);
	draw_shelf(a);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
	SDL_RenderFillRect(a->r, NULL);
	menu_draw(a, head, rows, n, -1, menu_std_width(a), MENU_ACCENT);
}

/* Download logs, from Over The Hare: see logpack.h. The names to hide are
 * gathered here, at the moment of the download, because only the launcher
 * knows them - every saved network rather than just the one it is on, since
 * an older boot's log can name another; every paired headset; both account
 * names. about.txt carries the first questions asked about any report. */
static app *g_logs_app;

static bool pack_logs(char *path, size_t pn, char *name, size_t nn)
{
	wifi_net   nets[WIFI_MAX_NETS];
	bt_device  bt[BT_MAX];
	const char *secrets[WIFI_MAX_NETS + BT_MAX + 4];
	char       now_ssid[WIFI_SSID_MAX] = "", ip[64], logs[CFG_STR * 2];
	char       folder[64], about[4096], err[128], stamp[32];
	int        n = 0, saved, nb, i;
	size_t     at = 0;
	time_t     t = time(NULL);
	struct tm  tm;
	struct statvfs vf;
	double     up = 0;
	FILE      *f;

	saved = wifi_known(nets, WIFI_MAX_NETS);
	for (i = 0; i < saved; i++) secrets[n++] = nets[i].ssid;
	if (wifi_status(now_ssid, sizeof now_ssid, ip, sizeof ip) != WIFI_OFF && now_ssid[0])
		secrets[n++] = now_ssid;
	nb = bt_bonded(bt, BT_MAX);
	for (i = 0; i < nb; i++) secrets[n++] = bt[i].name;
	if (ra_signed_in() && ra_user()) secrets[n++] = ra_user();
	if (ss_signed_in() && ss_user()) secrets[n++] = ss_user();

	localtime_r(&t, &tm);
	strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &tm);
	strftime(folder, sizeof folder, "chaos-logs-%Y-%m-%d-%H%M", &tm);
	if ((f = fopen("/proc/uptime", "r"))) { if (fscanf(f, "%lf", &up) != 1) up = 0; fclose(f); }

#define ABOUT(...) do { \
		int k_ = snprintf(about + at, sizeof about - at, __VA_ARGS__); \
		if (k_ > 0 && (size_t)k_ < sizeof about - at) at += (size_t)k_; \
	} while (0)
	ABOUT("ChaOS %s, from TortOS %s\n", CHAOS_VERSION, TORTOS_VERSION);
	ABOUT("Packed %s, up %.0f minutes\n", stamp, up / 60);
	if (statvfs(P_CARD, &vf) == 0)
		ABOUT("Card: %.1f GB free of %.1f GB\n",
		      (double)vf.f_bavail * vf.f_frsize / 1e9, (double)vf.f_blocks * vf.f_frsize / 1e9);
	ABOUT("Auto Off: %d s\n", g_logs_app ? g_logs_app->auto_off : -1);
	ABOUT("Wi-Fi: %s\n", now_ssid[0] ? now_ssid : "not connected");
	ABOUT("Paired headsets: %d\n", nb);
	ABOUT("\nShelves:\n");
	for (i = 0; g_logs_app && i < g_logs_app->sys.count; i++) {
		const system_cfg *sc = &g_logs_app->sys.systems[i];

		if (is_muse(sc))
			ABOUT("  %-22s %d albums, %d books, %d tracks\n", sc->name,
			      ml_count(&g_muse, false), ml_count(&g_muse, true), g_muse.ntracks);
		else
			ABOUT("  %-22s %d\n", sc->name, g_logs_app->view[i].list.count);
	}
#undef ABOUT

	snprintf(logs, sizeof logs, "%s/logs", P_USERDATA);
	snprintf(name, nn, "%s.tar.gz", folder);
	snprintf(path, pn, "/tmp/%s", name);
	if (!logpack_build(logs, about, secrets, n, folder, path, err, sizeof err)) {
		fprintf(stderr, "logs: could not pack them: %s\n", err);
		return false;
	}
	fprintf(stderr, "logs: packed %s, %d names masked\n", name, n);
	return true;
}

/* A folder about to be deleted whole from Over The Hare, or a folder or file
 * about to be renamed there: if Muse is playing or paused on that file or on
 * one in that folder, stop Muse first. The shelf is rebuilt when the screen
 * closes, as for any change there; this is only so the player is not left
 * holding a file that is gone. */
static void muse_before_delete(const char *abs)
{
	const char *p = musec_path();
	char full[LIB_PATH * 2];
	size_t n = strlen(abs);

	if (!p[0]) return;
	snprintf(full, sizeof full, "%s/%s", P_CARD, p);
	if (!strncmp(full, abs, n) && (full[n] == '/' || !full[n])) {
		fprintf(stderr, "muse: stopped, what it played is being moved or deleted\n");
		musec_stop();
	}
}

/* Rows of the settings keyed "<kind><folder>" or "<kind><folder>/...", which
 * a folder renamed with everything in it takes along. Gathered first and
 * changed after, so the rows are not rewritten under the read. */
typedef struct key_move { struct key_move *next; char *key, *val; } key_move;
typedef struct { size_t n; key_move *list; } key_moves;

static bool key_under(const char *key, const char *value, void *ctx)
{
	key_moves *m = ctx;
	key_move *k;

	/* "Album" and "Album/CD1", not "Album 2" */
	if (key[m->n] && key[m->n] != '/') return true;
	if (!(k = calloc(1, sizeof *k))) return false;
	k->key = strdup(key);
	k->val = strdup(value);
	k->next = m->list;
	m->list = k;
	return true;
}

static void keys_follow(db *d, const char *kind, const char *frel, const char *trel)
{
	char prefix[LIB_PATH + 16], nkey[LIB_PATH * 2 + 32];
	key_moves m = { 0, NULL };
	key_move *k;

	if (snprintf(prefix, sizeof prefix, "%s%s", kind, frel) >= (int)sizeof prefix) return;
	m.n = strlen(prefix);
	db_each_prefix(d, prefix, key_under, &m);
	while ((k = m.list)) {
		m.list = k->next;
		if (k->key && k->val &&
		    snprintf(nkey, sizeof nkey, "%s%s%s", kind, trel, k->key + m.n) < (int)sizeof nkey) {
			db_del(d, k->key);
			db_set_str(d, nkey, k->val);
		}
		free(k->key);
		free(k->val);
		free(k);
	}
}

/* A book's place names its file ("<seconds>\t<file>"): when that file was
 * renamed, the place follows. With `to` NULL the file's new name is the one
 * Tidy names would have given it, if that is there now. */
static void book_file_follows(const char *folder, const char *from, const char *to)
{
	char key[LIB_PATH + 300], val[LIB_PATH + 32], tidy[256], there[LIB_PATH * 2 + 300];
	char *tab;

	snprintf(key, sizeof key, "book.%s", folder);
	if (!db_get_str(db_dev(), key, val, sizeof val, "") || !(tab = strchr(val, '\t'))) return;
	if (from && strcmp(tab + 1, from)) return;
	if (!to) {
		if (!ml_tidy_file(tab + 1, tidy, sizeof tidy)) return;
		snprintf(there, sizeof there, "%s/%s/%s", P_CARD, folder, tidy);
		if (access(there, F_OK) != 0) return;
		to = tidy;
	}
	snprintf(tab + 1, sizeof val - (size_t)(tab + 1 - val), "%s", to);
	db_set_str(db_dev(), key, val);
}

/* The transfer page renamed a music or audiobook folder or file, by hand or
 * with Tidy names, which may also have renamed the files in the folder: what
 * is kept under the old names follows. An album's cover, from the .media
 * beside it; what Album Art remembers of it and a book's place, keyed by the
 * folder's path, for it and any folder in it; and the shelf held in memory,
 * until it is read again on the way out of the screen, so the page's lists
 * find the album's cover meanwhile. */
static void muse_after_rename(const char *from, const char *to, bool tidied)
{
	size_t card = strlen(P_CARD), fl;
	const char *frel, *trel, *fbase = strrchr(from, '/'), *tbase = strrchr(to, '/');
	static const char *const EXT[] = { "jpg", "png" };
	char cfrom[LIB_PATH * 2 + 300], cto[LIB_PATH * 2 + 300];
	struct stat st;
	int k;

	if (strncmp(from, P_CARD, card) || from[card] != '/' ||
	    strncmp(to, P_CARD, card) || to[card] != '/' || !fbase || !tbase)
		return;
	frel = from + card + 1;
	trel = to + card + 1;
	fl = strlen(frel);
	/* A file renamed by hand: its track, and a book's place if it was there */
	if (stat(to, &st) == 0 && !S_ISDIR(st.st_mode)) {
		for (k = 0; k < g_muse.ntracks; k++)
			if (!strcmp(g_muse.tracks[k].path, frel))
				snprintf(g_muse.tracks[k].path, sizeof g_muse.tracks[k].path, "%s", trel);
		/* A rename names a sibling, so the folder is the same on both */
		snprintf(cfrom, sizeof cfrom, "%.*s", (int)(fbase - frel), frel);
		book_file_follows(cfrom, fbase + 1, tbase + 1);
		fprintf(stderr, "muse: %s is now %s\n", frel, trel);
		return;
	}
	/* Each track at its new path. Tidy names renamed a file in the folder
	 * itself by ml_tidy_file unless that name was taken, so the same rule
	 * finds it again; anything else kept its name. */
	for (k = 0; k < g_muse.ntracks; k++) {
		ml_track *t = &g_muse.tracks[k];
		char tidy[256], np[LIB_PATH], there[LIB_PATH * 2 + 300];
		const char *file;

		if (strncmp(t->path, frel, fl) || t->path[fl] != '/') continue;
		file = t->path + fl + 1;
		if (tidied && !strchr(file, '/') && ml_tidy_file(file, tidy, sizeof tidy)) {
			snprintf(there, sizeof there, "%s/%s/%s", P_CARD, trel, tidy);
			if (access(there, F_OK) == 0) file = tidy;
		}
		if (snprintf(np, sizeof np, "%s/%s", trel, file) < (int)sizeof np)
			snprintf(t->path, sizeof t->path, "%s", np);
	}
	if (strcmp(frel, trel)) {
		for (k = 0; k < 2; k++) {
			snprintf(cfrom, sizeof cfrom, "%.*s/.media/%s.%s", (int)(fbase - from), from,
			         fbase + 1, EXT[k]);
			if (access(cfrom, F_OK) != 0) continue;
			snprintf(cto, sizeof cto, "%.*s/.media/%s.%s", (int)(fbase - from), from,
			         tbase + 1, EXT[k]);
			rename(cfrom, cto);
		}
		keys_follow(db_lib(), "museart.", frel, trel);
		keys_follow(db_dev(), "book.", frel, trel);
	}
	if (tidied) book_file_follows(trel, NULL, NULL);
	if (strcmp(frel, trel)) fprintf(stderr, "muse: %s is now %s\n", frel, trel);
}

/* Album covers for the page, down with Muse's Album Art, and the covers the
 * shelf draws, down with Muse's shelf. */
static bool  cover_file(int al, char *out, size_t n);
static void  cover_recheck(int al);
static bool  cover_answers(void);
static void  singles_cover(void);
static void  muse_file_albums(void);
static char *albums_wanted(size_t *len, char *why, size_t wn);
static char *album_pick(int id, const char *json, size_t n, size_t *len,
                        char *why, size_t wn);
static void  xfer_after_write(const char *abs);

/* Down To The Wire over the cable, or Over The Hare over Wi-Fi. MENU's row
 * goes by whether a computer is on the cable (xfer_screen); the welcome on an
 * empty card names both, so it says which. */
/* An update the Down To The Wire page sent (#69): hare.c takes the image into
 * .update/ and says so through wire_up_received; the screen's loop below
 * checks it against GitHub's SHA-256 a slice at a time, so the page keeps
 * getting answers, then asks on the device and makes System Update's
 * handoff. */
static void up_handoff(app *a, const char *heading, char *note, size_t n);   /* below */
typedef enum { WU_NONE, WU_CHECK, WU_HASHING, WU_ASK } wire_up_phase;
static wire_up_phase  g_wu;
static update_release g_wu_offer;

static void wire_up_received(const update_release *offer)
{
	g_wu_offer = *offer;
	g_wu = WU_CHECK;
}


static void xfer_screen_over(app *a, bool over_cable)
{
	char       ssid[WIFI_SSID_MAX], ip[64];
	char       addr[96], pinbuf[32], who[64], moved[64], head[192];
	menu_row   rows[3];
	hare_stats st;
	unsigned long long total_in = 0, total_out = 0;
	bool       done = false;
	/* Over the USB cable or over the air, which is also the screen's name. */
	const bool  cable = over_cable;
	const char *name  = cable ? XFER_NAME_CABLE : XFER_NAME_WIFI;
	unsigned    cover_at = 0, cover_next = 0;   /* the embedded covers, below */
	/* The update the page sent: its file being hashed, the confirm's row, and
	 * what the Now row says in place of the server's last word until that
	 * changes. */
	static uint8_t wu_chunk[1 << 18];
	char        wu_path[CFG_STR * 3], wu_now[64] = "", wu_last[128] = "";
	int         wu_fd = -1, wu_sel = 6;
	long long   wu_hashed = 0;
	sha256      wu_h;

	g_logs_app = a;
	/* A fresh link over the cable each time this opens: a computer can drop
	 * it while the device believes it is still up (plat_cable_reconnect).
	 * Charging held back first, as the Brick's data over the cable breaks
	 * while it charges hard (plat_cable_slow_charge); every way out puts it
	 * back. */
	if (cable) {
		plat_cable_slow_charge(true);
		plat_cable_reconnect();
	}
	hare_set_logs(pack_logs);
	hare_set_before_delete(muse_before_delete);
	hare_set_after_rename(muse_after_rename);
	/* Box art fetched by the page is Down To The Wire's alone (hareart.h);
	 * a cover uploaded into a shelf's .media is shrunk on every device. */
	hareart_init(&a->sys, P_ROMS);
	singles_cover();                /* back, if it was taken off the card */
	if (cable) {
		hare_set_art(hareart_wanted, hareart_match, hareart_crc, hareart_nointro);
		hare_set_albums(albums_wanted, album_pick);
	} else {
		hare_set_art(NULL, NULL, NULL, NULL);
		hare_set_albums(NULL, NULL);
	}
	/* The page wears the screen's name. */
	hare_set_page(cable ? "wire.html" : NULL);
	hare_set_after_write(xfer_after_write);
	hare_set_cover(cover_from_web);
	g_wu = WU_NONE;
	if (cable) {
		char dir[CFG_STR * 3];

		up_path(dir, sizeof dir, "");
		hare_set_update(dir, CHAOS_VERSION, plat_device_name(), wire_up_received);
	} else {
		hare_set_update(NULL, NULL, NULL, NULL);
	}
	if (!hare_start(P_ROMS, P_CARD, P_SHARED, P_WEB)) {
		menu_row row = { "Could not start", NULL, false };

		draw_shelf(a);
		menu_draw(a, name, &row, 1, -1, 0, MENU_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(1800);
		plat_cable_slow_charge(false);
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		return;
	}

	snprintf(pinbuf, sizeof pinbuf, "%s", hare_pin());
	addr[0] = '\0';

	while (!done && !want_quit && a->running) {
		static unsigned next_check;
		unsigned now = plat_now_ms();
		int busy;

		/* The launcher's slice of the transfer. Bounded, like everything else
		 * in a frame - see httpd.h. */
		busy = hare_poll();
		hare_status(&st);
		total_in  += st.in;
		total_out += st.out;

		/* The update the page sent: opened, hashed 25 ms a frame, then asked. */
		if (g_wu == WU_CHECK) {
			up_path(wu_path, sizeof wu_path, g_wu_offer.file);
			wu_fd = open(wu_path, O_RDONLY | O_CLOEXEC);
			sha256_start(&wu_h);
			wu_hashed = 0;
			g_wu = wu_fd >= 0 ? WU_HASHING : WU_NONE;
			if (wu_fd < 0) hare_update_state("refused", "it couldn't be read");
		} else if (g_wu == WU_HASHING) {
			unsigned until = now + 25;
			ssize_t got = 1;

			while (got > 0 && plat_now_ms() < until)
				if ((got = read(wu_fd, wu_chunk, sizeof wu_chunk)) > 0) {
					sha256_add(&wu_h, wu_chunk, (size_t)got);
					wu_hashed += got;
				}
			if (got <= 0) {
				uint8_t digest[32];
				char hex[65], why[64];
				bool charging = false, readable;
				int pct = 0;

				close(wu_fd);
				wu_fd = -1;
				sha256_finish(&wu_h, digest, hex);
				readable = plat_battery(&pct, &charging);
				g_wu = WU_NONE;
				if (got < 0 || wu_hashed != g_wu_offer.size || strcmp(hex, g_wu_offer.sha256)) {
					fprintf(stderr, "update: what the page sent doesn't match: %lld bytes, %s\n",
					        wu_hashed, hex);
					unlink(wu_path);
					hare_update_state("refused", "it didn't match GitHub's SHA-256");
					snprintf(wu_now, sizeof wu_now, "update didn't match");
				} else if (!update_battery_ok(readable, charging, pct)) {
					snprintf(why, sizeof why, "Plug in or charge above %d%%",
					         UPDATE_BATTERY_MIN_PCT);
					hare_update_state("refused", why);
					snprintf(wu_now, sizeof wu_now, "update needs charge");
				} else {
					fprintf(stderr, "update: %s from the page matches its SHA-256\n",
					        g_wu_offer.file);
					hare_update_state("asking", NULL);
					wu_sel = 6;           /* Not now, as every confirm starts */
					g_wu = WU_ASK;
				}
				if (wu_now[0]) snprintf(wu_last, sizeof wu_last, "%s", st.last);
			}
		}
		/* The Now row goes back to the server's word once it has a new one. */
		if (wu_now[0] && strcmp(wu_last, st.last)) wu_now[0] = '\0';

		/* Every album's embedded cover copied out to .media, where the page
		 * can show it and Album Art can judge it: an album the shelf never
		 * drew had none there, so the page showed nothing and asked
		 * MusicBrainz over a cover the files already carried (Eric,
		 * 2026-10-05). cover_file asks Muse as the shelf does, one album
		 * each tenth of a second, again for any Muse has not answered. */
		muse_poll();
		cover_answers();
		muse_file_albums();             /* read again on the way out, if moved */
		if (g_cov && g_muse.nalbums > 0 && now - cover_at >= 100u) {
			char pic[LIB_PATH * 2 + 8];

			int al = (int)(cover_next++ % (unsigned)g_muse.nalbums);

			cover_at = now;
			cover_recheck(al);
			cover_file(al, pic, sizeof pic);
		}

		/* Two seconds, because wifi_status forks wpa_cli and this loop runs
		 * every frame. Same cache the About screen uses. Over a cable the
		 * address never changes, so it is always shown, and the browser row
		 * says whether anyone is on the other end. */
		if (!addr[0] || now - next_check > 2000u) {
			next_check = now;
			if (cable ? plat_cable_link(ip, sizeof ip)
			          : wifi_status(ssid, sizeof ssid, ip, sizeof ip) == WIFI_CONNECTED
			            && ip[0]) {
				/* Port 80 needs no colon, and the address is being copied by
				 * hand off a three-inch screen - so it is only shown when it
				 * is not the one everybody assumes. */
				if (hare_port() == 80)
					snprintf(addr, sizeof addr, "%s", ip);
				else
					snprintf(addr, sizeof addr, "%s:%d", ip, hare_port());
			} else {
				snprintf(addr, sizeof addr, "Wi-Fi went away");
			}
		}

		if (st.clients == 0)      snprintf(who, sizeof who, "waiting");
		else if (st.clients == 1) snprintf(who, sizeof who, "1 connected");
		else                      snprintf(who, sizeof who, "%d connected", st.clients);

		if (total_in || total_out) {
			char hin[24], hout[24];

			human_bytes(hin,  sizeof hin,  total_in);
			human_bytes(hout, sizeof hout, total_out);
			snprintf(moved, sizeof moved, "%s / %s", hin, hout);
		} else {
			snprintf(moved, sizeof moved, "nothing yet");
		}

		hare_head(head, sizeof head, addr, pinbuf);
		hare_rows(rows, who, moved, wu_now[0] ? wu_now : st.last[0] ? st.last : "waiting");

		plat_input_poll(&a->in);
		if (a->in.quit_requested) {
			if (wu_fd >= 0) close(wu_fd);
			g_wu = WU_NONE;
			hare_stop();
			plat_cable_slow_charge(false);
			a->running = false;
			return;
		}

		/* Auto Off must not shut the device down in the middle of a 900MB
		 * upload, and it also must not shut it down while somebody is reading
		 * a listing on the other side of the room. Bytes moving is obvious
		 * activity; a connected browser is a person at the other end of it,
		 * which is the same claim a button press makes and no weaker for
		 * arriving over a network. Neither suspends Auto Off - close the tab
		 * and the countdown resumes. */
		if (busy || st.clients > 0 || g_wu != WU_NONE) a->idle.since_ms = now;

		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			if (wu_fd >= 0) close(wu_fd);
			g_wu = WU_NONE;
			hare_stop();
			plat_cable_slow_charge(false);   /* so charging while off isn't held */
			power_off(a);
			return;
		}
		if (g_wu == WU_ASK) {
			/* The confirm: Update now hands over as System Update does; Not
			 * now, B or MENU keeps the image for next time. */
			bool leave = menu_leaving(a);

			if (in_repeat(&a->in, IN_UP) || in_repeat(&a->in, IN_DOWN))
				wu_sel = wu_sel == 5 ? 6 : 5;
			if (a->in.pressed[IN_ACCEPT] && wu_sel == 5) {
				update_release kept = g_up_rel;
				char head2[48], note[64] = "";
				unsigned t0;

				g_wu = WU_NONE;
				hare_update_state("updating", NULL);
				/* A moment for the page to hear it before the server goes. */
				for (t0 = plat_now_ms(); plat_now_ms() - t0 < 600; SDL_Delay(4))
					hare_poll();
				g_up_rel = g_wu_offer;
				snprintf(head2, sizeof head2, "Update to %s", g_wu_offer.version);
				up_handoff(a, head2, note, sizeof note);
				/* Back here means it didn't happen; note says why. */
				g_up_rel = kept;
				hare_update_state("refused", note);
				snprintf(wu_now, sizeof wu_now, "update didn't start");
				snprintf(wu_last, sizeof wu_last, "%s", st.last);
			} else if (leave || a->in.pressed[IN_ACCEPT]) {
				g_wu = WU_NONE;
				hare_update_state("notnow", NULL);
				snprintf(wu_now, sizeof wu_now, "update kept for later");
				snprintf(wu_last, sizeof wu_last, "%s", st.last);
			}
		} else if (menu_leaving(a)) {
			done = true;
		}

		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		tick_tint(a);
		draw_shelf(a);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		if (g_wu == WU_CHECK || g_wu == WU_HASHING) {
			menu_row checking = { "Checking the update...", NULL, false };

			menu_draw(a, name, &checking, 1, -1, 0, MENU_ACCENT);
		} else if (g_wu == WU_ASK) {
			char ask[48];
			menu_row confirm[] = {
				MENU_NOTE("The computer sent it."),
				MENU_NOTE("The device restarts"),
				MENU_NOTE("to update."),
				MENU_NOTE("Games and saves stay."),
				MENU_HR,
				{ "Update now", NULL, true },
				{ "Not now", NULL, true },
			};

			snprintf(ask, sizeof ask, "Update to %s?", g_wu_offer.version);
			menu_draw(a, ask, confirm, 7, wu_sel, menu_std_width(a), MENU_ACCENT);
		} else {
			menu_draw(a, head, rows, 3, -1, menu_std_width(a), MENU_ACCENT);
		}
		plat_draw_osd(a->r);
		plat_present();
		/* Shorter than the usual 8ms: this loop is also the server's, and a
		 * transfer moves POLL_BUDGET per pass. */
		SDL_Delay(4);
	}

	if (wu_fd >= 0) close(wu_fd);
	g_wu = WU_NONE;

	/* Asked before hare_stop, which is what the screen owning the server
	 * means: after it, there is nobody left to ask. */
	{
		bool changed = hare_shelf_changed();

		hare_stop();
		plat_cable_slow_charge(false);
		/* The point of the whole feature is getting ROMs onto the card, and
		 * the shelf is scanned once at startup - so without this the file
		 * lands, the screen says it landed, and the game is not there. The
		 * only way to see it was to restart the launcher.
		 *
		 * On the way out rather than as each upload finishes: a transfer of
		 * twenty games would otherwise rescan twenty times, and every one of
		 * them would stall the loop that is still receiving. */
		if (changed) {
			wait_panel(a, name, "Scanning...");
			rescan_all(a);
		}
	}
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

static void xfer_screen(app *a)
{
	xfer_screen_over(a, plat_cable_link(NULL, 0));
}

/* The welcome on an empty card: no games and no music, as on a card fresh
 * from the installer. A blank shelf is what a broken launcher looks like, so
 * instead of naming folders it offers the two ways files come over from a
 * computer, and opens either: Down To The Wire once a computer is on the
 * cable, Over The Hare once Wi-Fi is joined, with the Wi-Fi row beside it
 * until then. A device with no Wi-Fi at all (a Pixel 2 without an adapter)
 * has only the cable. It goes when the shelf has anything on it, which
 * leaving either screen sees to: they rescan when files landed. */
enum { WELCOME_WIRE, WELCOME_HARE, WELCOME_WIFI };

static int welcome_rows(menu_row *rows, int *kind)
{
	const bool radio = plat_has_wifi();
	bool open = false;
	int  n = 0;

	if (radio) {
		rows[n++] = MENU_NOTE("Add games and music");
		rows[n++] = MENU_NOTE("from a computer:");
	} else {
		rows[n++] = MENU_NOTE("Add games and music from");
		rows[n++] = MENU_NOTE("a computer, by USB cable:");
	}
	rows[n++] = MENU_HR;
	kind[n] = WELCOME_WIRE;
	rows[n++] = plat_cable_link(NULL, 0)
	          ? (menu_row){ XFER_NAME_CABLE, "USB", true }
	          : (menu_row){ XFER_NAME_CABLE, "no cable", false };
	if (radio) {
		wifi_state st = menu_wifi(NULL, 0);

		kind[n] = WELCOME_HARE;
		rows[n++] = st == WIFI_CONNECTED
		          ? (menu_row){ XFER_NAME_WIFI, "Wi-Fi", true }
		          : (menu_row){ XFER_NAME_WIFI, "needs Wi-Fi", false };
		if (st != WIFI_CONNECTED) {
			kind[n] = WELCOME_WIFI;
			rows[n++] = (menu_row){ "Wi-Fi", st == WIFI_CONNECTING ? "connecting" :
			                                 st == WIFI_IDLE ? "not connected" : "off", true };
		}
	}
	/* The key only where there is something for it to open */
	for (int i = 0; i < n; i++) open |= rows[i].live;
	if (open) {
		rows[n++] = MENU_RULE;
		rows[n++] = MENU_NOTE("A: open");
	}
	return n;
}

/* The cursor, kept on a row that can be opened: rows come and go as the
 * cable and Wi-Fi do, and it starts on the first one that is live. -1 while
 * none is. */
static int welcome_sel = -1;

static int welcome_cursor(const menu_row *rows, int n)
{
	if (welcome_sel >= 0 && welcome_sel < n && rows[welcome_sel].live) return welcome_sel;
	welcome_sel = menu_step_sel(rows, n, -1, +1);
	if (welcome_sel < 0 || welcome_sel >= n || !rows[welcome_sel].live) welcome_sel = -1;
	return welcome_sel;
}

static void welcome_draw(app *a)
{
	menu_row rows[12];
	int      kind[12];
	int      n = welcome_rows(rows, kind);

	menu_draw(a, "Welcome to ChaOS", rows, n, welcome_cursor(rows, n),
	          menu_std_width(a), MENU_ACCENT);
}

/* The shelf's keys while the welcome is up. MENU and the power button are
 * the main loop's, as on any shelf; B has nothing behind it to go back to. */
static void welcome_update(app *a)
{
	menu_row rows[12];
	int      kind[12];
	int      n = welcome_rows(rows, kind), sel = welcome_cursor(rows, n);

	if (sel < 0) return;
	if (in_repeat(&a->in, IN_UP))   welcome_sel = menu_step_sel(rows, n, sel, -1);
	if (in_repeat(&a->in, IN_DOWN)) welcome_sel = menu_step_sel(rows, n, sel, +1);
	if (!a->in.pressed[IN_ACCEPT]) return;
	switch (kind[sel]) {
	case WELCOME_WIRE: xfer_screen_over(a, true);  break;
	case WELCOME_HARE: xfer_screen_over(a, false); break;
	case WELCOME_WIFI: wifi_screen(a);             break;
	}
	redraw_now();
}

/* Box art, from libretro. See artscrape.h for the matching rule and why it is
 * that one.
 *
 * Starts as soon as the screen opens rather than asking again. The player has
 * already pressed A on a row called Box Art; a confirmation would be a second
 * question with the same answer. It is safe to start because it is safe to
 * stop: art already on the card costs no request, so B is free and so is
 * running the whole thing again tomorrow.
 *
 * Deliberately NOT automatic. It is a network request on someone's behalf,
 * and a launcher that quietly fetches two hundred images the first time a
 * card is inserted has made that decision for them. */
/* The heading and rows, in one place so a shot cannot picture a screen that
 * does not exist - the lesson from --hare, where the fixture claimed a byte
 * count the code could not produce. */
static void art_head(char *out, size_t n, const char *now)
{
	char fit[128];

	/* Trimmed to fit. A ROM name is as long as somebody's dump of it -
	 * "Legend of Zelda, The - A Link to the Past (USA)" and worse - and this
	 * is a heading, which menu_draw centers rather than wraps, so a long one
	 * ran off both sides of the panel. Three quarters of the screen leaves
	 * the panel a margin it can keep. */
	ui_fit_text(ui_font(UI_F_LABEL), now, fit, sizeof fit,
	            TORTOS_SCREEN_W * 3 / 4);
	snprintf(out, n, "Box Art\n%s", fit);
}

static int art_rows(menu_row *out, const char *where, const char *counts,
                    bool working)
{
	out[0] = (menu_row){ "Systems", where,  false };
	out[1] = (menu_row){ "Art",     counts, false };
	out[2] = (menu_row){ working ? "B to stop" : "B to close", NULL, false };
	return 3;
}

void art_preview(app *a, const char *now, const char *where,
                 const char *counts, bool working)
{
	menu_row rows[3];
	char head[192];
	int n = art_rows(rows, where, counts, working);

	art_head(head, sizeof head, now);
	draw_shelf(a);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
	SDL_RenderFillRect(a->r, NULL);
	menu_draw(a, head, rows, n, -1, menu_std_width(a), MENU_ACCENT);
}

/* game_info, gi_row and gi_rows: src/game_menu.h */

static void gi_gather(app *a, int owner, const game_entry *g, game_info *gi)
{
	const system_cfg *s = &a->sys.systems[owner];
	char p[LIB_PATH * 3];
	struct stat st;

	memset(gi, 0, sizeof *gi);

	/* The save slots are not read here any more. The row that showed them went
	 * on 2026-09-17 - the carousel shows them when you load, with the frames
	 * rather than a count - and with it went a stat of every slot plus the
	 * autosave each time this screen opened. */
	box_art_path(s->folder, g->name, p, sizeof p);
	gi->has_art = (stat(p, &st) == 0 && st.st_size > 0);
	gi->is_fav = fav_is(a->sys.systems[owner].tag, g->file);

	/* Loading the set clobbers whatever set is loaded, which is nobody's
	 * during shelf browsing - no game is running. It stays loaded after this
	 * returns, which is what lets the Cheevos row open the list: the same set
	 * this count was taken from is the one that screen reads. */
	chv_path(P_ROMS, s->folder, g->name, p, sizeof p);
	gi->has_cheevos = chv_load(p) && chv_count() > 0;
	if (gi->has_cheevos)
		snprintf(gi->cheevos, sizeof gi->cheevos, "%d/%d, %d/%d points",
		         chv_earned(), chv_count(),
		         chv_points_earned(), chv_points_total());
	else
		snprintf(gi->cheevos, sizeof gi->cheevos, "none");

	/* What a scrape left on the card, if anything. Read here with the rest
	 * rather than when the synopsis card opens: this is one indexed row, the
	 * screen already reads the save directory and the achievement set, and a
	 * row that appears a frame after the panel does reads as a glitch. */
	{
		game_meta m;

		if (db_game_get(db_lib(), s->folder, g->file, &m)) {
			gi->scraped = true;
			gi->has_synopsis = m.synopsis[0] != '\0';
			snprintf(gi->year, sizeof gi->year, "%.4s", m.year);
			/* The scrape's own English names, in its own order - but
			 * joined with a comma AND A SPACE, which it does not do. That is
			 * typesetting rather than an opinion about somebody else's
			 * taxonomy: "Platform,Shoot'em Up" reads as one word with a
			 * stumble in it. One in three games has a second genre. */
			{
				const char *g = m.genres;
				size_t k = 0;

				while (*g && k + 2 < sizeof gi->genre) {
					gi->genre[k++] = *g;
					if (*g++ == ',' && *g != ' ') gi->genre[k++] = ' ';
				}
				gi->genre[k] = '\0';
			}
		}
	}
}

/* `only` names one system's folder, or NULL for the whole library.
 *
 * One system is worth having for the FIRST run on a card, where it is the
 * difference between twenty images and a hundred and eighty. It is worth much
 * less afterwards: since the direct-name pass landed, a whole-library re-run
 * over a full card is nine directory sweeps and no network at all. */
static void art_screen(app *a, const char *only, const char *one,
                       const char *stem, const char *one_file, unsigned accent)
{
	menu_row     rows[3];
	art_progress p;
	char         counts[64], where[64], head[192];
	int          nrows = 3;
	bool         done = false, working = true;
	/* Whether the one game already had a cover, so a miss can say it kept it
	 * rather than read as though the cover went. */
	bool         had = one && stem && only && has_box_art(only, stem);

	if (!net_online()) {
		menu_row row = { "No network", NULL, false };

		draw_shelf(a);
		menu_draw(a, "Box Art", &row, 1, -1, 0, accent);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(1600);
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		return;
	}

	/* The scraper needs the one async slot, and an account answer from a game
	 * just quit may still be holding it. */
	sync_abandon();

	/* SCREENSCRAPER FIRST, FOR ONE GAME. That is the settled order: them
	 * when the player has an account, libretro when they do not and libretro
	 * again when they cannot answer - a miss, a refusal, or a name that does
	 * not survive the check.
	 *
	 * Only the single-game path for now. The bulk run is the same idea over
	 * 1,708 games and a different conversation about time: measured at 5.5
	 * seconds a game it is an overnight job, where libretro fetches one index
	 * per system and then only what is missing.
	 *
	 * It brings the text with it. A cover fetched without the year and the
	 * synopsis would mean asking again later for what was in hand now. */
	if (one && stem && only && ss_signed_in()) {
		const system_cfg *sys = NULL;
		char dir[LIB_PATH * 2];
		int i;

		for (i = 0; i < a->sys.count; i++)
			if (!strcmp(a->sys.systems[i].folder, only)) { sys = &a->sys.systems[i]; break; }
		snprintf(dir, sizeof dir, "%s/%s", P_ROMS, only);
		if (sys && ss_run_begin(only, one_file, stem, dir, sys->exts)) {
			bool ss_working = true;

			while (ss_working && !want_quit && a->running) {
				int st = ss_run_step();

				plat_input_poll(&a->in);
				if (a->in.quit_requested) { ss_run_cancel(); a->running = false; return; }
				if (a->in.pressed[IN_POWER] || idle_due(a)) {
					ss_run_cancel();
					power_off(a);
					return;
				}
				if (menu_leaving(a)) {
					ss_run_cancel();
					plat_input_flush();
					memset(&a->in, 0, sizeof a->in);
					return;
				}
				if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
				if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
				if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
				if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

				if (st == 0) {
					/* Got it. Say so on the same panel the libretro run
					 * uses, so the two sources look like one feature. */
					menu_row row[2];

					art_head(head, sizeof head, one);
					row[0] = (menu_row){ "Box Art",
					                     had ? "replaced" : "found", false };
					row[1] = (menu_row){ "B to close", NULL, false };
					tick_tint(a);
					draw_shelf(a);
					SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
					SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
					SDL_RenderFillRect(a->r, NULL);
					menu_draw(a, head, row, 2, -1, menu_std_width(a), accent);
					plat_draw_osd(a->r);
					plat_present();
					SDL_Delay(900);
					ss_run_cancel();
					free_all_textures(a);
					plat_input_flush();
					memset(&a->in, 0, sizeof a->in);
					return;
				}
				if (st < 0) { ss_run_cancel(); ss_working = false; break; }

				art_head(head, sizeof head, one);
				rows[0] = (menu_row){ "Box Art", ss_run_where(), false };
				rows[1] = (menu_row){ "B to stop", NULL, false };
				tick_tint(a);
				draw_shelf(a);
				SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
				SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
				SDL_RenderFillRect(a->r, NULL);
				menu_draw(a, head, rows, 2, -1, menu_std_width(a), accent);
				plat_draw_osd(a->r);
				plat_present();
				SDL_Delay(8);
			}
			if (!a->running || want_quit) return;
		}
	}

	if (only) {
		/* A one-entry library rather than a filter inside artscrape.c. The
		 * scraper already takes the list it should work on; handing it a
		 * shorter list is the same operation, and it keeps "which systems"
		 * a question the caller answers. */
		static systems_cfg one;
		int i;

		one.count = 0;
		for (i = 0; i < a->sys.count; i++)
			if (!strcmp(a->sys.systems[i].folder, only)) {
				one.systems[0] = a->sys.systems[i];
				one.count = 1;
				break;
			}
		art_begin(&one, P_ROMS, stem);
	} else {
		art_begin(&a->sys, P_ROMS, NULL);
	}

	while (!done && !want_quit && a->running) {
		/* One step per frame. A step is one request STARTED or one poll of
		 * the request already running - never a wait for one, because this
		 * loop is where the power button is read. */
		if (working && art_step() == 0) working = false;
		art_status(&p);

		snprintf(counts, sizeof counts, "%d found, %d missing, %d already",
		         p.found, p.missing, p.skipped);
		snprintf(where, sizeof where, "%d of %d", p.systems_done, p.systems);
		if (one) {
			/* Asked about one game, answer about one game: the run is that
			 * game alone (art_begin's `only`), and "1 found, 19 already" would
			 * be a true statement about work the player did not ask for. */
			art_head(head, sizeof head, one);
			rows[0] = (menu_row){ "Box Art",
			                      working ? "fetching"
			                      : p.found ? (had ? "replaced" : "found")
			                      : had ? "not found, kept the old one"
			                            : "not found", false };
			rows[1] = (menu_row){ working ? "B to stop" : "B to close",
			                      NULL, false };
			nrows = 2;
		} else {
			/* Running: what it is doing. Finished: what went wrong, or
			 * that it is done - never the last game it happened to fetch,
			 * which is what this said before and reads as still working
			 * on it. */
			art_head(head, sizeof head,
			         working ? (p.now[0] ? p.now : "starting")
			         : p.problem[0] ? p.problem : "Done");
			nrows = art_rows(rows, where, counts, working);
		}

		plat_input_poll(&a->in);
		if (a->in.quit_requested) { art_cancel(); a->running = false; return; }

		/* A fetch in flight is not somebody in the room, so it does not hold
		 * the idle clock the way a connected browser does in Over The Hare -
		 * there, a person was at the other end. Here the device is talking to
		 * itself, and a library left scraping unattended on battery is
		 * exactly what Auto Off is for. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			art_cancel();
			power_off(a);
			return;
		}
		if (menu_leaving(a)) {
			art_cancel();
			done = true;
		}

		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		tick_tint(a);
		draw_shelf(a);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw(a, head, rows, nrows, -1, menu_std_width(a), accent);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(8);
	}

	art_cancel();
	/* The cards on the shelf are textures already uploaded from whatever art
	 * existed when the shelf was built. New art on the card changes nothing
	 * until they are dropped and read again. */
	free_all_textures(a);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* One game, and the two things you can do to it from here.
 *
 * X on the shelf, because Y is already favorite and the pair reads as a
 * unit - look at this one, or mark it. Nothing here is reachable during a
 * game: the in-game menu is about the session, this is about the file.
 *
 * A still, not a live view. Everything is read when the screen opens and
 * again after an action changes something, rather than every frame - these
 * are numbers that only move when the player moves them, and the alternative
 * is a stat storm at 120 Hz for a panel nobody is interacting with. */
/* The info screen's context. owner, g and v all move when a favorite is
 * toggled, so they live here where the key handler can put them back. */
typedef struct {
	app        *a;
	sysview    *v;
	game_entry *g;
	int         owner;
	game_info   gi;
	menu_style  st;   /* the accent follows the OWNING system, which can move */
} info_ctx;

static int info_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	info_ctx *c = ctx;
	int n;

	(void)max;
	*heading = c->g->title;
	/* menu_wifi, not wifi_status. This loop runs every frame and wifi_status
	 * forks wpa_cli out of a 119 MB process; this screen was doing it about
	 * 125 times a second for a row that says "needs Wi-Fi". The system menu
	 * was measured at 12 fps for the same mistake. */
	n = gi_rows(rows, &c->gi, menu_wifi(NULL, 0) == WIFI_CONNECTED);
	return n > max ? max : n;
}

/* Defined with the achievement card, which is where wrap_text lives and which
 * this is the same gesture as: a row that opens into something to read. */
static void synopsis_screen(app *a, const char *title, const char *text,
                            unsigned accent);

/* The achievement list, which the in-game menu opens over a paused frame and
 * this screen opens over the shelf. `over_shelf` says which, rather than the
 * absence of a frame saying it: in-game, a capture that failed also leaves no
 * frame, and drawing the shelf under a paused game would be a lie about where
 * the player is. */
static void cheevos_screen(app *a, SDL_Texture *bg, bool over_shelf);

/* ---- ChaOS: choosing a cover (Get / Replace Box Art) ----------------------
 *
 * For the games libretro's thumbnails don't have - homebrew above all - the
 * player chooses: libretro's own match as before, a cover from itch.io (where
 * most new homebrew for these consoles is published, src/itch.h), or one of
 * the game's own screenshots, its title screen most often. The candidates are
 * shown one at a time, as large as the panel allows, left and right to look
 * and A to use one; a downloaded one is fetched only when it is looked at,
 * and the next one meanwhile. */

#define PICK_MAX   24
#define PICK_TMP   "/tmp/chaos-pick-%d"

typedef struct {
	char label[160];   /* what the caption says: where it is from, and what */
	char url[256];     /* to download, or "" */
	char path[512];    /* on the card, or where the download goes */
	int  state;        /* 0 not yet, 1 coming, 2 here, -1 couldn't */
} pick_item;

/* The cover the player chose, as the game's box art: decoded and written as
 * a PNG beside the old one and renamed over it once whole, so nothing is
 * lost if it fails, then shrunk to the size covers are drawn at. */
static bool cover_save(app *a, const char *src, const char *folder, const char *stem)
{
	char dest[LIB_PATH * 3], tmp[LIB_PATH * 3 + 8], dir[LIB_PATH * 3];
	SDL_Surface *s = IMG_Load(src);
	char *slash;
	bool ok;

	if (!s) {
		fprintf(stderr, "cover: couldn't read %s: %s\n", src, IMG_GetError());
		return false;
	}
	box_art_path(folder, stem, dest, sizeof dest);
	snprintf(dir, sizeof dir, "%s", dest);
	if ((slash = strrchr(dir, '/'))) { *slash = '\0'; mkdir(dir, 0755); }
	snprintf(tmp, sizeof tmp, "%s.new", dest);
	ok = IMG_SavePNG(s, tmp) == 0 && rename(tmp, dest) == 0;
	SDL_FreeSurface(s);
	if (!ok) {
		unlink(tmp);
		fprintf(stderr, "cover: couldn't write %s\n", dest);
		return false;
	}
	art_shrink(dest);
	free_all_textures(a);
	fprintf(stderr, "cover: %s/%s from %s\n", folder, stem, src);
	return true;
}

static void pick_draw(app *a, const char *title, pick_item *it, int n, int sel,
                      SDL_Texture *tex, int tw, int th, const char *note, unsigned accent)
{
	TTF_Font *fh = ui_font(UI_F_LABEL), *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	SDL_Rect panel = { 96, 40, TORTOS_SCREEN_W - 192, TORTOS_SCREEN_H - 80 };
	SDL_Rect area = { panel.x + 48, panel.y + 104, panel.w - 96, panel.h - 250 };
	char line[192], fit[192];

	tick_tint(a);
	draw_shelf(a);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 140);
	SDL_RenderFillRect(a->r, NULL);
	ui_panel(a->r, &panel, 28, accent);
	ui_fit_text(fh, title, fit, sizeof fit, panel.w - 96);
	ui_text(a->r, fh, fit, panel.x + panel.w / 2, panel.y + 28, 0, UI_TEXT);

	if (tex && tw > 0 && th > 0) {
		/* As large as fits, whole, centred, and smoothly: a cover is a
		 * picture, not pixel art, and itch.io's are 315x250 */
		float k = (float)area.w / tw < (float)area.h / th ? (float)area.w / tw : (float)area.h / th;
		SDL_Rect d;

		SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
		d.w = (int)(tw * k);
		d.h = (int)(th * k);
		d.x = area.x + (area.w - d.w) / 2;
		d.y = area.y + (area.h - d.h) / 2;
		SDL_RenderCopy(a->r, tex, NULL, &d);
	} else {
		const char *say = n <= 0 ? note
		                : it[sel].state < 0 ? "Couldn't load this one" : "Loading...";
		/* ChaOS: every line of it, wrapped to the panel rather than cut:
		 * "\n" starts a line, and a line too long breaks between words
		 * (an install's "Getting Godot (FRT) 3.5, which it runs on" was
		 * losing its end). Up to six lines, centred as a block. */
		char lines[6][160];
		int first_para[6], para_no = 0;
		int nl = 0, lh = ui_font_line(UI_F_MENU), y, k;
		const char *p = say ? say : "";

		while (*p && nl < 6) {
			size_t para = strcspn(p, "\n");
			const char *e = p + para;

			while (p < e && nl < 6) {
				char try[160];
				size_t take = 0, best = 0;

				/* the most whole words that fit */
				while (take < (size_t)(e - p) && take < sizeof try - 1) {
					size_t w = take;

					while (w < (size_t)(e - p) && p[w] == ' ') w++;
					while (w < (size_t)(e - p) && p[w] != ' ') w++;
					if (w >= sizeof try) break;
					memcpy(try, p, w);
					try[w] = '\0';
					if (ui_text_width(fm, try) > area.w && best) break;
					best = take = w;
					if (ui_text_width(fm, try) > area.w) break;
				}
				if (!best) best = (size_t)(e - p) < sizeof try - 1 ? (size_t)(e - p) : sizeof try - 1;
				first_para[nl] = para_no == 0;
				snprintf(lines[nl++], sizeof lines[0], "%.*s", (int)best, p);
				p += best;
				while (p < e && *p == ' ') p++;
			}
			p = *e ? e + 1 : e;
			para_no++;
		}
		y = area.y + area.h / 2 - nl * lh / 2;
		for (k = 0; k < nl; k++) {
			ui_fit_text(fm, lines[k], fit, sizeof fit, area.w);
			ui_text(a->r, fm, fit, area.x + area.w / 2, y + k * lh, 0,
			        first_para[k] ? UI_TEXT_SOFT : UI_TEXT_DIM);
		}
	}
	if (n > 0) {
		ui_fit_text(fm, it[sel].label, fit, sizeof fit, panel.w - 96);
		ui_text(a->r, fm, fit, panel.x + panel.w / 2, area.y + area.h + 22, 0, UI_TEXT_SOFT);
		snprintf(line, sizeof line, "%s%d of %d%s   A: use   B: back",
		         sel > 0 ? "\xE2\x80\xB9 " : "", sel + 1, n, sel + 1 < n ? " \xE2\x80\xBA" : "");
	} else {
		snprintf(line, sizeof line, "B: back");
	}
	ui_text(a->r, fs, line, panel.x + panel.w / 2, panel.y + panel.h - 56, 0, UI_TEXT_DIM);
}

static void pick_frame(app *a, const char *title, pick_item *it, int n, int sel,
                       SDL_Texture *tex, int tw, int th, const char *note, unsigned accent)
{
	pick_draw(a, title, it, n, sel, tex, tw, th, note, accent);
	plat_draw_osd(a->r);
	plat_present();
}

/* The picker itself: the index chosen, or -1. Downloads one candidate at a
 * time in the one request slot: the one on screen first, then the next. */
static int pick_cover(app *a, const char *title, pick_item *it, int n, unsigned accent)
{
	SDL_Texture *tex = NULL;
	int sel = 0, shown = -1, fetching = -1, tw = 0, th = 0, chosen = -1;

	sync_abandon();
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	for (;;) {
		/* The download in flight */
		if (fetching >= 0) {
			int got = net_async_poll();

			if (got != 0) {
				it[fetching].state = got > 0 ? 2 : -1;
				if (got < 0) unlink(it[fetching].path);
				fetching = -1;
			}
		}
		/* The next to ask for: the one on screen, else the one after it */
		if (fetching < 0) {
			int want = -1;

			if (it[sel].state == 0) want = sel;
			else if (sel + 1 < n && it[sel + 1].state == 0) want = sel + 1;
			if (want >= 0) {
				if (!it[want].url[0]) it[want].state = 2;
				else if (net_get_async(it[want].url, it[want].path, 20)) {
					it[want].state = 1;
					fetching = want;
				} else {
					it[want].state = -1;
				}
			}
		}
		/* The picture on screen, once it is here */
		if (shown != sel && it[sel].state == 2) {
			SDL_Surface *s = IMG_Load(it[sel].path);

			if (tex) SDL_DestroyTexture(tex);
			tex = NULL;
			if (s) {
				tex = SDL_CreateTextureFromSurface(a->r, s);
				tw = s->w;
				th = s->h;
				SDL_FreeSurface(s);
			} else {
				it[sel].state = -1;
			}
			shown = sel;
		}

		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); break; }
		if (menu_leaving(a)) break;
		if (in_repeat(&a->in, IN_LEFT) && sel > 0) { sel--; shown = -1; }
		if (in_repeat(&a->in, IN_RIGHT) && sel + 1 < n) { sel++; shown = -1; }
		if (shown != sel && tex) { SDL_DestroyTexture(tex); tex = NULL; }
		if (a->in.pressed[IN_ACCEPT] && it[sel].state == 2 && tex) { chosen = sel; break; }
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		pick_frame(a, title, it, n, sel, shown == sel ? tex : NULL, tw, th, NULL, accent);
		SDL_Delay(8);
	}
	if (fetching >= 0) net_async_abort();
	if (tex) SDL_DestroyTexture(tex);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return chosen;
}

/* A line on the panel for a moment: why there is nothing to choose from */
static void pick_say(app *a, const char *title, const char *say, unsigned accent)
{
	unsigned t0 = plat_now_ms();

	while (plat_now_ms() - t0 < 1800 && a->running) {
		plat_input_poll(&a->in);
		if (a->in.pressed[IN_ACCEPT] || menu_leaving(a)) break;
		pick_frame(a, title, NULL, 0, 0, NULL, 0, 0, say, accent);
		SDL_Delay(16);
	}
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

static void itch_index_path(const char *folder, char *out, size_t n)
{
	char dir[LIB_PATH * 2];

	snprintf(dir, sizeof dir, "%s/itch", P_USERDATA);
	mkdir(dir, 0755);
	snprintf(out, n, "%s/%s.list", dir, folder);
}

/* A day between catch-ups: new homebrew is a handful a day across all of
 * itch.io, and one look a day keeps the list fresh for the price of a page. */
#define ITCH_FRESH_S (24 * 60 * 60)

static bool itch_cover_screen(app *a, const char *folder, const char *stem,
                              const char *title, unsigned accent)
{
	char path[LIB_PATH * 3], now[96];
	itch_game *games;
	int count = 0, best[PICK_MAX], k, i;
	time_t when = 0;
	bool whole = false, picked = false;

	itch_index_path(folder, path, sizeof path);
	games = itch_index_load(path, &count, &when, &whole);
	/* Read the list first when there is none, it isn't whole, or it is a
	 * day old; a list on the card that can't be brought up to date is
	 * still used */
	if (!games || !whole || time(NULL) - when > ITCH_FRESH_S) {
		if (!net_online()) {
			if (!games) { pick_say(a, title, "Needs Wi-Fi", accent); return false; }
		} else {
			bool going = true;

			free(games);
			games = NULL;
			sync_abandon();
			if (!itch_fetch_begin(folder, path)) going = false;
			while (going && a->running) {
				int st = itch_fetch_step(), g, pg;

				plat_input_poll(&a->in);
				if (a->in.quit_requested) { itch_fetch_cancel(); a->running = false; return false; }
				if (a->in.pressed[IN_POWER] || idle_due(a)) { itch_fetch_cancel(); power_off(a); return false; }
				if (menu_leaving(a)) { itch_fetch_cancel(); break; }
				if (st <= 0) {
					if (st < 0) fprintf(stderr, "itch: %s\n", itch_fetch_problem());
					going = false;
					break;
				}
				itch_fetch_progress(&g, &pg);
				snprintf(now, sizeof now, "Reading itch.io's list\n%s \xC2\xB7 %d games", folder, g);
				pick_frame(a, title, NULL, 0, 0, NULL, 0, 0, now, accent);
				SDL_Delay(8);
			}
			/* Whatever is on the card now, new or as it was */
			games = itch_index_load(path, &count, &when, &whole);
			if (!games) {
				const char *why = itch_fetch_problem();
				pick_say(a, title, why && why[0] ? why : "Couldn't read itch.io", accent);
				return false;
			}
		}
	}
	k = itch_match(stem, games, count, best, PICK_MAX);
	/* The title shown, when it isn't the file's name, is tried too */
	if (!k && title && strcmp(title, stem)) k = itch_match(title, games, count, best, PICK_MAX);
	if (!k) {
		free(games);
		pick_say(a, title, "No match on itch.io", accent);
		return false;
	}
	{
		pick_item it[PICK_MAX];

		memset(it, 0, sizeof it);
		for (i = 0; i < k; i++) {
			snprintf(it[i].label, sizeof it[i].label, "itch.io: %s", games[best[i]].title);
			snprintf(it[i].url, sizeof it[i].url, "%s", games[best[i]].image);
			snprintf(it[i].path, sizeof it[i].path, PICK_TMP, i);
		}
		free(games);
		i = pick_cover(a, title, it, k, accent);
		if (i >= 0) picked = cover_save(a, it[i].path, folder, stem);
		for (int j = 0; j < k; j++) unlink(it[j].path);
	}
	return picked;
}

static bool shot_cover_screen(app *a, const char *folder, const char *file,
                              const char *stem, const char *title, unsigned accent)
{
	static char names[PICK_MAX][SHOTS_NAME];
	char dir[LIB_PATH * 3], when[64];
	pick_item it[PICK_MAX];
	int n, i;

	shots_dir(P_CARD, folder, file, dir, sizeof dir);
	n = shots_list(dir, names, PICK_MAX);
	if (n <= 0) { pick_say(a, title, "No screenshots of this game", accent); return false; }
	memset(it, 0, sizeof it);
	for (i = 0; i < n; i++) {
		shots_label(names[i], when, sizeof when);
		snprintf(it[i].label, sizeof it[i].label, "Screenshot, %s", when);
		snprintf(it[i].path, sizeof it[i].path, "%s/%s", dir, names[i]);
	}
	i = pick_cover(a, title, it, n, accent);
	return i >= 0 && cover_save(a, it[i].path, folder, stem);
}

/* Get / Replace Box Art: where from */
typedef struct {
	info_ctx *ic;
	int       shots;
	bool      itch;
} boxart_ctx;

enum { BA_AUTO, BA_ITCH, BA_SHOT };

static int boxart_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	boxart_ctx *c = ctx;
	static char shots[24];
	bool net = menu_wifi(NULL, 0) == WIFI_CONNECTED;
	int n = 0;

	(void)max;
	*heading = "Box Art";
	rows[n++] = (menu_row){ "Find Automatically", net ? NULL : "needs Wi-Fi", true };
	if (c->itch)
		rows[n++] = (menu_row){ "Search itch.io", net ? NULL : "needs Wi-Fi", true };
	snprintf(shots, sizeof shots, "%d", c->shots);
	rows[n++] = (menu_row){ "Use a Screenshot", c->shots ? shots : "none yet", c->shots > 0 };
	rows[n++] = MENU_RULE;
	rows[n++] = MENU_NOTE("itch.io: homebrew; screenshots: MENU+X");
	return n;
}

static menu_result boxart_key(app *a, void *ctx, in_button key, int sel)
{
	boxart_ctx *c = ctx;
	info_ctx *ic = c->ic;
	const char *folder = a->sys.systems[ic->owner].folder;
	unsigned accent = a->sys.systems[ic->owner].accent;
	int id = sel == 0 ? BA_AUTO : (c->itch && sel == 1) ? BA_ITCH : BA_SHOT;
	bool done = false;

	if (key != IN_ACCEPT) return MENU_STAY;
	if (id == BA_AUTO) {
		/* The ordinary scrape, over this one game: nothing deleted first,
		 * the new cover renamed over the old only once it has arrived whole */
		art_screen(a, folder, ic->g->title, ic->g->name, ic->g->file, accent);
		return MENU_DONE;
	}
	if (id == BA_ITCH) {
		if (!net_online()) return MENU_STAY;
		done = itch_cover_screen(a, folder, ic->g->name, ic->g->title, accent);
	} else {
		done = shot_cover_screen(a, folder, ic->g->file, ic->g->name, ic->g->title, accent);
	}
	return done ? MENU_DONE : MENU_STAY;
}

static void boxart_screen(app *a, info_ctx *ic)
{
	boxart_ctx c = { .ic = ic };
	static char names[PICK_MAX][SHOTS_NAME];
	char dir[LIB_PATH * 3];
	const char *folder = a->sys.systems[ic->owner].folder;

	shots_dir(P_CARD, folder, ic->g->file, dir, sizeof dir);
	c.shots = shots_list(dir, names, PICK_MAX);
	if (c.shots < 0) c.shots = 0;
	c.itch = itch_slugs(folder) != NULL;
	menu_run(a, &(menu_style){ .accent = a->sys.systems[ic->owner].accent },
	         boxart_build, boxart_key, &c);
}

static menu_result info_key(app *a, void *ctx, in_button key, int sel)
{
	info_ctx *c = ctx;
	menu_row  rows[GI_MAX];
	const char *heading;
	int n = info_build(c, rows, GI_MAX, &heading);

	/* X closes it, the same button that opened it. */
	if (key == IN_X) return MENU_DONE;
	if (key != IN_ACCEPT || sel >= n || !rows[sel].live) return MENU_STAY;

	if (!strcmp(rows[sel].label, "Synopsis")) {
		game_meta m;

		/* Read again here rather than carried in game_info: it is up to 4 KB
		 * of prose for a row that is usually not opened, and the gatherer runs
		 * on every action this screen takes. */
		if (db_game_get(db_lib(), a->sys.systems[c->owner].folder, c->g->file, &m)
		    && m.synopsis[0])
			synopsis_screen(a, c->g->title, m.synopsis, c->st.accent);
		return MENU_STAY;
	}

	if (!strcmp(rows[sel].label, "RetroAchievements")) {
		/* The set the gatherer loaded to count this row is still the loaded
		 * one, so the list has what it needs without reloading it. Over the
		 * shelf rather than a paused frame: no game is running here. */
		cheevos_screen(a, NULL, true);
		return MENU_STAY;
	}

	{
		/* Replace, or get: the ordinary scrape, run over this one game and
		 * nothing else. Nothing is deleted first. It used to be - remove, then
		 * fill in - and a game libretro has no art for lost the cover it had,
		 * which for a translation, homebrew or a hand-added cover is one the
		 * scraper can never bring back. The new one is renamed over the old
		 * only once it has arrived whole. */
		/* ChaOS: where from - libretro as before, itch.io, or a screenshot */
		boxart_screen(a, c);
	}
	gi_gather(a, c->owner, c->g, &c->gi);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return MENU_STAY;
}

static void game_info_screen(app *a)
{
	info_ctx c = { 0 };

	c.a = a;
	c.v = &a->view[a->sys_cursor];
	if (c.v->list.count == 0) return;
	c.owner = shelf_owner(a, a->sys_cursor, c.v->cursor);
	c.g = &c.v->list.items[c.v->cursor];
	gi_gather(a, c.owner, c.g, &c.gi);
	c.st.accent = a->sys.systems[c.owner].accent;
	/* Without this the panel sized itself to its own content, and its heading
	 * is a GAME TITLE - so it drew 976px wide beside every other menu's 800.
	 * info_preview next door was given the shared width and this was not,
	 * which is the same one-of-two-paths miss as every other bug of the day. */
	c.st.fixed_w = menu_std_width(a);
	c.st.root = true;

	menu_run(a, &c.st, info_build, info_key, &c);

	/* The card may now hold art it did not before. */
	free_all_textures(a);
}

/* One frame of it, from the real gatherer where possible: the fixture is the
 * game actually under the cursor, so a shot is a picture of this library
 * rather than of numbers somebody typed. */
void info_preview(app *a, bool net)
{
	sysview   *v = &a->view[a->sys_cursor];
	menu_row   rows[GI_MAX];
	game_info  gi;
	int        owner, n, sel;

	if (v->list.count == 0) return;
	owner = shelf_owner(a, a->sys_cursor, v->cursor);
	gi_gather(a, owner, &v->list.items[v->cursor], &gi);
	n = gi_rows(rows, &gi, net);
	/* Where the screen actually opens, asked the way menu_run_body asks it
	 * rather than hardcoded. It was a literal 5, which was the Box Art action
	 * until the rows were reordered and then was Saves - so the shot showed a
	 * highlight on a dead row, which the real screen cannot do. */
	sel = rows[0].live ? 0 : menu_step_sel(rows, n, 0, +1);

	draw_shelf(a);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
	SDL_RenderFillRect(a->r, NULL);
	g_menu_root = true;
	menu_draw(a, v->list.items[v->cursor].title, rows, n, sel, menu_std_width(a),
	          a->sys.systems[owner].accent);
	g_menu_root = false;
}

/* Bluetooth: pair a headset, connect it, forget it.
 *
 * The three things this stack punishes you for getting wrong are all handled
 * in bt.c rather than here - the agent, judging by `info` instead of by the
 * return of `connect`, and where the bonds actually live. What is left here is
 * a list and four buttons.
 *
 * The list is refreshed on an INTERVAL, never per frame: every query is a fork
 * out of a 119 MB process, and this loop runs every frame. Same two seconds
 * the Wi-Fi row uses, for the same reason.
 */
#define BT_VISIBLE 7
#define BT_SCAN_S  20

/* After connecting `mac` from the Bluetooth screen: wait - five seconds at
 * most - until launch.sh has published it and the sound has reached it, so
 * the headset about to be bumped is not pulled out from under the song. Doing
 * that first sent every switch through the speaker: the old headset went,
 * Muse's write failed, it fell back, and only then was it moved - seen
 * 2026-09-26. Routing keeps running while it waits, which is what moves it. */
static void bt_await_route(const char *mac)
{
	char want[32];
	unsigned until = plat_now_ms() + 5000;
	int i;

	snprintf(want, sizeof want, "bt_%s", mac);
	for (i = 3; want[i]; i++) if (want[i] == ':') want[i] = '_';
	while ((int)(until - plat_now_ms()) > 0) {
		muse_poll();
		aout_apply(false);
		if (!strcasecmp(aout_bt_sink(), want) &&
		    (!musec_heard() || !strcasecmp(musec_sink_now(), want)))
			return;
		SDL_Delay(50);
	}
}

static void bt_screen(app *a)
{
	bt_ui u = { 0 };
	menu_row rows[BT_VISIBLE + 4];
	unsigned next_refresh = 0, scan_until = 0, note_until = 0;
	int sel = 0;                  /* ROW space: 0 is the toggle, then devices */
	bool done = false;

	while (!done && !want_quit && a->running) {
		unsigned now = plat_now_ms();
		int nrows, i;

		if (next_refresh == 0 || now >= next_refresh) {
			u.state = bt_status();
			u.n = u.state == BT_READY ? bt_visible(u.dev, BT_MAX) : 0;
			for (i = 0; i < u.n; i++)
				bt_label(&u.dev[i], u.vals[i], sizeof u.vals[0]);
			next_refresh = now + 2000;
		}
		if (scan_until && now >= scan_until) scan_until = 0;
		/* A message says what just happened; it is not the state of anything,
		 * so it goes away on its own. "Forgotten" used to sit there until the
		 * screen was left and re-entered, which made it read as a condition
		 * rather than an event. */
		if (note_until && now >= note_until) { note_until = 0; u.note[0] = '\0'; }
		u.scanning = scan_until != 0;

		if (sel > u.n) sel = u.n;
		if (sel < 0) sel = 0;
		u.cursor = sel == 0 ? -1 : u.top + sel - 1;
		if (u.cursor >= u.n) u.cursor = u.n - 1;
		nrows = bt_menu_build(&u, rows, (int)(sizeof rows / sizeof rows[0]),
		                      BT_VISIBLE);

		plat_input_poll(&a->in);
		muse_poll();                        /* see menu_run_body */
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (menu_leaving(a)) done = true;
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);

		if (in_repeat(&a->in, IN_DOWN) && sel < u.n) sel++;
		if (in_repeat(&a->in, IN_UP)   && sel > 0)   sel--;
		/* The window follows the cursor, so a long list scrolls a row at a
		 * time from either end. */
		if (u.cursor >= 0) {
			if (u.cursor < u.top) u.top = u.cursor;
			if (u.cursor >= u.top + BT_VISIBLE) u.top = u.cursor - BT_VISIBLE + 1;
		}

		if (a->in.pressed[IN_Y] && u.state == BT_READY) {
			/* TWENTY seconds, not ten. Classic inquiry runs in cycles of
			 * about 10.24 s and a device answers probabilistically within
			 * one, so a single cycle misses devices that are plainly there:
			 * measured 2026-09-06 with an OpenRun Pro in pairing mode, ten
			 * seconds found nothing twice and twenty found it first try.
			 *
			 * It costs nothing to wait. The list refreshes every two seconds
			 * while the scan runs, so results appear as they arrive rather
			 * than at the end - a longer window only means it keeps
			 * filling. */
			bt_scan(BT_SCAN_S);
			scan_until = now + BT_SCAN_S * 1000;
			next_refresh = now + 1500;
			u.note[0] = '\0';
			note_until = 0;
		}

		if (a->in.pressed[IN_ACCEPT]) {
			char err[64];

			if (u.cursor < 0) {
				bool want = u.state != BT_READY;

				/* The preference either way: launch.sh reads it at boot and
				 * starts the stack or not. */
				db_set_int(db_dev(), "bluetooth", want ? 1 : 0);
				db_write_boot_env();

				wait_panel(a, "Bluetooth",
				           want ? "Turning on..." : "Turning off...");
				/* On: the stack first when there is no adapter (Bluetooth
				 * was off at boot), so the switch works now rather than at
				 * the next boot. Never bt_power without one: bluetoothctl
				 * waits for a controller that is not coming. Off: the
				 * adapter, then the stack and the radio's power. */
				if (want && u.state == BT_NO_ADAPTER && !bt_up()) {
					snprintf(u.note, sizeof u.note, "Bluetooth did not come up");
				} else {
					/* The stack just started: the bonds are readable now
					 * (on the Brick they're on plastron's state partition,
					 * which bt_up mounts), so a PCM for each, as after a
					 * pair. The one written at boot had none to read */
					if (want && u.state == BT_NO_ADAPTER)
						bt_asoundrc(P_ROOT, P_USERDATA);
					bt_power(want);
					if (!want) bt_down();
					snprintf(u.note, sizeof u.note, "%s", want ? "On" : "Off");
				}
				note_until = now + 4000;
				next_refresh = 0;
			} else if (u.cursor < u.n) {
				bt_device *d = &u.dev[u.cursor];

				if (d->connected) {
					wait_panel(a, "Bluetooth", "Disconnecting...");
					if (bt_disconnect(d->mac)) d->connected = false;
					snprintf(u.note, sizeof u.note, "Disconnected"); note_until = now + 4000;
				} else {
					bool ok = true;

					if (!d->bonded) {
						wait_panel(a, "Bluetooth", "Pairing...");
						ok = bt_pair(d->mac, err, sizeof err);
						if (!ok) {
							snprintf(u.note, sizeof u.note, "%s", err);
							note_until = now + 4000;
						} else {
							/* A PCM for the new bond. The emulator and
							 * Muse already running see it at their next
							 * open - see bt.h. */
							bt_asoundrc(P_ROOT, P_USERDATA);
						}
					}
					if (ok) {
						wait_panel(a, "Bluetooth", "Connecting...");
						if (bt_connect(d->mac, err, sizeof err)) {
							/* Connecting one here is choosing it, so any other
							 * goes back to paired: "connected" on this screen then
							 * means "where the sound is", and switching back is one
							 * press rather than a disconnect and a connect. Eric's
							 * call, 2026-09-26. Only from here: a headset that
							 * connects by itself does not bump another, or two that
							 * both do could knock each other off forever. Neither
							 * headset reconnected by itself within a minute of the
							 * Brick disconnecting it, so paired stays paired. */
							int bumped;

							bt_await_route(d->mac);
							bumped = bt_disconnect_others(d->mac);

							d->connected = true;
							snprintf(u.note, sizeof u.note, "%s",
							         bumped ? "Connected. The other headset is paired now"
							                : "Connected");
							note_until = now + 4000;
						} else {
							snprintf(u.note, sizeof u.note, "%s", err); note_until = now + 4000;
						}
					}
				}
				next_refresh = 0;
			}
		}

		if (a->in.pressed[IN_X] && u.cursor >= 0 && u.cursor < u.n &&
		    u.dev[u.cursor].bonded) {
			wait_panel(a, "Bluetooth", "Forgetting...");
			if (bt_forget(u.dev[u.cursor].mac)) {
				bt_asoundrc(P_ROOT, P_USERDATA);
				snprintf(u.note, sizeof u.note, "Forgotten"); note_until = now + 4000;
			} else {
				snprintf(u.note, sizeof u.note, "It would not unpair"); note_until = now + 4000;
			}
			next_refresh = 0;
		}

		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		tick_tint(a);
		draw_shelf(a);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw(a, "Bluetooth", rows, nrows, sel, menu_std_width(a), MENU_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(8);
	}

	/* On the way out, not while the list is up: `bluetoothctl devices` reads
	 * the same cache, so sweeping mid-screen would empty the list the player
	 * is looking at. */
	bt_sweep_cache(NULL);
}

/* Play time, most played first.
 *
 * Summarized once on entry rather than every frame: it is a fold over every
 * session ever recorded, which measured 6.3 ms cold on the card - nothing at
 * all once, and a waste sixty times a second. Nothing here writes.
 */
/* The most the list can ever be. The number actually shown is asked of
 * menu_list_fit at entry, because the answer moves with Text Size. */
#define STATS_VISIBLE 8

/* Returns true if it sent the player to a shelf, so the menu above it closes
 * rather than redrawing over a screen that has moved on. */
static bool stats_screen(app *a)
{
	char vals[STATS_VISIBLE + 1][32], labels[STATS_VISIBLE + 1][80];
	/* +3: the total row above the list, then a rule and two notes below it. */
	menu_row rows[STATS_VISIBLE + 4];
	/* The total row says more than a duration, so it gets its own buffer
	 * rather than borrowing a game's - which the device compiler caught as a
	 * truncation the host compiler let through. */
	char total[32], total_val[64], detail[192];
	stats_window win = STATS_ALL;
	bool by_sys = false;
	long now = (long)time(NULL);
	int ngames = stats_summarize(win, by_sys, now);
	int cursor = 0, top = 0;
	/* The total above the list, then a rule and two notes below it. What is
	 * left over is the list, and it is asked rather than assumed. */
	int vis = menu_list_fit(1, 1, 2);

	/* The buffers are fixed, so however much room the panel has, the list
	 * stops where they do. */
	if (vis > STATS_VISIBLE) vis = STATS_VISIBLE;
	bool done = false;

	stats_format(stats_total_seconds(), total, sizeof total);

	while (!done && !want_quit && a->running) {
		int shown = 0, i;

		/* The total is a row rather than a heading, so it lines up with the
		 * games under it and reads as one table. */
		/* The window's name IS the total's label. A row already sat here
		 * saying "Total", and a heading that also said which slice would
		 * have said the same thing twice in two type sizes. */
		snprintf(labels[0], sizeof labels[0], "%s", stats_window_name(win));
		/* "over" spelled out put "This Month  2h 16m over 143 launches" at
		 * 764px against 704 of content, and menu_draw answered by cutting
		 * the LABEL - so the row said "This Y...". The separator is the
		 * cheapest 60px on the screen. */
		snprintf(total_val, sizeof total_val, "%s · %d launch%s",
		         total, stats_total_launches(),
		         stats_total_launches() == 1 ? "" : "es");
		rows[shown++] = (menu_row){ labels[0], total_val, false };

		for (i = top; i < ngames && shown <= vis; i++) {
			const char *tag, *file;
			long secs;
			int launches;
			const char *dot;
			int len;

			if (!stats_at(i, &tag, &file, &secs, &launches)) break;
			if (by_sys) {
				/* stats.c knows the tag and nothing else - the display name
				 * lives in systems.cfg, which is this side of the seam. A
				 * system no longer on the card keeps its tag rather than
				 * disappearing: the time was still spent. */
				int k, found = 0;

				for (k = 0; k < a->sys.count; k++)
					if (!strcmp(a->sys.systems[k].tag, tag)) {
						snprintf(labels[shown], sizeof labels[0], "%s",
						         a->sys.systems[k].name);
						found = 1;
						break;
					}
				if (!found)
					snprintf(labels[shown], sizeof labels[0], "%s", tag);
			} else {
			/* The extension is noise in a list of names. Last dot only, so a
			 * title with dots of its own keeps them. */
			dot = strrchr(file, '.');
			len = dot && dot != file ? (int)(dot - file) : (int)strlen(file);
			{
				/* Extension off, then the same title rule the shelf uses, so
				 * "Contra (USA).zip" reads as "Contra" here too. It was
				 * printing the cataloging: a column of names where every one
				 * ends in a region and a revision is a column you cannot
				 * scan. */
				char bare[LIB_NAME];

				snprintf(bare, sizeof bare, "%.*s", len, file);
				lib_title(bare, labels[shown], sizeof labels[0]);
			}
			}
			stats_format(secs, vals[shown], sizeof vals[0]);
			/* live, not selected. The selection is menu_draw's `sel`
			 * argument; passing it here instead drew every other row
			 * quiet and highlighted none of them. */
			rows[shown] = (menu_row){ labels[shown], vals[shown], true };
			shown++;
		}

		if (ngames == 0) {
			snprintf(labels[1], sizeof labels[1], "Nothing played yet");
			rows[1] = (menu_row){ labels[1], NULL, false };
			shown = 2;
			detail[0] = '\0';
		} else {
			/* What the selected row knows beyond its total. This is where
			 * the extra columns went: three numbers beside a game name in
			 * 704px reads as a spreadsheet nobody can scan, and only one row
			 * is being looked at anyway. */
			long longest = 0, last = 0, secs = 0;
			int lost = 0, n = 0;
			char lg[16], ag[16], tail[24];

			stats_at(cursor, NULL, NULL, &secs, &n);
			stats_extra(cursor, &longest, &last, &lost);
			stats_format(longest, lg, sizeof lg);
			stats_ago(last, now, ag, sizeof ag);
			/* Only when it happened. A "0 lost" on every row would train the
			 * eye to skip the one place the number matters. */
			if (lost) snprintf(tail, sizeof tail, "  ·  %d lost", lost);
			else      tail[0] = '\0';
			/* Written in full and left long. Four facts do not fit 704px and
			 * are not meant to: the note scrolls. */
			snprintf(detail, sizeof detail,
			         "%d launch%s  ·  longest %s  ·  %s%s",
			         n, n == 1 ? "" : "es", lg, ag, tail);
		}

		if (detail[0]) {
			rows[shown++] = MENU_RULE;
			rows[shown++] = MENU_NOTE(detail);
		}
		rows[shown++] = MENU_NOTE(by_sys ? "A: go    Y: by game    L/R: window"
		                                 : "A: go    Y: by system    L/R: window");

		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; return false; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return false; }
		if (menu_leaving(a)) done = true;
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);

		/* A goes to the row under the cursor: the game on its own shelf, or in
		 * by-system mode that system's. Not a launch.
		 *
		 * This screen is a still of what has been played, and it knows a tag
		 * and a file. That is not enough to start a game - the core, the
		 * folder, the display mode and the save paths all hang off the shelf.
		 * It could resolve every one of them, and then there would be a second
		 * launch path to keep in step with the first. Landing on the shelf
		 * costs one button and arrives where launching already works, with the
		 * art, the autosave dot and the game's own menu.
		 *
		 * A row can outlive its ROM - these are keyed on tag and file and
		 * survive the card changing - so a miss does nothing rather than
		 * guessing, which is the answer the boot-resume path gives too. */
		if (a->in.pressed[IN_ACCEPT] && ngames > 0) {
			const char *tag = NULL, *file = NULL;
			int si, gi;

			if (stats_at(cursor, &tag, &file, NULL, NULL))
				for (si = 0; si < a->sys.count; si++) {
					if (strcmp(a->sys.systems[si].tag, tag) != 0) continue;
					if (a->view[si].list.count == 0) break;
					a->sys_cursor = si;
					/* By system there is no file to look for, so the shelf
					 * keeps whichever game it was already on. */
					if (!by_sys)
						for (gi = 0; gi < a->view[si].list.count; gi++)
							if (!strcmp(a->view[si].list.items[gi].file, file)) {
								a->view[si].cursor = gi;
								break;
							}
					/* CUBIC IS NOT ENTERED, IT IS TURNED. There is no
					 * going in and no going back - the cube IS the shelf,
					 * and update_both reads both cursors every frame.
					 *
					 * But it only syncs its coverflows on a press: the
					 * system one when sdir is non-zero, the game one when
					 * the view was never primed. Moving a cursor from out
					 * here leaves the cube still drawing where it was, with
					 * no press coming to reconcile it. So say it directly,
					 * with the same snap-without-animation enter_system
					 * uses on the other two directions.
					 *
					 * This is what came out on 2026-09-10: the jump was
					 * written for a mode that enters, shipped to one that
					 * does not, and the lesson recorded was that three UI
					 * directions are three code paths. */
					if (CARD_DIRS[g_dir].both) {
						cf_reset(&a->cf_sys, a->sys_cursor);
						cf_reset(&a->view[si].cf, a->view[si].cursor);
					} else {
						enter_system(a);
					}
					return true;
				}
		}

		{	/* A view change re-folds the same rows under a new question, so
			 * the cursor cannot survive it - row 3 of "This Week by system"
			 * is not row 3 of anything else. */
			bool changed = false;

			if (in_repeat(&a->in, IN_RIGHT)) {
				win = (win + 1) % STATS_WINDOWS; changed = true;
			}
			if (in_repeat(&a->in, IN_LEFT)) {
				win = (win + STATS_WINDOWS - 1) % STATS_WINDOWS;
				changed = true;
			}
			if (a->in.pressed[IN_Y]) { by_sys = !by_sys; changed = true; }
			if (changed) {
				now = (long)time(NULL);
				ngames = stats_summarize(win, by_sys, now);
				stats_format(stats_total_seconds(), total, sizeof total);
				cursor = top = 0;
			}
		}

		if (ngames > 0) {
			if (in_repeat(&a->in, IN_DOWN) && cursor < ngames - 1) cursor++;
			if (in_repeat(&a->in, IN_UP)   && cursor > 0)          cursor--;
			/* The window follows the cursor rather than the other way round,
			 * so a long list scrolls one row at a time from either end. */
			if (cursor < top) top = cursor;
			if (cursor >= top + vis) top = cursor - vis + 1;
		}

		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		tick_tint(a);
		draw_shelf(a);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		/* +1 because the total is row 0 and the games start under it. */
		menu_draw(a, "Play Time", rows, shown,
		          ngames ? cursor - top + 1 : -1, menu_std_width(a),
		          MENU_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(8);
	}
	return false;
}

/* ---- ChaOS: Button Mapping and Rewind ------------------------------------
 *
 * Kept in db_dev: remap.<TAG> for a console, remap.<TAG>:<file> for one game
 * over it, each a whole SETMAP line (src/remap.h); rewind.<TAG> = 1 for a
 * console that keeps states to rewind to. A game with no map of its own
 * plays its console's, and a console with none its turbo default. */

static void remap_key(char *k, size_t n, const char *tag, const char *file)
{
	if (file && *file) snprintf(k, n, "remap.%s:%s", tag, file);
	else snprintf(k, n, "remap.%s", tag);
}

/* What a game plays with: its own map, its console's, or the turbo default */
static const char *remap_spec(const char *tag, const char *file, char *buf, size_t n)
{
	char k[CFG_STR + LIB_PATH + 16];
	const char *t;

	remap_key(k, sizeof k, tag, file);
	if (file && *file && db_get_str(db_dev(), k, buf, n, NULL) && buf[0]) return buf;
	remap_key(k, sizeof k, tag, NULL);
	if (db_get_str(db_dev(), k, buf, n, NULL) && buf[0]) return buf;
	t = plat_turbo_map(tag);
	snprintf(buf, n, "%s", t ? t : "");
	return buf;
}

static bool rewind_load(const char *tag)
{
	char k[CFG_STR + 16];
	snprintf(k, sizeof k, "rewind.%s", tag);
	return db_get_int(db_dev(), k, 0) == 1;
}

static void rewind_save(const char *tag, bool on)
{
	char k[CFG_STR + 16];
	snprintf(k, sizeof k, "rewind.%s", tag);
	db_set_int(db_dev(), k, on ? 1 : 0);
}

enum { RM_ROW_SCOPE = 100, RM_ROW_RESET, RM_ROW_NONE };

static void gm_backdrop(app *a, void *ctx);   /* with the in-game menu */

typedef struct {
	app        *a;
	int         sys;          /* the console */
	const char *file;         /* the game, in a game; NULL from the shelf */
	void       *game;         /* the in-game menu's gm_ctx, for its backdrop */
	bool        own;          /* the game has a map of its own */
	rm_map      map;
	int         ids[24];
	char        vals[RM_BUTTONS][24];
	char        scope[48], heading[64];
} rm_ctx;

static void rm_load(rm_ctx *c)
{
	const char *tag = c->a->sys.systems[c->sys].tag;
	char spec[512], k[CFG_STR + LIB_PATH + 16];

	remap_key(k, sizeof k, tag, c->file);
	c->own = c->file && db_has(db_dev(), k);
	rm_from_spec(tag, remap_spec(tag, c->own ? c->file : NULL, spec, sizeof spec), &c->map);
}

/* Saved where it belongs, and in a game sent at once */
static void rm_store(rm_ctx *c)
{
	const char *tag = c->a->sys.systems[c->sys].tag;
	char spec[512], k[CFG_STR + LIB_PATH + 16];

	if (!rm_to_spec(tag, &c->map, spec, sizeof spec)) return;
	remap_key(k, sizeof k, tag, c->own ? c->file : NULL);
	db_set_str(db_dev(), k, spec);
	if (c->file) plat_resident_line("SETMAP\tmap=%s", spec);
}

static int rm_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	rm_ctx *c = ctx;
	const system_cfg *sc = &c->a->sys.systems[c->sys];
	int n = 0, i;

	(void)max;
	snprintf(c->heading, sizeof c->heading, "%s Button Mapping", ctl_console_short(sc->tag, sc->name));
	*heading = c->heading;
	if (c->file) {
		snprintf(c->scope, sizeof c->scope, "%s", c->own ? "This game" : "All its games");
		c->ids[n] = RM_ROW_SCOPE;
		rows[n++] = (menu_row){ "For", c->scope, true, .cycles = true };
		c->ids[n] = RM_ROW_NONE; rows[n++] = MENU_RULE;
	}
	for (i = 0; i < RM_BUTTONS; i++) {
		rm_choice_label(sc->tag, c->map.choice[i], c->vals[i], sizeof c->vals[i]);
		c->ids[n] = i;
		rows[n++] = (menu_row){ rm_button_label(i), c->vals[i], true, .cycles = true };
	}
	c->ids[n] = RM_ROW_NONE; rows[n++] = MENU_RULE;
	c->ids[n] = RM_ROW_RESET;
	rows[n++] = (menu_row){ "Reset to Default", NULL, true };
	return n;
}

static menu_result rm_key(app *a, void *ctx, in_button key, int sel)
{
	rm_ctx *c = ctx;
	const char *tag = a->sys.systems[c->sys].tag;
	int id = sel >= 0 && sel < 24 ? c->ids[sel] : RM_ROW_NONE;
	int d = key == IN_LEFT ? -1 : key == IN_RIGHT || key == IN_ACCEPT ? 1 : 0;
	char k[CFG_STR + LIB_PATH + 16];

	if (!d) return MENU_STAY;
	if (id >= 0 && id < RM_BUTTONS) {
		rm_choice ch[RM_CHOICES];
		int n = rm_choices(tag, ch);
		c->map.choice[id] = (c->map.choice[id] + d + n) % n;
		rm_store(c);
	} else if (id == RM_ROW_SCOPE) {
		/* This game takes the console's map as its start; all its games
		 * drops the game's own */
		c->own = !c->own;
		if (c->own) rm_store(c);
		else {
			remap_key(k, sizeof k, tag, c->file);
			db_del(db_dev(), k);
			rm_load(c);
			rm_store(c);
		}
	} else if (id == RM_ROW_RESET && key == IN_ACCEPT) {
		remap_key(k, sizeof k, tag, c->own ? c->file : NULL);
		db_del(db_dev(), k);
		{
			char spec[512];
			const char *t = remap_spec(tag, NULL, spec, sizeof spec);
			if (c->own) t = plat_turbo_map(tag);
			rm_from_spec(tag, t, &c->map);
			if (c->file) {
				rm_to_spec(tag, &c->map, spec, sizeof spec);
				plat_resident_line("SETMAP\tmap=%s", spec);
			}
		}
		if (c->own) rm_store(c);
	}
	return MENU_STAY;
}

static void rm_backdrop(app *a, void *ctx)
{
	gm_backdrop(a, ((rm_ctx *)ctx)->game);
}

/* From the console's menu (no game: file NULL) or the in-game menu (its
 * gm_ctx and style, the game's file) */
static void remap_screen(app *a, int sys, const char *file, void *game,
                         const menu_style *over)
{
	rm_ctx c;
	menu_style st = { .fixed_w = menu_shelf_width(a), .follow_tint = true };

	memset(&c, 0, sizeof c);
	c.a = a;
	c.sys = sys;
	c.file = file;
	c.game = game;
	if (game && over) {
		st = *over;                 /* its width, tint, power rule and peek */
		st.backdrop = rm_backdrop;
	}
	st.root = false;
	rm_load(&c);
	menu_run(a, &st, rm_build, rm_key, &c);
}

/* ---- ChaOS: a console's Controls --------------------------------------------
 *
 * "GB Controls": what each of the Brick's buttons does on this console now,
 * the player's Button Mapping included, then the shortcuts MENU and POWER
 * keep, paged with left and right (controls.h). Its one live row opens Button
 * Mapping, as a submenu. From the in-game menu it is the game's (its own
 * mapping where it has one) over the paused game; from the console's menu,
 * the console's. */
typedef struct {
	app        *a;
	int         sys;
	const char *file;          /* the game, in a game */
	void       *game;          /* the in-game menu's gm_ctx */
	const menu_style *over;
	int         page;
	char        head[64];
	char        vals[RM_BUTTONS][24];
	ctl_console cc;
} cc_ctx;

static void cc_fill(cc_ctx *c)
{
	const char *tag = c->a->sys.systems[c->sys].tag;
	char spec[512];
	rm_map m;
	int i;

	rm_from_spec(tag, remap_spec(tag, c->file, spec, sizeof spec), &m);
	for (i = 0; i < RM_BUTTONS; i++) {
		c->cc.button[i] = rm_button_label(i);
		c->cc.value[i] = rm_choice_label(tag, m.choice[i], c->vals[i], sizeof c->vals[i]);
	}
	c->cc.rewind = rewind_load(tag);
}

static int cc_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	cc_ctx *c = ctx;
	const system_cfg *sc = &c->a->sys.systems[c->sys];

	(void)max;
	cc_fill(c);
	snprintf(c->head, sizeof c->head, "%s Controls", ctl_console_short(sc->tag, sc->name));
	*heading = c->head;
	return ctl_console_rows(&c->cc, c->page, rows);
}

static menu_result cc_key(app *a, void *ctx, in_button key, int sel)
{
	cc_ctx *c = ctx;
	int pages = ctl_console_pages(&c->cc);

	(void)sel;
	if (key == IN_RIGHT && c->page + 1 < pages) c->page++;
	if (key == IN_LEFT && c->page > 0) c->page--;
	if (key == IN_ACCEPT) remap_screen(a, c->sys, c->file, c->game, c->over);
	return MENU_STAY;
}

static void cc_backdrop(app *a, void *ctx)
{
	gm_backdrop(a, ((cc_ctx *)ctx)->game);
}

static void console_controls_screen(app *a, int sys, const char *file, void *game,
                                    const menu_style *over)
{
	cc_ctx c;
	menu_style st = { .fixed_w = menu_shelf_width(a), .follow_tint = true };

	memset(&c, 0, sizeof c);
	c.a = a;
	c.sys = sys;
	c.file = file;
	c.game = game;
	c.over = over;
	if (game && over) {
		st = *over;
		st.backdrop = cc_backdrop;
	}
	st.root = false;
	menu_run(a, &st, cc_build, cc_key, &c);
}

/* ---- ChaOS: Updates ---------------------------------------------------------
 *
 * What src/update.h does, on a screen: what is installed and what is latest,
 * and the one thing to do next - check, download, cancel, install, or remove
 * a download nobody wants yet. Nothing here starts on its own: the boot's
 * check only says there is something (upd_announce, on the shelf), and a
 * finished download waits on the card until the player comes back for it. */

enum { UP_NONE, UP_CHECK, UP_DOWNLOAD, UP_FULL, UP_CANCEL, UP_INSTALL, UP_REMOVE, UP_NOTES,
       UP_SYSTEM };

/* ChaOS: the whole card image, through the updater in the root, for a
 * release on another TortOS base (below, with TortOS's System Update) */
static void image_update_screen(app *a);

typedef struct {
	app  *a;
	int   ids[16];
	char  latest[48], size[24], full[24], pct[24], batt[32], dots[8], head[64];
	int   last;               /* what Try Again repeats: UP_CHECK, UP_DOWNLOAD or UP_FULL */
	bool  online;
	wifi_state wifi;
	Uint32 next_look;
} upd_ctx;

/* Enough charge to be sure of the boot that writes the new system: a
 * quarter, or the cable */
#define UPD_MIN_BATTERY 25

static bool upd_battery_ok(char *why, size_t n)
{
	int pct = 100;
	bool charging = false;

	if (!plat_battery(&pct, &charging) || charging || pct >= UPD_MIN_BATTERY) return true;
	snprintf(why, n, "charge to %d%%", UPD_MIN_BATTERY);
	return false;
}

/* ChaOS: the steps an update goes through, each saying where it is - done,
 * how far, working (dots that move, where there is no percentage to give),
 * or not yet - so a slow one never looks stuck */
enum { US_DOWNLOAD, US_CHECK, US_INSTALL, US_STEPS };

static int upd_steps(upd_ctx *c, upd_state s, menu_row *rows, int n)
{
	static const char *const LABEL[US_STEPS] = { "Download", "Check and Unpack", "Install" };
	int at, i;

	/* where it has got to */
	at = s == UPD_DOWNLOADING ? US_DOWNLOAD : s == UPD_UNPACKING ? US_CHECK : US_INSTALL;
	/* dots that move where there is no percentage: "working" from 1 to 3,
	 * the gap kept by spaces the width of the dots they stand for */
	{
		int k = (int)((plat_now_ms() / 400) % 3) + 1;
		snprintf(c->dots, sizeof c->dots, "%.*s", k, "...");
	}
	if (s == UPD_DOWNLOADING && !upd_receiving())
		snprintf(c->pct, sizeof c->pct, "connecting%s", c->dots);
	else
		snprintf(c->pct, sizeof c->pct, "%d%%", upd_percent());
	for (i = 0; i < US_STEPS; i++) {
		const char *v;
		if (i < at)                    v = "done";
		else if (i > at)               v = NULL;
		else if (i == US_DOWNLOAD)     v = c->pct;
		else if (i == US_INSTALL)      v = "next";
		else                           v = c->dots;
		c->ids[n] = UP_NONE;
		rows[n++] = (menu_row){ LABEL[i], v, false };
	}
	return n;
}

static int upd_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	upd_ctx *c = ctx;
	upd_state s;
	const ota_info *o = upd_latest();
	Uint32 now = plat_now_ms();
	int n = 0;

	(void)max;
	if (now >= c->next_look) {
		char ssid[WIFI_SSID_MAX], ip[64];
		c->online = net_online();
		c->wifi = wifi_status(ssid, sizeof ssid, ip, sizeof ip);
		c->next_look = now + 2000;
	}
	upd_tick(c->online);
	s = upd_get();
	*heading = "Updates";

	switch (s) {
	case UPD_IDLE:     snprintf(c->latest, sizeof c->latest, "not checked"); break;
	case UPD_CHECKING:
		snprintf(c->latest, sizeof c->latest, "checking%.*s",
		         (int)((plat_now_ms() / 400) % 4), "...");
		break;
	case UPD_CURRENT:  snprintf(c->latest, sizeof c->latest, "%s, up to date", o->version); break;
	case UPD_FAILED:   snprintf(c->latest, sizeof c->latest, "%s", o->version[0] ? o->version : "unknown"); break;
	default:           snprintf(c->latest, sizeof c->latest, "%s", o->version); break;
	}
	/* While one is on its way, the screen is its steps, under a heading
	 * that says which version: room for them all without scrolling */
	if (s == UPD_DOWNLOADING || s == UPD_UNPACKING || s == UPD_READY) {
		snprintf(c->head, sizeof c->head, "%s %s", o->small ? "Update to" : "Full Install of",
		         o->version);
		*heading = c->head;
		n = upd_steps(c, s, rows, n);
		c->ids[n] = UP_NONE; rows[n++] = MENU_RULE;
		if (s == UPD_DOWNLOADING) {
			c->ids[n] = UP_CANCEL; rows[n++] = (menu_row){ "Cancel", NULL, true };
		} else if (s == UPD_UNPACKING) {
			c->ids[n] = UP_NONE; rows[n++] = MENU_NOTE("Keep the Brick on");
		} else {
			bool ok = upd_battery_ok(c->batt, sizeof c->batt);
			c->ids[n] = UP_INSTALL;
			rows[n++] = (menu_row){ "Install and Restart", ok ? NULL : c->batt, ok };
			c->ids[n] = UP_REMOVE;
			rows[n++] = (menu_row){ "Remove Download", NULL, true };
			/* An install that could not be armed says why, and stays ready */
			if (upd_error()[0]) { c->ids[n] = UP_NONE; rows[n++] = MENU_NOTE(upd_error()); }
		}
		return n;
	}
	c->ids[n] = UP_NONE; rows[n++] = (menu_row){ "Installed", CHAOS_VERSION, false };
	c->ids[n] = UP_NONE; rows[n++] = (menu_row){ "Latest", c->latest, false };
	/* What the newer one brings, once its notes are in */
	if (s != UPD_CURRENT && s != UPD_CHECKING && upd_notes()) {
		c->ids[n] = UP_NOTES;
		rows[n] = (menu_row){ "What's New", NULL, true };
		rows[n++].opens = true;
	}
	c->ids[n] = UP_NONE; rows[n++] = MENU_RULE;

	switch (s) {
	case UPD_AVAILABLE:
		/* Two ways in: Update, ChaOS's own files that changed, a few MB,
		 * when this card's system is the release's; Full Install, the whole
		 * system and every one of ChaOS's files fresh, tens of MB. Games,
		 * saves and settings stay with either. */
		snprintf(c->size, sizeof c->size, "%.1f MB", (double)upd_size_of(true) / 1e6);
		snprintf(c->full, sizeof c->full, "%.0f MB", (double)upd_size_of(false) / 1e6);
		if (o->can_small) {
			c->ids[n] = UP_DOWNLOAD;
			rows[n++] = (menu_row){ "Update", c->online ? c->size : "needs Wi-Fi", true };
		}
		c->ids[n] = UP_FULL;
		rows[n++] = (menu_row){ "Full Install", c->online ? c->full : "needs Wi-Fi", true };
		c->ids[n] = UP_NONE; rows[n++] = MENU_NOTE("Games, saves, settings stay");
		break;
	case UPD_CURRENT:
		/* Up to date: Full Install puts this version back whole, to repair
		 * a card whose ChaOS files have gone wrong */
		snprintf(c->full, sizeof c->full, "%.0f MB", (double)upd_size_of(false) / 1e6);
		c->ids[n] = UP_FULL;
		rows[n++] = (menu_row){ "Full Install", c->online ? c->full : "needs Wi-Fi",
		                        o->root[0] != '\0' };
		c->ids[n] = UP_CHECK;
		rows[n++] = (menu_row){ "Check Now", c->online ? NULL : "needs Wi-Fi", true };
		c->ids[n] = UP_NONE; rows[n++] = MENU_NOTE("Full Install reinstalls this one");
		break;
	case UPD_INSTALLER:
		/* A release on a newer TortOS base: not ChaOS's root alone but the
		 * whole system, boot partition and all, which the updater in the
		 * root writes after everything else has stopped (TortOS 2.2.0's
		 * System Update). A card from before that updater needs ChaOS
		 * Installer once; this one has it. */
		c->ids[n] = UP_SYSTEM;
		rows[n++] = (menu_row){ "Update", c->online ? NULL : "needs Wi-Fi", true };
		c->ids[n] = UP_NONE; rows[n++] = MENU_NOTE("The whole system, then a restart");
		c->ids[n] = UP_NONE; rows[n++] = MENU_NOTE("Games, saves, settings stay");
		break;
	/* (downloading, unpacking and ready: the steps, above) */
	case UPD_FAILED:
		c->ids[n] = UP_NONE; rows[n++] = MENU_NOTE(upd_error());
		/* then Try Again, below */
		__attribute__((fallthrough));
	default:
		c->ids[n] = UP_CHECK;
		/* Live without Wi-Fi too: A then says what is wrong with it, Wi-Fi
		 * off or not connected, rather than the row doing nothing */
		rows[n++] = (menu_row){ s == UPD_FAILED ? "Try Again" : "Check Now",
		                        c->online ? NULL : "needs Wi-Fi",
		                        s != UPD_CHECKING };
		break;
	}
	return n;
}

static menu_result upd_key(app *a, void *ctx, in_button key, int sel)
{
	upd_ctx *c = ctx;
	const ota_info *o = upd_latest();
	int id = sel >= 0 && sel < 16 ? c->ids[sel] : UP_NONE;
	char q[64];

	if (key != IN_ACCEPT) return MENU_STAY;
	if (id == UP_DOWNLOAD || id == UP_FULL) c->last = id;
	else if (id == UP_CHECK && upd_get() != UPD_FAILED) c->last = UP_CHECK;
	if ((id == UP_CHECK || id == UP_DOWNLOAD || id == UP_FULL || id == UP_SYSTEM) &&
	    !c->online) {
		upd_offline(c->wifi == WIFI_OFF);
		return MENU_STAY;
	}
	switch (id) {
	case UP_NOTES:
		snprintf(q, sizeof q, "What's New in %s", o->version);
		if (upd_notes()) synopsis_screen(a, q, upd_notes(), MENU_ACCENT);
		break;
	case UP_CHECK:
		/* Try Again repeats what failed: the download it was (Update or Full
		 * Install), or else the check */
		if (upd_get() == UPD_FAILED && (c->last == UP_DOWNLOAD || c->last == UP_FULL) &&
		    o->root[0] && upd_download(c->last == UP_DOWNLOAD))
			break;
		upd_check();
		break;
	case UP_DOWNLOAD:
		upd_download(true);
		break;
	case UP_FULL:
		upd_download(false);
		break;
	case UP_SYSTEM:
		image_update_screen(a);
		break;
	case UP_CANCEL:
		if (confirm_panel(a, "Updates", "Stop the download?", "Stop")) upd_cancel();
		break;
	case UP_REMOVE:
		if (confirm_panel(a, "Updates", "Remove the download?", "Remove")) upd_remove();
		break;
	case UP_INSTALL:
		snprintf(q, sizeof q, o->small ? "Update to ChaOS %s now?" : "Full Install of ChaOS %s now?",
		         o->version);
		if (!confirm_panel(a, "Updates", q, "Install and Restart")) break;
		if (o->small) wait_panel(a, "Updates", "Installing...");
		if (!upd_arm()) break;
		/* The restart: as the power button does it, with launch.sh told to
		 * boot again rather than power off. The boot writes the new system
		 * and checks it, a few seconds longer than usual, then ChaOS
		 * copies itself onto the card and comes up as it was. */
		{
			FILE *f = fopen(CHAOS_REBOOT_FLAG, "w");
			if (f) fclose(f);
		}
		power_off(a);
		return MENU_DONE;
	default:
		break;
	}
	return MENU_STAY;
}

/* ---- System Update: the screen (TortOS#69, src/update.h) --------------------
 *
 * What the last check said, and the way to the update: a confirm, then the
 * release's SHA256SUMS, the image into .update/ on the card with a bar, its
 * SHA-256 against both GitHub's digest and SHA256SUMS, the request, and init
 * told to hand over to the updater (plat_update_handoff), which writes it
 * after everything else has stopped. Every step is a little of each frame,
 * so the power button and B always answer. */

#define UP_FEED_TMP "/tmp/tortos-release.json"
#define UP_SUMS_TMP "/tmp/tortos-SHA256SUMS"

typedef enum {
	UP_IDLE,          /* showing what is known */
	UP_CHECKING,      /* asking the release feed */
	UP_SUMS,          /* fetching SHA256SUMS, before the image */
	UP_DOWNLOADING,   /* the image, into .update/ */
	UP_VERIFYING,     /* its SHA-256, read back off the card */
} up_phase;

static long long up_file_size(const char *path)
{
	struct stat st;

	return stat(path, &st) == 0 ? (long long)st.st_size : -1;
}

/* A small file whole and NUL terminated, or NULL if it isn't there or runs
 * past max. */
static char *up_slurp(const char *path, size_t max, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	size_t n;

	if (!f) return NULL;
	buf = malloc(max + 1);
	n = buf ? fread(buf, 1, max + 1, f) : 0;
	fclose(f);
	if (!buf || n > max) { free(buf); return NULL; }
	buf[n] = '\0';
	*len = n;
	return buf;
}

/* TortOS's shelf check and toast (#69 step 4) are left out: ChaOS's own
 * check (upd_poll, src/update.c) runs from the shelf and says so with its
 * notice. Nothing asks in the background here, so there is nothing to give
 * up. */
static void upbg_abandon(void)
{
}

/* Clear .update/ of everything but `keep`, the updater's log and its last
 * result: an older release's image, a half download, a request nobody
 * acted on. */
static void up_tidy(const char *keep)
{
	char dir[CFG_STR * 2], path[CFG_STR * 3];
	struct dirent *e;
	DIR *d;

	up_path(dir, sizeof dir, "");
	if (!(d = opendir(dir))) return;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || !strcmp(e->d_name, "update.log") ||
		    !strcmp(e->d_name, "result") || (keep && !strcmp(e->d_name, keep)))
			continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		unlink(path);
	}
	closedir(d);
}

/* What stands in the way of updating now, or NULL. */
static const char *up_blocked(bool net, char *buf, size_t n)
{
	char path[CFG_STR * 3];
	struct statvfs vf;
	bool charging = false, readable;
	int pct = 0;

	if (!net) return "Needs Wi-Fi";
	readable = plat_battery(&pct, &charging);
	if (!update_battery_ok(readable, charging, pct)) {
		snprintf(buf, n, "Plug in or charge above %d%%", UPDATE_BATTERY_MIN_PCT);
		return buf;
	}
	up_path(path, sizeof path, g_up_rel.file);
	if (up_file_size(path) != g_up_rel.size && statvfs(P_CARD, &vf) == 0 &&
	    (long long)vf.f_bavail * (long long)vf.f_frsize < g_up_rel.size + (1 << 20)) {
		snprintf(buf, n, "Needs %lld MB free on the card", (g_up_rel.size >> 20) + 1);
		return buf;
	}
	return NULL;
}

static void up_draw(app *a, const char *heading, const menu_row *rows, int n, int sel)
{
	tick_tint(a);
	draw_shelf(a);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
	SDL_RenderFillRect(a->r, NULL);
	menu_draw(a, heading, rows, n, sel, menu_std_width(a), MENU_ACCENT);
	plat_draw_osd(a->r);
	plat_present();
}

/* The device downloads it, then restarts: asked once. It starts on "Not
 * now", as every confirm here does (confirm_panel), so the press that opened
 * it can't also answer it. */
static bool up_confirm(app *a, const char *heading)
{
	menu_row rows[] = {
		MENU_NOTE("The device downloads it,"),
		MENU_NOTE("then restarts to update."),
		MENU_NOTE("Games and saves stay."),
		MENU_HR,
		{ "Update now", NULL, true },
		{ "Not now", NULL, true },
	};
	int sel = 5;

	for (;;) {
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; return false; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return false; }
		if (menu_leaving(a)) return false;
		if (in_repeat(&a->in, IN_UP) || in_repeat(&a->in, IN_DOWN)) sel = sel == 4 ? 5 : 4;
		if (a->in.pressed[IN_ACCEPT]) return sel == 4;
		up_draw(a, heading, rows, 6, sel);
		SDL_Delay(8);
	}
}

/* The request written, then init told. Init stops everything, this process
 * with it, so returning at all means it didn't happen: the request goes, and
 * the reason is left in note. */
static void up_handoff(app *a, const char *heading, char *note, size_t n)
{
	menu_row rows[] = {
		{ "Restarting to update...", NULL, false },
		MENU_RULE,
		MENU_NOTE("Don't turn the device off"),
	};
	char path[CFG_STR * 3];
	bool charging = false, readable;
	int pct = 0;
	unsigned t0;

	readable = plat_battery(&pct, &charging);
	if (!update_battery_ok(readable, charging, pct)) {
		snprintf(note, n, "Plug in or charge above %d%%", UPDATE_BATTERY_MIN_PCT);
		return;
	}
	up_path(path, sizeof path, "request");
	if (!update_request_write(path, plat_device_name(), &g_up_rel)) {
		snprintf(note, n, "Couldn't write to the card");
		return;
	}
	up_draw(a, heading, rows, 3, -1);
	fprintf(stderr, "update: handing over to the updater for %s\n", g_up_rel.file);
	if (!plat_update_handoff()) {
		unlink(path);
		fprintf(stderr, "update: no updater on this system\n");
		snprintf(note, n, "This system can't update itself");
		return;
	}
	for (t0 = plat_now_ms(); plat_now_ms() - t0 < 60000; ) {
		up_draw(a, heading, rows, 3, -1);
		SDL_Delay(100);
	}
	unlink(path);
	fprintf(stderr, "update: init never stopped ChaOS\n");
	snprintf(note, n, "The restart didn't happen");
}

static void update_screen(app *a)
{
	upd_ctx c;

	memset(&c, 0, sizeof c);
	c.a = a;
	/* A set width, so the moving dots and the percentage do not make the
	 * panel breathe */
	menu_run(a, &(menu_style){ .accent = MENU_ACCENT, .fixed_w = 640 }, upd_build, upd_key, &c);
}

/* The boot's check, once Wi-Fi is up, and what it found said once on the
 * shelf. Called every pass of the shelf's loop. */
static char   g_upd_msg[96];
static Uint32 g_upd_msg_until;

/* ChaOS: a word on the shelf for a moment, in the update notice's pill */
static void shelf_say(const char *msg, unsigned ms)
{
	snprintf(g_upd_msg, sizeof g_upd_msg, "%s", msg);
	g_upd_msg_until = plat_now_ms() + ms;
	redraw_now();
}

static void upd_poll(void)
{
	static Uint32 next;
	static bool online;
	Uint32 now = plat_now_ms();

	if (now >= next) { online = net_online(); next = now + 5000; }
	if (upd_tick(online)) redraw_now();
	if (upd_announce(g_upd_msg, sizeof g_upd_msg)) {
		g_upd_msg_until = now + 7000;
		redraw_now();
	}
}

/* A pill across the top of the shelf while there is something to say */
static void draw_upd_notice(app *a)
{
	Uint32 now = plat_now_ms();
	TTF_Font *f = ui_font(UI_F_META);
	unsigned acc = MENU_ACCENT;
	SDL_Color edge = { (Uint8)(acc >> 16), (Uint8)(acc >> 8), (Uint8)acc, 255 };
	int w, h;

	const char *say = g_upd_msg;
	char work[96];

	/* ChaOS: an update going on in the background says how far, for as long
	 * as it goes - after a game too, which held it (launch) */
	if (upd_get() == UPD_DOWNLOADING || upd_get() == UPD_UNPACKING) {
		const ota_info *o = upd_latest();

		if (upd_get() == UPD_DOWNLOADING)
			snprintf(work, sizeof work, "Downloading ChaOS %s \xC2\xB7 %d%%",
			         o ? o->version : "", upd_percent());
		else
			snprintf(work, sizeof work, "Preparing ChaOS %s%.*s", o ? o->version : "",
			         (int)(now / 500 % 4), "...");
		say = work;
		redraw_at(now + 500);
	} else if (!g_upd_msg[0] || now >= g_upd_msg_until) {
		return;
	}
	w = ui_text_width(f, say) + 48;
	h = ui_font_height(UI_F_META) + 20;
	{
		SDL_Rect outer = { (TORTOS_SCREEN_W - w) / 2 - 2, 22, w + 4, h + 4 };
		SDL_Rect inner = { (TORTOS_SCREEN_W - w) / 2, 24, w, h };
		ui_round_rect(a->r, &outer, h / 2 + 2, edge);
		ui_round_rect(a->r, &inner, h / 2, (SDL_Color){ 23, 21, 28, 255 });
	}
	ui_text(a->r, f, say, TORTOS_SCREEN_W / 2, 34, 0, UI_TEXT);
	if (say == g_upd_msg) redraw_at(g_upd_msg_until);
}

/* ChaOS: the whole image, for an update ChaOS's own over-the-air one can't
 * make (a release on another TortOS base): TortOS's System Update, written
 * by the updater in the root after everything else has stopped. */
static void image_update_screen(app *a)
{
	static uint8_t chunk[1 << 20];
	char head[48], latest[40], action[48], size[40], note[64] = "", blocked_buf[64];
	char why[160], path[CFG_STR * 3], part[CFG_STR * 3 + 8], cache[CFG_STR * 2];
	const char *device = plat_device_name();
	up_phase phase = UP_IDLE;
	bool asked = false, failed = false, done = false;
	unsigned asked_at = 0;
	long long hashed = 0;
	int fd = -1;
	sha256 h;

	upbg_abandon();     /* the shelf's check, if it is asking: this screen asks */
	up_known();
	up_cache_path(cache, sizeof cache);
	/* ChaOS: asked every time it opens. It opens from Updates, whose own
	 * check has just found a release on another base, so the answer kept from
	 * up to a day ago may not know of it yet. */
	phase = UP_CHECKING;

	while (!done && !want_quit && a->running) {
		unsigned now = plat_now_ms();
		bool net = menu_wifi(NULL, 0) == WIFI_CONNECTED;
		bool busy = phase == UP_SUMS || phase == UP_DOWNLOADING || phase == UP_VERIFYING;
		menu_row rows[8];
		const char *blocked = NULL;
		int n = 0, sel = -1;

		snprintf(head, sizeof head, "Update to %s", g_up_rel.version);
		up_path(path, sizeof path, g_up_rel.file);
		snprintf(part, sizeof part, "%s.part", path);

		/* ---- the work, a step a frame ---- */
		if (phase == UP_CHECKING) {
			if (!net) {
				phase = UP_IDLE;
			} else if (!asked) {
				/* One request at a time: box art may have the slot */
				if (net_get_async(UPDATE_FEED, UP_FEED_TMP, 20)) asked = true;
				else if (!asked_at) asked_at = now;
				else if (now - asked_at > 10000) { phase = UP_IDLE; failed = true; }
			} else {
				int got = net_async_poll();

				if (got != 0) {
					update_release rel;
					update_state st = UPDATE_UNKNOWN;
					size_t len = 0;
					char *reply = got == 1 ? up_slurp(UP_FEED_TMP, 256 * 1024, &len) : NULL;

					snprintf(why, sizeof why, "no answer from the release feed");
					if (reply)
						st = update_answer(reply, len, device, CHAOS_VERSION, cache,
						                   time(NULL), &rel, why, sizeof why);
					free(reply);
					unlink(UP_FEED_TMP);
					fprintf(stderr, "update: checked: %s\n", why);
					if (st != UPDATE_UNKNOWN) {
						g_up_state = st;
						if (st == UPDATE_WAITING) g_up_rel = rel;
						up_set_value();
					}
					failed = st == UPDATE_UNKNOWN;
					asked = false;
					asked_at = 0;
					phase = UP_IDLE;
				}
			}
		} else if (phase == UP_SUMS || phase == UP_DOWNLOADING) {
			int got = net_async_poll();

			a->idle.since_ms = now;          /* Auto Off waits for a download */
			if (got == -1) {
				unlink(part);
				fprintf(stderr, "update: the download stopped (curl %d, HTTP %d)\n",
				        net_async_exit(), net_async_http());
				snprintf(note, sizeof note, "The download stopped");
				phase = UP_IDLE;
			} else if (got == 1 && phase == UP_SUMS) {
				size_t len = 0;
				char *sums = up_slurp(UP_SUMS_TMP, 64 * 1024, &len);
				char hex[65] = "";
				uint8_t digest[32];

				if (sums) {
					sha256_start(&h);
					sha256_add(&h, sums, len);
					sha256_finish(&h, digest, hex);
				}
				unlink(UP_SUMS_TMP);
				if (!sums || strcmp(hex, g_up_rel.sums_sha256) ||
				    !update_sums_list(sums, len, g_up_rel.file, g_up_rel.sha256)) {
					fprintf(stderr, "update: SHA256SUMS doesn't match the release\n");
					snprintf(note, sizeof note, "The release's checksums don't match");
					phase = UP_IDLE;
				} else if (up_file_size(path) == g_up_rel.size) {
					phase = UP_VERIFYING;    /* downloaded before: check it again */
				} else if (net_get_async(g_up_rel.url, path, 3600)) {
					phase = UP_DOWNLOADING;
				} else {
					snprintf(note, sizeof note, "The download couldn't start");
					phase = UP_IDLE;
				}
				free(sums);
				if (phase == UP_VERIFYING) {
					fd = open(path, O_RDONLY | O_CLOEXEC);
					sha256_start(&h);
					hashed = 0;
					if (fd < 0) { snprintf(note, sizeof note, "The download stopped"); phase = UP_IDLE; }
				}
			} else if (got == 1) {
				fd = open(path, O_RDONLY | O_CLOEXEC);
				sha256_start(&h);
				hashed = 0;
				phase = fd >= 0 ? UP_VERIFYING : UP_IDLE;
				if (fd < 0) snprintf(note, sizeof note, "The download stopped");
			}
		} else if (phase == UP_VERIFYING) {
			unsigned until = now + 25;
			ssize_t got = 1;

			a->idle.since_ms = now;
			while (got > 0 && plat_now_ms() < until)
				if ((got = read(fd, chunk, sizeof chunk)) > 0) {
					sha256_add(&h, chunk, (size_t)got);
					hashed += got;
				}
			if (got <= 0) {
				uint8_t digest[32];
				char hex[65];

				close(fd);
				fd = -1;
				sha256_finish(&h, digest, hex);
				if (got < 0 || hashed != g_up_rel.size || strcmp(hex, g_up_rel.sha256)) {
					fprintf(stderr, "update: the download doesn't match: %lld bytes, %s\n",
					        hashed, hex);
					unlink(path);
					snprintf(note, sizeof note, "The download didn't match");
					phase = UP_IDLE;
				} else {
					fprintf(stderr, "update: %s matches both SHA-256s\n", g_up_rel.file);
					phase = UP_IDLE;
					up_handoff(a, head, note, sizeof note);
				}
			}
		}
		busy = phase == UP_SUMS || phase == UP_DOWNLOADING || phase == UP_VERIFYING;

		/* ---- the rows ---- */
		if (phase == UP_CHECKING) {
			rows[n++] = (menu_row){ "Installed", CHAOS_VERSION, false };
			rows[n++] = (menu_row){ "Latest", "checking...", false };
		} else if (busy) {
			long long have = phase == UP_DOWNLOADING ? up_file_size(part) : 0;

			if (have < 0) have = 0;
			if (phase == UP_VERIFYING) {
				rows[n++] = (menu_row){ "Checking the download", "", false };
				menu_bar_fill(1.0f);
			} else {
				snprintf(size, sizeof size, "%lld / %lld MB", (have + (1 << 19)) >> 20,
				         (g_up_rel.size + (1 << 19)) >> 20);
				rows[n++] = (menu_row){ "Downloading", size, false };
				menu_bar_fill(g_up_rel.size > 0 ? (float)have / (float)g_up_rel.size : 0.0f);
			}
			rows[n++] = MENU_BAR;
			rows[n++] = MENU_RULE;
			rows[n++] = MENU_NOTE("B: cancel");
		} else {
			const char *say = note;

			if (g_up_state == UPDATE_WAITING) {
				snprintf(latest, sizeof latest, "%s", g_up_rel.version);
				blocked = up_blocked(net, blocked_buf, sizeof blocked_buf);
				snprintf(action, sizeof action, "Update to %s", g_up_rel.version);
				snprintf(size, sizeof size, "%lld MB", (g_up_rel.size + (1 << 19)) >> 20);
				if (!say[0]) say = blocked ? blocked : "A: update";
			} else {
				snprintf(latest, sizeof latest, "%s",
				         g_up_state == UPDATE_CURRENT ? CHAOS_VERSION
				         : failed ? "no answer" : net ? "not checked" : "needs Wi-Fi");
				snprintf(action, sizeof action, "Check again");
				if (!say[0])
					say = g_up_state == UPDATE_CURRENT ? "Up to date"
					    : failed ? "Couldn't reach GitHub" : !net ? "Needs Wi-Fi" : "";
			}
			rows[n++] = (menu_row){ "Installed", CHAOS_VERSION, false };
			rows[n++] = (menu_row){ "Latest", latest, false };
			rows[n++] = MENU_HR;
			if (g_up_state == UPDATE_WAITING)
				rows[n++] = (menu_row){ action, size, !blocked };
			else
				rows[n++] = (menu_row){ action, NULL, net };
			if (rows[n - 1].live) sel = n - 1;
			if (say[0]) {
				rows[n++] = MENU_RULE;
				rows[n++] = MENU_NOTE(say);
			}
		}

		/* ---- the buttons ---- */
		plat_input_poll(&a->in);
		if (a->in.quit_requested || a->in.pressed[IN_POWER] || (!busy && idle_due(a)) ||
		    (busy && (a->in.pressed[IN_BACK] || a->in.pressed[IN_MENU]))) {
			bool leave = !(busy && (a->in.pressed[IN_BACK] || a->in.pressed[IN_MENU]));

			if (phase == UP_CHECKING || phase == UP_SUMS || phase == UP_DOWNLOADING)
				net_async_abort();
			if (fd >= 0) { close(fd); fd = -1; }
			unlink(part);
			if (busy) fprintf(stderr, "update: stopped by the player\n");
			phase = UP_IDLE;
			note[0] = '\0';
			if (a->in.pressed[IN_MENU]) g_menu_closing = true;
			if (a->in.quit_requested) { a->running = false; return; }
			if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
			if (leave || g_menu_closing) done = true;
			continue;
		}
		if (!busy && menu_leaving(a)) {
			if (phase == UP_CHECKING) net_async_abort();
			done = true;
		}
		if (!busy && phase == UP_IDLE && sel >= 0 && a->in.pressed[IN_ACCEPT]) {
			note[0] = '\0';
			if (g_up_state != UPDATE_WAITING) {
				phase = UP_CHECKING;
				failed = false;
			} else if (up_confirm(a, head) && a->running) {
				fprintf(stderr, "update: ChaOS %s: %s\n", g_up_rel.version, g_up_rel.file);
				up_path(path, sizeof path, "");
				mkdir(path, 0755);
				up_tidy(g_up_rel.file);
				if (net_get_async(g_up_rel.sums_url, UP_SUMS_TMP, 30)) phase = UP_SUMS;
				else snprintf(note, sizeof note, "The download couldn't start");
			}
		}
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		up_draw(a, busy ? head : "Updates", rows, n, sel);
		SDL_Delay(phase == UP_VERIFYING ? 1 : 8);
	}
}

/* What the updater said, once, at the first start after it ran: done, or
 * nothing written and why, or stopped while writing. The line comes off the
 * card once shown; the updater's log stays. Half downloads from a run that
 * was cut short go too. */
static void update_after_start(app *a)
{
	char dir[CFG_STR * 2], path[CFG_STR * 3], line[512] = "", detail[256], now[300];
	struct dirent *e;
	update_result res;
	DIR *d;
	FILE *f;

	up_path(dir, sizeof dir, "");
	if ((d = opendir(dir))) {
		while ((e = readdir(d))) {
			size_t k = strlen(e->d_name);

			if (k > 5 && !strcmp(e->d_name + k - 5, ".part")) {
				snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
				unlink(path);
			}
		}
		closedir(d);
	}
	up_path(path, sizeof path, "result");
	if (!(f = fopen(path, "r"))) return;
	if (!fgets(line, sizeof line, f)) line[0] = '\0';
	fclose(f);
	unlink(path);
	res = update_result_parse(line, detail, sizeof detail);
	if (res == UPDATE_RESULT_NONE) return;
	fprintf(stderr, "update: the updater said: %s", line);

	for (;;) {
		menu_row rows[6];
		int n = 0;
		const char *heading = "The update didn't finish";

		if (res == UPDATE_RESULT_DONE) {
			heading = "Updated";
			snprintf(now, sizeof now, "ChaOS is now %s", detail);
			rows[n++] = MENU_NOTE(now);
		} else if (res == UPDATE_RESULT_UNTOUCHED) {
			rows[n++] = MENU_NOTE("Nothing was changed.");
			rows[n++] = MENU_NOTE("Try again from Updates.");
		} else {
			rows[n++] = MENU_NOTE("It stopped while writing.");
			rows[n++] = MENU_NOTE("If anything seems wrong,");
			rows[n++] = MENU_NOTE("use ChaOS Installer.");
		}
		rows[n++] = MENU_RULE;
		rows[n++] = MENU_NOTE("A: OK");

		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER]) { power_off(a); return; }
		if (a->in.pressed[IN_ACCEPT] || a->in.pressed[IN_BACK] || a->in.pressed[IN_MENU]) break;
		up_draw(a, heading, rows, n, -1);
		SDL_Delay(8);
	}
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

static void about_screen(app *a)
{
	char ver[48], addr[80], batt[32], up[48];
	menu_row rows[4];
	char ssid[WIFI_SSID_MAX], ip[64];
	int pct = 0;
	bool charging = false;
	unsigned secs;
	bool done = false;

	snprintf(ver, sizeof ver, "%s", CHAOS_VERSION);

	while (!done && !want_quit && a->running) {
		static unsigned next_check;
		unsigned now = plat_now_ms();

		/* Same two-second cache as the menu row, and for the same reason:
		 * this loop runs every frame and wifi_status forks wpa_cli. */
		if (next_check == 0 || now >= next_check) {
			wifi_state ws = wifi_status(ssid, sizeof ssid, ip, sizeof ip);
			next_check = now + 2000;
			if (ws == WIFI_CONNECTED && ip[0])
				snprintf(addr, sizeof addr, "%s", ip);
			else if (ws == WIFI_CONNECTED)
				snprintf(addr, sizeof addr, "no address yet");
			else
				snprintf(addr, sizeof addr, "not connected");
			if (plat_battery(&pct, &charging))
				snprintf(batt, sizeof batt, "%d%%%s", pct,
				         charging ? " charging" : "");
			else
				snprintf(batt, sizeof batt, "unknown");
		}
		/* ChaOS: since the device started (/proc/uptime), not since this
		 * launcher did - it is restarted after a crash, and was counting
		 * from then. The Brick has no sleep, so on is awake. */
		{
			FILE *u = fopen("/proc/uptime", "r");
			double up_s = 0;

			if (!u || fscanf(u, "%lf", &up_s) != 1) up_s = now / 1000.0;
			if (u) fclose(u);
			secs = (unsigned)up_s;
		}
		snprintf(up, sizeof up, "%uh %02um", secs / 3600, (secs / 60) % 60);

		rows[0] = (menu_row){ "Version",   ver,  false };
		rows[1] = (menu_row){ "Address",   addr, false };
		rows[2] = (menu_row){ "Battery",   batt, false };
		rows[3] = (menu_row){ "Awake for", up,   false };

		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (menu_leaving(a)) done = true;
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);

		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		tick_tint(a);
		draw_shelf(a);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw(a, "About ChaOS", rows, 4, -1, menu_std_width(a), MENU_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(8);
	}
}

/* Which page the shelf's controls are described on. See controls.h: the page
 * follows UI Direction, because which axis moves is what that setting does. */
static ctl_dir controls_dir(void)
{
	if (CARD_DIRS[g_dir].both)     return CTL_CUBIC;
	/* a List runs as a column does: up and down move, left and right jump */
	if (CARD_DIRS[g_dir].vertical || LIST_VIEW_ON()) return CTL_VERTICAL;
	return CTL_HORIZONTAL;
}

/* MENU > Controls: what every button does, a page per place. Read-only like
 * About, and paged with left and right the way Play Time changes its window -
 * the whole list at once is thirty rows nobody reads. */
static void controls_screen(app *a)
{
	ctl_page page = CTL_MOVING;
	bool done = false;

	ctl_set_fn_volume(plat_brightness_on_volume());
	ctl_set_battery_lights(plat_battery_lights());

	while (!done && !want_quit && a->running) {
		menu_row rows[CTL_MAX_ROWS];
		char head[80];
		int n = ctl_rows(page, controls_dir(), rows);

		snprintf(head, sizeof head, "Main Menu Controls");

		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (menu_leaving(a)) done = true;
		/* SELECT opens Muse here as it does on every other menu screen -
		 * which is one of the things this page exists to tell you. */
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);

		/* ChaOS: to the ends and no further, as the footer's arrows say */
		if (in_repeat(&a->in, IN_RIGHT) && page + 1 < CTL_PAGES) page = (ctl_page)(page + 1);
		if (in_repeat(&a->in, IN_LEFT) && page > 0) page = (ctl_page)(page - 1);
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		tick_tint(a);
		draw_shelf(a);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw(a, head, rows, n, -1, menu_std_width(a), MENU_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(8);
	}
}

/* The widest row of one built menu. */
static int menu_measure(const menu_row *rows, int n, const char *heading)
{
	TTF_Font *fm = ui_font(UI_F_MENU), *fh = ui_font(UI_F_LABEL);
	int gap = menu_row_h(), w = 0, i;
	bool two_col = false;

	/* The same three row kinds menu_draw knows about. This function is a
	 * second copy of that measuring pass and the two must agree, or a panel is
	 * sized by one rule and drawn by another. */
	for (i = 0; i < n; i++)
		if (rows[i].value && !ROW_IS_NOTE(rows[i])) two_col = true;
	for (i = 0; i < n; i++) {
		int rw;

		if (!rows[i].label) continue;          /* a rule measures nothing */
		rw = ui_text_width(fm, rows[i].label);
		if (two_col && rows[i].value && !ROW_IS_NOTE(rows[i]))
			rw += gap + (rows[i].toggle ? menu_switch_w(TTF_FontHeight(fm))
			                            : ui_text_width(fm, rows[i].value));
		if (rows[i].cycles) rw += TTF_FontHeight(fm) * 3 / 2;   /* the left arrow */
		if (rw > w) w = rw;
		/* ChaOS: its description, as menu_draw sizes for it */
		if (rows[i].hint && rows[i].hint[0]) {
			TTF_Font *df = ui_font(UI_F_META);
			int hw = df ? ui_text_width(df, rows[i].hint) : 0;

			if (hw > w) w = hw;
		}
	}
	if (heading) {
		int hw = ui_text_width(fh, heading);
		if (hw > w) w = hw;
	}
	return w;
}

/* One width for both shelf menus, every system, and every value their rows can
 * cycle to. The panel is a frame the lists sit inside rather than something
 * that resizes to whatever is selected: without this, cycling display mode from
 * "Fill" to "Integer tall" widens the slab under the cursor, and walking from
 * the systems row into a system resizes it again.
 *
 * Measured across all of that rather than picked, so a longer label, another
 * system or a larger font scale widens the frame instead of overflowing it.
 * Cached because it is a few hundred text measurements and none of its inputs
 * change while a menu is open. */
static int menu_shelf_width(app *a)
{
	menu_row rows[MENU_MAX_ROWS];
	menu_bufs bufs;
	const char *heading;
	int w, n, i, k;

	if (a->menu_w) return a->menu_w;

	n = menu_build(a, SCREEN_SYSTEMS, a->sys_cursor, rows, &bufs, &heading);
	w = menu_measure(rows, n, heading);

	/* Every value that can be cycled on this menu, at its widest, so the
	 * panel does not resize under the row being cycled. Sort By joined
	 * Display Mode in that category the moment it stopped being a
	 * placeholder - "Recently Added" is 187px wider than "Name".
	 *
	 * Muse's menu and Favorites' are their own rows, Sort By second and no
	 * Display Mode, so the enum's row numbers are only the rest's. Written
	 * through them on Muse's, this measured Rescan Folder carrying a sort
	 * order, a row no menu has. */
	for (i = 0; i < a->sys.count; i++) {
		bool muse = is_muse(&a->sys.systems[i]);
		bool own  = muse || a->view[i].owner;
		int  srow = own ? 1 : SM_SORT, show = -1;

		n = menu_build(a, SCREEN_GAMES, i, rows, &bufs, &heading);
		if (muse) {
			sm_muse_row ids[SM_MUSE_ROWS];
			int both = ml_count(&g_muse, true) && ml_count(&g_muse, false);
			int nn = sys_menu_muse_rows(muse_books_shown(), both,
			                            !plat_has_wifi(), ids);

			for (k = 0; k < nn; k++) {
				if (ids[k] == SMM_SORT) srow = k;
				if (ids[k] == SMM_SHOW) show = k;
			}
			/* The longer of Show's two values, so turning it over does not
			 * change the menu's width under the cursor. */
			if (show >= 0) {
				int mw;

				rows[show].value = "Audiobooks";
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
		}
		for (k = 0; k < (muse ? ML_ORDERS : SORT_COUNT); k++) {
			int mw;
			rows[srow].value = muse ? ml_order_label((ml_order)k, false) : SORTS[k].label;
			mw = menu_measure(rows, n, heading);
			if (mw > w) w = mw;
			if (muse) {
				rows[srow].value = ml_order_label((ml_order)k, true);
				mw = menu_measure(rows, n, heading);
				if (mw > w) w = mw;
			}
		}
	}
	a->menu_w = w;
	return w;
}

/* The width every menu reached from the shelf uses.
 *
 * Menus sized themselves to their own rows, so the panel changed size on
 * nearly every screen. Measured on the device 2026-09-08: the TortOS menu drew
 * a 794px panel, Play Time 791, Bluetooth 737, Wi-Fi 640 - so opening Wi-Fi
 * from the menu shrank the panel by 154px, about 15% of the display, and grew
 * it again on the way out.
 *
 * Sizing to content was right while a label that did not fit was cut, because
 * cutting a name is a loss. A long label scrolls now, so a panel no longer has
 * to grow to hold one, and the trade is a little empty air in a narrow menu
 * against a panel that stops jumping.
 *
 * 800px of PANEL, stated rather than measured, so it does not drift when a
 * row's text changes. What menu_draw takes is the content width and it adds
 * menu_pad() on each side, so the pad comes off here - by calling the same
 * function menu_draw calls, because a hardcoded 48 would be right only at one
 * text size.
 *
 * The floor is not decoration. menu_shelf_width is the widest row in the
 * shelf menus, and a panel narrower than that sets one of them scrolling. It
 * is "Bluetooth" and the paired headset's name today - a row that grows with
 * a device nobody here chose the name of - measured at 699px against the 704
 * of content 800 leaves. That it clears is not an estimate: the panel draws at
 * exactly 800 on the device, and had the row been wider max() would have
 * widened the panel rather than let it scroll. Five pixels is not much, and
 * max() is the whole reason a longer name costs air instead of a cut.
 *
 * Not for the one-row notices - "Scanning...", "Could not start". Those are
 * messages, and a message stretched to menu width reads as a menu that failed
 * to load. Not for the in-game menu either, which sits over a paused game. */
static int menu_std_width(app *a)
{
	int content = 800 - menu_pad() * 2;
	int floor   = menu_shelf_width(a);

	return floor > content ? floor : content;
}

/* Date & Time (src/clock.h). Each row is one part of the clock, changed with
 * left and right and set the moment it changes: the system clock and the
 * hardware clock both, so it holds through a power-off. Time Zone steps
 * through CLOCK_ZONES and is the library database's "timezone", which the
 * Brick's launch.sh reads at boot as well. */
typedef struct {
	char field[CLK_FIELDS][24];
	char zone[64];   /* a city, or a zone id not on the list as it is */
} clock_ui;

static const char *const CLOCK_ROW_NAMES[CLK_FIELDS] = {
	"Year", "Month", "Day", "Hour", "Minute",
};

static int clock_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	clock_ui *c = ctx;
	time_t now = time(NULL);
	char id[64];
	int i, z, n = 0;

	*heading = "Date & Time";
	for (i = 0; i < CLK_FIELDS && n < max; i++) {
		clock_field_label(now, (clock_field)i, c->field[i], sizeof c->field[i]);
		rows[n++] = (menu_row){ CLOCK_ROW_NAMES[i], c->field[i], true };
	}
	db_get_str(db_lib(), "timezone", id, sizeof id, "America/New_York");
	z = clock_zone_find(id);
	snprintf(c->zone, sizeof c->zone, "%s", z >= 0 ? CLOCK_ZONES[z].city : id);
	if (n < max) rows[n++] = (menu_row){ "Time Zone", c->zone, true };
	/* The status bar: the clock top left, the battery top right. Their rows
	 * sit straight under Time Zone, in the order clock_key reads them. */
	if (n + 3 <= max) {
		rows[n++] = (menu_row){ "Clock", g_status_clock ? "On" : "Off", true, .toggle = true };
		/* Which of two, so a value and arrows rather than a switch: a switch
		 * reads On or Off, and neither says which format is on */
		rows[n++] = (menu_row){ "12h/24h Clock", g_status_clock24 ? "24h" : "12h", true, .cycles = true };
		rows[n++] = (menu_row){ "Battery", STATUS_BATT_NAMES[g_status_batt], true, .cycles = true };
	}
	if (n + 3 <= max) {
		rows[n++] = MENU_RULE;
		rows[n++] = MENU_NOTE("Left/right: change");
		/* Only where it is true: the Brick's ntpd sets the clock again once
		 * Wi-Fi connects, so a time set here lasts until then */
		if (plat_has_wifi()) rows[n++] = MENU_NOTE("Wi-Fi sets the clock when connected");
	}
	return n;
}

static menu_result clock_key(app *a, void *ctx, in_button key, int sel)
{
	int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;

	(void)a; (void)ctx;
	/* The switches also take A, as the rest of the menus do */
	if (!d && key != IN_ACCEPT) return MENU_STAY;
	if (sel >= 0 && sel < CLK_FIELDS) {
		if (d) plat_clock_set(clock_step(time(NULL), (clock_field)sel, d));
	} else if (sel == CLK_FIELDS) {
		char id[64];
		int z;

		if (!d) return MENU_STAY;
		db_get_str(db_lib(), "timezone", id, sizeof id, "America/New_York");
		z = clock_zone_step(id, d);
		db_set_str(db_lib(), "timezone", CLOCK_ZONES[z].id);
		plat_clock_zone(CLOCK_ZONES[z].id);
	} else if (sel == CLK_FIELDS + 1) {
		g_status_clock = !g_status_clock;
		db_set_int(db_dev(), "status.clock", g_status_clock ? 1 : 0);
	} else if (sel == CLK_FIELDS + 2) {
		g_status_clock24 = !g_status_clock24;
		db_set_int(db_dev(), "status.clock24", g_status_clock24 ? 1 : 0);
	} else if (sel == CLK_FIELDS + 3) {
		g_status_batt = (g_status_batt + (d ? d : 1) + STATUS_BATT_COUNT) % STATUS_BATT_COUNT;
		db_set_int(db_dev(), "status.battery", g_status_batt);
	}
	return MENU_STAY;
}

static void clock_screen(app *a)
{
	clock_ui c;
	menu_style st = {
		/* Fixed, so the panel does not change size as "May" becomes
		 * "September" under a held key */
		.fixed_w = menu_std_width(a),
		.accent  = MENU_ACCENT,
	};

	memset(&c, 0, sizeof c);
	menu_run(a, &st, clock_build, clock_key, &c);
}

static void tortos_menu_draw(app *a, int sel)
{
	menu_row rows[MENU_MAX_ROWS];
	menu_bufs bufs;
	const char *heading;
	int n = menu_build(a, a->screen, a->sys_cursor, rows, &bufs, &heading);

	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
	SDL_RenderFillRect(a->r, NULL);
	/* The shelf's menu is TortOS's own on the systems screen and a system's on
	 * a game list, which is where it gains rows that belong to that system. */
	g_menu_root = !g_appearance_page;   /* ChaOS: Appearance is a submenu */
	menu_draw(a, heading, rows, n, sel, menu_shelf_width(a),
	          a->screen == SCREEN_SYSTEMS ? MENU_ACCENT : a->tint);
	g_menu_root = false;
}

/* The system menu's context. It holds the app rather than a copy of what was
 * on screen when it opened, so `a->screen` and `a->sys_cursor` are read live
 * exactly as the hand-written loop read them. Neither can change while the
 * menu is up; reading them is simply one fewer thing that could drift. */
typedef struct {
	app      *a;
	menu_bufs bufs;   /* the built rows point into this, so it outlives them */
	/* Which of the two menus to build. Normally the screen you are on, but
	 * Both has no screens to be on: there MENU means the firmware and B
	 * means this system, and neither can be inferred from where you are. */
	int       screen;
} sysmenu_ctx;

static int sysmenu_build(void *ctx, menu_row *rows, int max,
                         const char **heading)
{
	sysmenu_ctx *c = ctx;
	int n = menu_build(c->a, c->screen, c->a->sys_cursor, rows, &c->bufs,
	                   heading);

	return n > max ? max : n;
}

/* Left and right cycle the value on a row that has one; A opens whatever the
 * row leads to. Everything else the runner has already dealt with. */
static menu_result sysmenu_key(app *a, void *ctx, in_button key, int sel)
{
	int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;

	(void)ctx;

	if (a->screen == SCREEN_GAMES) {
		/* Muse's menu is its own rows - see SM_MUSE_ROWS - and which is
		 * where depends on what its shelf shows, so the enum's row numbers
		 * below mean nothing on it: sys_menu_muse_rows says. Sort By goes on
		 * to the Sort By below like any shelf's. Album Art is a dead row off
		 * the network, and the runner does not land on dead rows. */
		if (is_muse(&a->sys.systems[a->sys_cursor])) {
			sm_muse_row ids[SM_MUSE_ROWS];
			int n = sys_menu_muse_rows(muse_books_shown(),
			                           ml_count(&g_muse, true) && ml_count(&g_muse, false),
			                           !plat_has_wifi(), ids);
			sm_muse_row id = sel >= 0 && sel < n ? ids[sel] : SMM_COUNT;

			if (id == SMM_SORT) {
				sel = SM_SORT;
			} else if (id == SMM_LOCK) {
				/* Closes the menu; the screen underneath locks. */
				if (key != IN_ACCEPT) return MENU_STAY;
				muse_ask_lock();
				return MENU_DONE;
			} else if (id == SMM_SHOW) {
				/* Two values, so left, right and A all turn it over. */
				if (d || key == IN_ACCEPT)
					muse_show(&a->view[a->sys_cursor], !muse_books_shown());
				return MENU_STAY;
			} else {
				if (key != IN_ACCEPT) return MENU_STAY;
				if (id == SMM_ART) { album_art_screen(a); return MENU_STAY; }
				if (id != SMM_RESCAN) return MENU_STAY;
				wait_panel(a, "Muse", "Scanning...");
				rescan_all(a);
				return MENU_DONE;
			}
		}
		/* Favorites' menu is its own rows too, Games and Sort By - see
		 * SM_FAV_ROWS - so its Sort By is row 1 and not SM_SORT. Nothing here
		 * knew that from the day the menu was cut to two rows, and left and
		 * right on Favorites' Sort By did nothing at all. */
		if (a->view[a->sys_cursor].owner) sel = sel == 1 ? SM_SORT : SM_GAMES;
		/* And a console's rows lose Box Art on a device with no radio, which
		 * moves Rescan Folder up one: which row is which comes from the same
		 * list the build used. */
		else if (!is_muse(&a->sys.systems[a->sys_cursor])) {
			sys_ui dev = { .no_wifi = !plat_has_wifi(),
			               .shader = look_console_of(a->sys.systems[a->sys_cursor].tag)
			                         == LOOK_NONE ? NULL : "" };
			sm_row ids[SM_ROWS];
			int n = sys_menu_system_rows(&dev, ids);

			sel = sel >= 0 && sel < n ? (int)ids[sel] : SM_ROWS;
		}
		/* ChaOS. Shader: left and right turn it on and off, A opens its
		 * settings. Overlay: left, right and A go through None and the
		 * console's PNGs. Both saved the moment they change, as there is no
		 * confirm step to hang the write off. */
		if (sel == SM_SHADER) {
			sysview *v = &a->view[a->sys_cursor];

			if (d) {
				v->look.on = !v->look.on;
				look_save(a, a->sys_cursor);
			} else if (key == IN_ACCEPT) {
				shader_screen(a, a->sys_cursor, NULL);
			}
			return MENU_STAY;
		}
		if (sel == SM_OVERLAY) {
			if (d || key == IN_ACCEPT) overlay_cycle(a, a->sys_cursor, d ? d : 1);
			return MENU_STAY;
		}
		if (sel == SM_DISPLAY) {
			sysview *v = &a->view[a->sys_cursor];
			if (d || key == IN_ACCEPT) {
				v->dmode = (v->dmode + (d ? d : 1) + DMODE_COUNT) % DMODE_COUNT;
				display_save(a, a->sys_cursor);
				overlay_reload(a, a->sys_cursor);   /* that mode's */
			}
			return MENU_STAY;
		}
		if (sel == SM_BUTTONS) {
			if (key == IN_ACCEPT) console_controls_screen(a, a->sys_cursor, NULL, NULL, NULL);
			return MENU_STAY;
		}
		if (sel == SM_REWIND) {
			const char *tag = a->sys.systems[a->sys_cursor].tag;
			if (d || key == IN_ACCEPT) rewind_save(tag, !rewind_load(tag));
			return MENU_STAY;
		}
		/* Sort order, on the same left/right idiom.
		 *
		 * The cursor follows the GAME, not its index. Re-sorting under a
		 * cursor left where it sat means closing the menu onto a different
		 * game than the one that was selected when it opened - the shelf
		 * would appear to have jumped on its own. Which game you were
		 * looking at is the thing that survives a reorder; where it happened
		 * to sit in the old order is not. On Muse's shelf the same goes for
		 * the album, whose folder is its `file`. */
		if (d && sel == SM_SORT) {
			sysview *v = &a->view[a->sys_cursor];
			char keep[LIB_PATH];
			int i, keep_owner = -1;

			keep[0] = '\0';
			if (v->cursor >= 0 && v->cursor < v->list.count) {
				snprintf(keep, sizeof keep, "%s",
				         v->list.items[v->cursor].file);
				/* On Favorites a file name is not enough: the same one can
				 * be on the shelf twice, from two systems. */
				if (v->owner) keep_owner = v->owner[v->cursor];
			}

			v->sort = is_muse(&a->sys.systems[a->sys_cursor])
			        ? (v->sort + d + ML_ORDERS) % ML_ORDERS
			        : sort_step(v->sort, d);
			sort_shelf(a, a->sys_cursor);
			sort_save(a);

			/* The textures are indexed by position, so they moved with
			 * nothing. Dropping them lets the next frame fetch each card's
			 * art for where it now sits. */
			free_view_textures(v);

			v->cursor = 0;
			for (i = 0; i < v->list.count; i++)
				if (!strcmp(v->list.items[i].file, keep) &&
				    (!v->owner || v->owner[i] == keep_owner)) {
					v->cursor = i;
					break;
				}
			cf_reset(&v->cf, v->cursor);
			return MENU_STAY;
		}
		if (key != IN_ACCEPT) return MENU_STAY;
		/* The system's own color, not the menu's. This acts on the shelf you
		 * are looking at, and menu_draw already follows that rule everywhere
		 * else. */
		if (sel == SM_BOXART)
			art_screen(a, a->sys.systems[a->sys_cursor].folder, NULL, NULL,
			           NULL, a->sys.systems[a->sys_cursor].accent);
		if (sel == SM_RESCAN) {
			wait_panel(a, a->sys.systems[a->sys_cursor].name, "Scanning...");
			rescan_all(a);
			/* Close, rather than redraw this menu over a shelf that may have
			 * just lost the system it was built for. Acting and closing is
			 * also what pressing it means. */
			return MENU_DONE;
		}
		return MENU_STAY;
	}

	/* The row's identity rather than its position: a device with no radio has
	 * fewer of them, and the same list the build used says which is which. */
	{
		/* Every field that hides a row, as the build sets it - a row hidden
		 * there and not here moves every row under it by one (0.10.2's
		 * Transition Speed turned Menu Color) */
		sys_ui dev = { .no_wifi = !plat_has_wifi(), .no_bt = !plat_has_bluetooth(),
		               .cable_xfer = plat_cable_link(NULL, 0), .ss_have = ss_have_dev(),
		               .pm_have = ports_pm_ready(P_ROMS),
		               .night_ok = plat_volume_night_ok(), .appearance = g_appearance_page,
		               .name_speed = CARDS_DRAWN(g_cards) ? PN_SPEEDS[g_pn_speed[g_dir]].name
		                                                  : NULL,
		               .list_view = CARD_DIRS[g_dir].both ? NULL : "" };
		pm_row ids[PM_ROWS];
		int n = sys_menu_tortos_rows(&dev, ids);

		sel = sel >= 0 && sel < n ? (int)ids[sel] : PM_ROWS;
	}

	/* Auto Off, on the left/right idiom Display mode uses. */
	if (d && sel == PM_SLEEP) {
		int k, at = 0;

		for (k = 0; k < AUTO_OFF_COUNT; k++)
			if (AUTO_OFF[k] == a->auto_off) { at = k; break; }
		at += d;
		if (at < 0) at = 0;
		if (at >= AUTO_OFF_COUNT) at = AUTO_OFF_COUNT - 1;
		a->auto_off = AUTO_OFF[at];
		auto_off_save(a->auto_off);
		/* From now, not from whenever the last countdown began: choosing 30s
		 * should not inherit two minutes of an old one already spent. */
		a->idle.since_ms = plat_now_ms();
		return MENU_STAY;
	}
	/* Text size, same idiom. Changing it reopens every font, so the whole UI
	 * is rebuilt: the panel's cached width is measured from font metrics, and
	 * any card generated for a game with no box art has its title baked in at
	 * the old size. Both are dropped here rather than left subtly wrong. */
	/* The UI theme. No font is reopened, so only the
	 * card textures are dropped - but the same shape: change it, throw away
	 * what was drawn from the old value, draw it again. A steps as well as
	 * left and right, so the row can be cycled without leaving the thumb.
	 *
	 * Themes the systems shelf today. The name is deliberately wider than
	 * that, so menu colors or the accent rule can join without the row having
	 * to be renamed a second time. */
	if (sel == PM_THEME && (d || key == IN_ACCEPT)) {
		int k = cards_step(g_cards, d ? d : 1);

		if (k != g_cards) {
			int fit;

			g_cards = k;
			/* A set that cannot show the direction in use falls back to
			 * Horizontal, and the saved direction follows, so the row and
			 * the shelf agree on the next launch too. */
			fit = cards_dir_fit(g_cards, g_dir);
			if (fit != g_dir) {
				g_dir = fit;
				db_set_str(db_dev(), "cards_dir", CARD_DIRS[g_dir].id);
			}
			free_all_textures(a);
			prime_sys_window(a);
			db_set_str(db_dev(), "cards", CARD_SETS[g_cards].id);
		}
		return MENU_STAY;
	}

	/* Which way both shelves run. Only the cards' positions change, not the
	 * textures, so nothing is dropped and there is nothing to rebuild. */
	/* ChaOS: the menus' accent, seen on this very menu as it changes. */
	if (sel == PM_COLOR && (d || key == IN_ACCEPT)) {
		g_menu_color = menu_color_step(g_menu_color, d ? d : 1);
		notice_set_accent(MENU_ACCENT);   /* the in-game notices' border too */
		db_set_str(db_dev(), "menu_color", MENU_COLORS[g_menu_color].id);
		return MENU_STAY;
	}

	/* ChaOS: Game List View, Normal or List */
	if (sel == PM_LISTVIEW && (d || key == IN_ACCEPT)) {
		g_list_view = !g_list_view;
		db_set_str(db_dev(), "game_view", g_list_view ? "list" : "normal");
		return MENU_STAY;
	}

	/* ChaOS: the Recently Played shelf, on or off. Built or taken off at once,
	 * so the systems row shows the change behind the menu. */
	if (sel == PM_RECENT && (d || key == IN_ACCEPT)) {
		g_recent_shelf = !g_recent_shelf;
		db_set_str(db_dev(), "recent_shelf", g_recent_shelf ? "on" : "off");
		refresh_virtual_shelves(a);
		return MENU_STAY;
	}

	/* ChaOS: Favorites and Muse on the systems row, on or off, at once */
	if (sel == PM_SHELF_FAV && (d || key == IN_ACCEPT)) {
		g_fav_shelf = !g_fav_shelf;
		db_set_str(db_dev(), "fav_shelf", g_fav_shelf ? "on" : "off");
		refresh_virtual_shelves(a);
		return MENU_STAY;
	}
	if (sel == PM_SHELF_MUSE && (d || key == IN_ACCEPT)) {
		g_muse_shelf = !g_muse_shelf;
		db_set_str(db_dev(), "muse_shelf", g_muse_shelf ? "on" : "off");
		muse_shelf_set(a);
		return MENU_STAY;
	}

	/* ChaOS: night volume, on or off. The ladder is remapped at once, so the
	 * loudness holds and the level shown is the new one. */
	if (sel == PM_NIGHT && (d || key == IN_ACCEPT)) {
		plat_volume_night(!plat_volume_night_get());
		return MENU_STAY;
	}

	/* ChaOS: this direction's Transition Speed, round the three */
	if (sel == PM_SPEED && (d || key == IN_ACCEPT)) {
		char k[64];
		int step = d ? d : 1;
		g_pn_speed[g_dir] = (g_pn_speed[g_dir] + step + PN_SPEED_COUNT) % PN_SPEED_COUNT;
		snprintf(k, sizeof k, "pixnames.speed.%s", CARD_DIRS[g_dir].id);
		db_set_str(db_dev(), k, PN_SPEEDS[g_pn_speed[g_dir]].id);
		return MENU_STAY;
	}

	if (sel == PM_DIR && (d || key == IN_ACCEPT)) {
		g_dir = cards_dir_step_for(g_cards, g_dir, d ? d : 1);
		db_set_str(db_dev(), "cards_dir", CARD_DIRS[g_dir].id);
		return MENU_STAY;
	}

	/* Two positions, so left and right and A all do the same thing: there is
	 * nothing to step through, only something to turn off and on. */
	if (sel == PM_AUDIO && (d || key == IN_ACCEPT)) {
		g_aout_policy = g_aout_policy == AOUT_AUTO ? AOUT_SPEAKER : AOUT_AUTO;
		aout_save(g_aout_policy);
		/* Forced: the resolved DEVICE may not have changed - pinning Speaker
		 * with a cable in is still the codec - but the setting did, and the
		 * next unplug must act on the new policy rather than on a cached
		 * device string that happens to match. */
		aout_apply(true);
		return MENU_STAY;
	}

	/* Four positions, stepped either way like the card sets, A going forward.
	 * Applied at once, to Muse and to Diatom if they are there to be told, so
	 * a player trying them over music hears each as it is chosen. */
	if (sel == PM_CHANNELS && (d || key == IN_ACCEPT)) {
		g_channels = (aout_channels)((g_channels + (d ? d : 1) + AOUT_CHANNELS_COUNT)
		                             % AOUT_CHANNELS_COUNT);
		channels_save(g_channels);
		aout_apply(false);
		return MENU_STAY;
	}

	if (key != IN_ACCEPT) return MENU_STAY;
	switch (sel) {
	case PM_WIFI:         wifi_screen(a); break;
	case PM_XFER:         xfer_screen(a); break;
	case PM_SCRAPE:       art_screen(a, NULL, NULL, NULL, NULL, MENU_ACCENT); break;
	case PM_ACHIEVEMENTS: ra_signin_screen(a); break;
	case PM_SS:           ss_screen(a); break;
	case PM_BT:           bt_screen(a); break;
	case PM_CLOCK:        clock_screen(a); break;
	case PM_APPEARANCE:   appearance_screen(a); break;
	case PM_STATS:        if (stats_screen(a)) return MENU_DONE; break;
	case PM_CONTROLS:     controls_screen(a); break;
	case PM_PORTS:        pm_get(a); break;
	case PM_ROMM:         romm_screen(a); break;
	case PM_UPDATE:       update_screen(a); break;
	case PM_ABOUT:        about_screen(a); break;
	default: break;
	}
	return MENU_STAY;
}

static void tortos_menu_for(app *a, int screen)
{
	sysmenu_ctx c;

	memset(&c, 0, sizeof c);
	c.a = a;
	c.screen = screen;
	menu_style st = {
		/* Measured across every system and every display mode, so the panel
		 * does not resize while Display Mode is being cycled on it. */
		.fixed_w     = menu_std_width(a),
		.accent      = MENU_ACCENT,
		/* On a game shelf this menu is about THAT system, so it wears the
		 * shelf's color - which tick_tint is still moving while it is open. */
		.follow_tint = screen != SCREEN_SYSTEMS,
		.root        = true,
	};

	menu_run(a, &st, sysmenu_build, sysmenu_key, &c);
}

/* MENU > Appearance: the look rows on their own page, B back to the menu. The
 * same runner and keys as the menu; only the rows it lists are different. */
static void appearance_screen(app *a)
{
	sysmenu_ctx c;
	menu_style st = {
		.fixed_w     = menu_std_width(a),
		.accent      = MENU_ACCENT,
		.follow_tint = a->screen != SCREEN_SYSTEMS,
	};

	memset(&c, 0, sizeof c);
	c.a = a;
	c.screen = a->screen;
	g_appearance_page = true;
	menu_run(a, &st, sysmenu_build, sysmenu_key, &c);
	g_appearance_page = false;
}

static void tortos_menu(app *a)
{
	tortos_menu_for(a, a->screen);
}

/* ---------- launching ----------------------------------------------------- */

/* The in-game menu - the launcher's, over Diatom's pause.
 *
 * MENU in a game makes Diatom write a preview of the frame, stop presenting,
 * and say PAUSED. From that word the display is ours: the menu is that frame
 * dimmed, with the rows a player expects - Continue, Save, Load, Reset, Quit
 * - drawn with the launcher's own font and glow. The menu is the launcher's,
 * not something patched into the emulator.
 *
 * Every row acts through one protocol line. Save and Load use the same autosave
 * paths the launch handed over, so the autosave funnel stays one thing. */
/* Display sits with the things you do to the game rather than the things you do
 * to a save, because it is the one row whose effect you judge by looking at the
 * game behind the menu. */
/* gm_row, gm_ui and gm_rows: src/game_menu.h */

/* The paused frame, drawn where the game actually is.
 *
 * Diatom reports its rect with every DISPLAY message (put there so a launcher
 * need not recompute it from geometry it does not have), and the preview it
 * writes is the CORE'S frame - 256x224 for an NES, no display mode applied.
 * Stretching that to the panel, which is what this used to do, showed a game
 * that looked nothing like the one paused underneath at any mode that does
 * not fill the screen, and made the picture jump size the moment MENU was
 * pressed.
 *
 * Falls back to filling the panel when Diatom has not said - the standalone
 * path, and the first moments of a launch. */
static void draw_paused_frame(app *a, SDL_Texture *bg)
{
	SDL_Rect r;

	if (!bg) return;
	if (plat_resident_rect(&r)) SDL_RenderCopy(a->r, bg, NULL, &r);
	else                        SDL_RenderCopy(a->r, bg, NULL, NULL);
}

/* Copy the paused frame's preview beside a manual save, so the slot strip can
 * show what is inside each slot. The pause preview IS the frame the save
 * serializes - Diatom wrote it on the way into the menu - so a straight copy
 * is the truthful thumbnail, no protocol round trip needed. */
/* The SOURCE is opened first, and the destination only if that worked.
 *
 * Opening both up front truncates the destination whenever the source is
 * missing - so a copy that cannot happen destroys the file it was going to
 * replace. Harmless while the only caller copied a preview a pause had just
 * written; not harmless once the Auto card is written this way, where the
 * thing being overwritten is the last good picture of a save. */
static void copy_file(const char *from, const char *to)
{
	FILE *a, *b;
	char buf[16384];
	size_t n;

	if (!from || !*from || !to || !*to) return;
	if (!(a = fopen(from, "rb"))) return;
	if (!(b = fopen(to, "wb"))) { fclose(a); return; }

	while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
	fclose(a);
	fclose(b);
}

/* The slot carousel: Auto plus the manual slots for Load, the manual slots for
 * Save. One slot at a
 * time, large - the paused frame at a size you can actually read - with the
 * save's own timestamp under it and a dot rail for where you are. Left and
 * right cycle; Load skips slots with nothing behind them, Save cannot aim at
 * Auto, which belongs to the exit funnel alone. */
typedef struct {
	SDL_Texture *thumb[GM_SLOTS + 1];   /* [0]=Auto, [1..GM_SLOTS] */
	int have[GM_SLOTS + 1];
	char when[GM_SLOTS + 1][40];
	/* Every slot of one game holds the same machine's frame, so one aspect
	 * describes them all and the picture can be framed exactly rather than
	 * dropped into a fixed box with bars down its sides. 4:3 until a slot
	 * with a picture in it says otherwise. */
	float aspect;
	int saving;
} slot_view;

/* Drawing only, so one frame of it can be rendered by --shot without a game
 * running or a device in hand. */
static void slot_draw(app *a, const slot_view *sv, int sel)
{
	/* `area` is the layout slot the picture is fitted into; nothing is ever
	 * drawn to it. The border is the picture's own edge in the system's
	 * color - a 240x160 GBA frame and a 256x224 NES frame are different
	 * shapes, and neither should be padded out into the same rectangle. */
	const SDL_Rect area = { (TORTOS_SCREEN_W - 700) / 2, 129, 700, 451 };
	const int bw = 12;
	SDL_Rect img = area, frame;
	char slotname[16];
	int line_menu = ui_font_line(UI_F_MENU), line_meta = ui_font_line(UI_F_META);
	int i;

	/* The heading sits close to the top edge so the picture gets the middle of
	 * the screen. Everything below hangs off the IMAGE rather than off `area`,
	 * so a frame shorter than the layout slot pulls its own caption up with it
	 * instead of leaving a gap.
	 *
	 * The margins are mirrored: the heading's top sits as far from the top of
	 * the screen as the marker rail's bottom sits from the bottom of it, and
	 * the heading is centered in the gap above the frame. That fixes every
	 * number here to one another rather than to taste, so changing the image
	 * size moves the rest to match instead of drifting into something. */
	ui_text(a->r, ui_font(UI_F_LABEL), sv->saving ? "Save to" : "Load from",
	        TORTOS_SCREEN_W / 2, 39, 0, UI_TEXT_DIM);

	if ((float)area.w / sv->aspect <= (float)area.h) {
		img.w = area.w;
		img.h = (int)(area.w / sv->aspect + 0.5f);
	} else {
		img.h = area.h;
		img.w = (int)(area.h * sv->aspect + 0.5f);
	}
	img.x = area.x + (area.w - img.w) / 2;
	img.y = area.y + (area.h - img.h) / 2;
	frame = (SDL_Rect){ img.x - bw, img.y - bw, img.w + bw * 2, img.h + bw * 2 };

	ui_glow(a->r, &img, a->tint, 85, 1.35f);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	/* Opaque, matching the menu panel's border. At alpha 150 the frame let the
	 * background through and read as a darker accent than the system's own. The
	 * thumbnail itself is drawn at full brightness below - it never was
	 * darkened, only the frame around it.
	 *
	 * Rounded outside, square inside: the corner radius belongs to the chrome,
	 * and rounding the image would mean clipping the game's own pixels. */
	ui_round_rect(a->r, &frame, bw, (SDL_Color){
		(Uint8)(a->tint >> 16), (Uint8)(a->tint >> 8), (Uint8)a->tint, 255 });
	if (sv->thumb[sel]) {
		SDL_SetTextureColorMod(sv->thumb[sel], 255, 255, 255);
		SDL_RenderCopy(a->r, sv->thumb[sel], NULL, &img);
	} else {
		/* An empty slot is the same frame with nothing in it, so the strip
		 * does not change shape as you cycle past one. */
		SDL_SetRenderDrawColor(a->r, 12, 13, 18, 238);
		SDL_RenderFillRect(a->r, &img);
		ui_text(a->r, ui_font(UI_F_MENU), "Empty", img.x + img.w / 2,
		        img.y + (img.h - line_menu) / 2, 0, UI_TEXT_DIM);
	}

	if (sel == 0) snprintf(slotname, sizeof slotname, "Auto");
	else          snprintf(slotname, sizeof slotname, "Slot %d", sel);
	ui_text(a->r, ui_font(UI_F_MENU), slotname,
	        TORTOS_SCREEN_W / 2, img.y + img.h + 36, 0, UI_TEXT);
	ui_text(a->r, ui_font(UI_F_META),
	        sv->have[sel] ? sv->when[sel] : (sv->saving ? "\xE2\x80\x94" : ""),
	        TORTOS_SCREEN_W / 2, img.y + img.h + 42 + line_menu, 0, UI_TEXT_DIM);

	/* The dot rail: where you are among the slots, without showing every
	 * picture.
	 * A hollow-dim dot is a slot you cannot land on. */
	{
		int dots = GM_SLOTS + 1, dw = 36;
		int x0 = (TORTOS_SCREEN_W - dots * dw) / 2 + dw / 2;
		int y  = img.y + img.h + 62 + line_menu + line_meta;

		/* One size for every marker. Sizing the selected one larger meant the
		 * rail changed shape as you cycled, and color already says which slot
		 * you are on - two signals for one fact, one of them moving. */
		for (i = 0; i < dots; i++) {
			int can = sv->saving ? i >= 1 : sv->have[i];
			int r2  = 12;
			SDL_Rect d = { x0 + i * dw - r2, y - r2, r2 * 2, r2 * 2 };
			SDL_Color c = i == sel
				? (SDL_Color){ (Uint8)(a->tint >> 16), (Uint8)(a->tint >> 8),
				               (Uint8)a->tint, 255 }
				: (SDL_Color){ 90, 94, 110, can ? 255 : 90 };
			ui_round_rect(a->r, &d, r2 / 2, c);
		}
	}
}

/* Returns the chosen slot (SLOT_AUTO, or 1..GM_SLOTS) or 0 for backed out. */
static int slot_strip(app *a, SDL_Texture *bg, int saving)
{
	slot_view sv = { .aspect = 4.0f / 3.0f, .saving = saving };
	sysview *v = &a->view[a->sys_cursor];
	game_entry *g = &v->list.items[v->cursor];
	/* The game's own system, not the shelf: from Favorites the shelf is
	 * Favorites, which has no folder, and the slots came out loose in .tortos/
	 * - a second set the system's shelf never saw. Reported 2026-09-24. */
	int o = shelf_owner(a, a->sys_cursor, v->cursor);
	int i, sel = -1, chosen = 0, done = 0;
	bool   have_b[GM_SLOTS + 1] = { false };
	char pth[LIB_PATH * 2];

	for (i = 0; i <= GM_SLOTS; i++) {
		int slot = gm_slot_at(i);
		struct stat st;

		slot_state_path(a, o, g, slot, pth, sizeof pth);
		sv.have[i] = (stat(pth, &st) == 0 && st.st_size > 0);
		if (sv.have[i]) {
			have_b[i] = true;
			/* The state's own mtime: when this moment was captured. The
			 * device clock is only as good as the device clock, and showing
			 * what the filesystem says beats pretending to know better. */
			struct tm *tm = localtime(&st.st_mtime);
			if (tm) {
				/* "Aug 23 9:21:05 AM". Built from the fields rather than with
				 * strftime's "%-I", which drops the leading zero but is a GNU
				 * extension and does nothing on the BSD libc the host build
				 * links against - it would have read right on the device and
				 * wrong in every screenshot. The month still comes from
				 * strftime so it stays whatever the locale calls it. */
				char mon[8];
				int h12 = tm->tm_hour % 12;
				strftime(mon, sizeof mon, "%b", tm);
				if (!h12) h12 = 12;
				snprintf(sv.when[i], sizeof sv.when[i], "%s %d %d:%02d:%02d %s",
				         mon, tm->tm_mday, h12, tm->tm_min, tm->tm_sec,
				         tm->tm_hour < 12 ? "AM" : "PM");
			}

			slot_preview_path(a, o, g, slot, pth, sizeof pth);
			SDL_Surface *sf = IMG_Load(pth);
			if (sf) {
				sv.thumb[i] = SDL_CreateTextureFromSurface(a->r, sf);
				SDL_FreeSurface(sf);
			}
		}
		/* Loading starts on the newest thing there is to load, which is
		 * almost always Auto. Saving is decided after the loop, below,
		 * because it needs to know about every slot before it can choose. */
		if (sel < 0 && !saving && sv.have[i]) sel = i;
	}

	/* Save lands on the first empty slot, or the last one when there is none -
	 * the rule and its reasoning are in game_menu.h, where a check can reach
	 * them. */
	if (saving) sel = gm_save_slot(have_b);

	for (i = 0; i <= GM_SLOTS; i++) {
		int tw = 0, th = 0;
		if (!sv.thumb[i]) continue;
		SDL_QueryTexture(sv.thumb[i], NULL, NULL, &tw, &th);
		if (tw > 0 && th > 0) { sv.aspect = (float)tw / (float)th; break; }
	}
	if (sel < 0) sel = saving ? 1 : -1;
	if (sel < 0) done = -1;                     /* nothing to load at all */

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	while (!done && !want_quit) {
		plat_input_poll(&a->in);

		if (in_repeat(&a->in, IN_LEFT) || in_repeat(&a->in, IN_RIGHT)) {
			int dir = in_repeat(&a->in, IN_RIGHT) ? 1 : -1, next = sel;
			do {
				next = (next + dir + GM_SLOTS + 1) % (GM_SLOTS + 1);
			} while ((saving ? next == 0 : !sv.have[next]) && next != sel);
			sel = next;
		}
		if (menu_leaving(a)) done = -1;
		/* SLOT_AUTO, not 9. Commit 4208619 renamed the resume slot from
		 * MinUI's number to `auto` and replaced it everywhere the constant
		 * was used - but here the old number was written out rather than
		 * referenced, so it survived the rename. Loading Auto from the menu
		 * has asked for a `.9.state` that has not existed since 2026-08-27,
		 * and every one of those was a state_rejected. */
		if (a->in.pressed[IN_ACCEPT]) { chosen = gm_slot_at(sel); done = 1; }
		/* This screen used to ignore the power button outright - the one
		 * screen in the launcher that did. Stop the game and close with
		 * nothing chosen; game_menu sees the flag and closes behind us. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = -1;
		}

		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
		SDL_RenderClear(a->r);
		draw_paused_frame(a, bg);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 185);
		SDL_RenderFillRect(a->r, NULL);

		slot_draw(a, &sv, sel);
		plat_present();
		SDL_Delay(8);
	}

	for (i = 0; i <= GM_SLOTS; i++)
		if (sv.thumb[i]) SDL_DestroyTexture(sv.thumb[i]);
	return done == 1 ? chosen : 0;
}

/* What the in-game menu needs from the app, handed to gm_rows to become rows.
 * Same split as everywhere else. */
static int gm_build(app *a, menu_row *out, gm_bufs *b, int shots, const char *shot_said)
{
	gm_ui u;

	u.shots = shots;
	u.shot_said = shot_said;

	{
		/* The OWNER's look, so a game played from Favorites is set for the
		 * console it belongs to. */
		static char ovl[256];
		sysview *sv = &a->view[a->sys_cursor];
		int o = shelf_owner(a, a->sys_cursor, sv->cursor);

		u.shader  = look_console_of(a->sys.systems[o].tag) == LOOK_NONE ? NULL
		          : a->view[o].look.on ? "On" : "Off";
		u.overlay = overlay_label(a, o, ovl, sizeof ovl);
		u.dmode   = DMODES[a->view[o].dmode].label;
		u.no_overlays = !overlay_count(a, o);
	}
	u.earned = chv_earned();
	u.total  = chv_count();
	return gm_rows(&u, out, b);
}

/* The list itself, over the paused frame. menu_draw already windows a list
 * longer than the screen, which a set of 166 certainly is.
 *
 * Earned rows are drawn live and unearned quiet - the same distinction
 * menu_draw makes for a placeholder, and it reads correctly here: what you
 * have is bright, what is still ahead of you is not. */
/* 255 characters at ~30 to a line is nine, and a line of capitals is half
 * that. Twelve is past the worst real description with room over; the card
 * scrolls when the lines outrun the panel, so depth costs height rather
 * than text. */
#define CHV_WRAP_LINES 12
#define CHV_WRAP_COLS  96

/* Break `src` into lines no wider than `w`, at whitespace. Returns how many.
 *
 * menu_draw marquees a note that does not fit, which is right for a footer
 * carrying four facts and wrong for a sentence: the reader has to wait for the
 * text to come back around to re-read a clause. Three still lines beat one
 * moving one, so a description is wrapped here rather than handed over long.
 *
 * A word wider than the panel is cut instead of hunted for a space, because
 * the alternative is a line that overflows and there is nowhere else for it to
 * go. Measuring per prefix is quadratic in the line, which nobody will notice
 * at 191 characters, once, when the card opens.
 *
 * LINE BREAKS IN THE SOURCE ARE PARAGRAPHS, and this used to walk straight past
 * them: only ' ' broke a line and only ' ' was skipped, so a '\n' rode into the
 * middle of a line and SDL_ttf drew it as nothing - a hole two characters wide
 * after "Gunstar-9!!" with the next paragraph running on behind it. Of the 78
 * ScreenScraper synopses on this card 47 carry a break and 42 carry a blank
 * line; splitting at every break gives 191 pieces with a median of 276
 * characters, which is paragraphs rather than a source that hard-wraps. So any
 * run of whitespace containing a break ends the paragraph and gets one empty
 * row, however many blank lines the run actually held. Never at the top, where
 * it would only push the first line down off the rule. */
static int wrap_text(TTF_Font *f, const char *src, int w,
                     char out[][CHV_WRAP_COLS], int max)
{
	int n = 0;

	while (n < max) {
		char buf[CHV_WRAP_COLS];
		int take = 0, i;
		bool para = false;

		while (*src == ' ' || *src == '\n' || *src == '\r' || *src == '\t') {
			if (*src == '\n' || *src == '\r') para = true;
			src++;
		}
		if (!*src) break;
		if (para && n > 0) {
			out[n++][0] = '\0';
			if (n >= max) break;
		}
		for (i = 1; src[i - 1] && src[i - 1] != '\n' && src[i - 1] != '\r' &&
		            src[i - 1] != '\t'; i++) {
			if (i >= (int)sizeof buf) break;
			memcpy(buf, src, (size_t)i);
			buf[i] = '\0';
			if (ui_text_width(f, buf) > w) break;
			if (src[i] == ' ' || src[i] == '\n' || src[i] == '\r' ||
			    src[i] == '\t' || src[i] == '\0') take = i;
		}
		if (!take) {                    /* one word longer than the panel */
			take = i > 1 ? i - 1 : 1;
			if (take >= (int)sizeof buf) take = sizeof buf - 1;
		}
		memcpy(out[n], src, (size_t)take);
		out[n][take] = '\0';
		n++;
		src += take;
	}
	return n;
}

/* One achievement, opened with A from the list.
 *
 * The list has room for a title and a number, and a title is a NAME RATHER
 * THAN AN INSTRUCTION - it says which achievement, not what to do for it. The
 * description that says has been in memory the whole time, parsed by chv_add
 * beside the title and shown nowhere.
 *
 * Drawn with sel = -1, so nothing highlights. This is a card rather than a
 * list: there is nothing here to choose between, and a cursor would invite
 * pressing A on a row that does not answer.
 *
 * Returns true if the caller should close too, which is what MENU and power
 * mean anywhere else in the launcher. */
/* What the two achievement screens sit on.
 *
 * In a game that is the paused frame under a heavy dim, which is what makes a
 * menu read as being over something that has stopped. On the shelf it is the
 * shelf under the SAME lighter dim the game details screen uses, so opening
 * Cheevos from that screen does not darken the room on the way. */
static void chv_backdrop(app *a, SDL_Texture *bg, bool over_shelf)
{
	if (over_shelf) {
		draw_shelf(a);
	} else {
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
		SDL_RenderClear(a->r);
		draw_paused_frame(a, bg);
	}
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, over_shelf ? 120 : 185);
	SDL_RenderFillRect(a->r, NULL);
}

static bool cheevo_detail_screen(app *a, SDL_Texture *bg, bool over_shelf,
                                 const cheevo *c)
{
	char lines[CHV_WRAP_LINES][CHV_WRAP_COLS];
	menu_row rows[CHV_WRAP_LINES + 3];
	unsigned vcols[CHV_WRAP_LINES + 3] = { 0 };
	char pts[24];
	bool got = c->earned || c->earned_now;
	int fixed = menu_std_width(a);
	int n = 0, nl, i, done = 0;
	bool close_all = false;

	nl = wrap_text(ui_font(UI_F_MENU),
	               c->desc[0] ? c->desc : "The set carries no description.",
	               fixed, lines, CHV_WRAP_LINES);
	/* Live, unlike an ordinary note. A caption under a setting is a caption;
	 * here the description IS the content, and UI_TEXT_DIM is too quiet to be
	 * the only thing on the card worth reading. */
	for (i = 0; i < nl; i++)
		rows[n++] = (menu_row){ lines[i], MENU_NOTE_MARK, true };
	rows[n++] = MENU_RULE;
	snprintf(pts, sizeof pts, "%d", c->points);
	rows[n++] = (menu_row){ "Points", pts, true };
	rows[n]   = (menu_row){ "Status", got ? "Earned" : "Not earned", true };
	vcols[n]  = got ? UI_EARNED_RGB : 0u;
	n++;

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	while (!done && !want_quit) {
		plat_input_poll(&a->in);

		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_ACCEPT]) done = 1;
		if (a->in.pressed[IN_MENU]) { done = 1; close_all = true; g_menu_closing = true; }
		/* Only over the shelf. Over a game this card is the in-game menu's,
		 * and Muse would need that menu's power rule, which this screen does
		 * not have to give it. SELECT from the in-game menu itself works. */
		if (over_shelf && a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);
		/* Same reasoning as the list below: this screen is not inside
		 * plat_resident_wait, so nothing else is watching power for it. Over
		 * the shelf there is no game to stop, and stopping one is not what
		 * power means there. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			if (over_shelf) { power_off(a); break; }
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = 1;
			close_all = true;
		}

		chv_backdrop(a, bg, over_shelf);
		menu_draw_ex(a, c->title, rows, n, -1, fixed, a->tint, vcols, false, 0);
		plat_present();
		SDL_Delay(8);
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return close_all;
}

/* The synopsis, opened with A from the game info screen.
 *
 * The same shape as the achievement card above and for the same reason: there
 * is nothing here to choose between, so it is drawn with no cursor and scrolls
 * itself. A synopsis averages 670 characters and the longest on the test card
 * is 2,221, so most of them fit and the long ones walk.
 *
 * Over the shelf rather than a paused frame: this is reached from the shelf,
 * where the info screen already dims the same way. */
#define SYN_WRAP_LINES 80
/* Room for the text twice over, plus the four blanks and the rule between the
 * copies. The rows are the wrapped lines POINTED AT twice, not wrapped twice,
 * so the line buffer stays SYN_WRAP_LINES - this is the only array that has to
 * grow, and 165 menu_rows is four kilobytes. */
#define SYN_ROWS (SYN_WRAP_LINES * 2 + 5)

/* The rows a synopsis is drawn as, and where they start repeating.
 *
 * Shared by the screen and by --synopsis, which used to build its own: the
 * harness then drew a card the device does not draw, which is the one thing a
 * harness must never do. Returns the row count; `rows` must hold SYN_ROWS. */
static int syn_layout(const char *text, int fixed,
                      char (*lines)[CHV_WRAP_COLS], menu_row *rows,
                      int *loop_at)
{
	int n, i, k;

	*loop_at = 0;
	n = wrap_text(ui_font(UI_F_MENU), text, fixed, lines, SYN_WRAP_LINES);
	for (i = 0; i < n; i++)
		rows[i] = (menu_row){ lines[i], MENU_BODY_MARK, true };

	/* A SYNOPSIS TOO TALL FOR THE PANEL IS LAID OUT TWICE, with two blank
	 * lines, a rule and two blank lines between the copies, and menu_draw is
	 * told where the repeat starts.
	 *
	 * Both spacings were looked at on the device 2026-09-17. The rule carries
	 * air of its own - it is the bar the heading uses, with the heading's gap
	 * under it - and on its own it reads as a line ruled THROUGH the prose,
	 * with the last sentence and the first pressed against it. The blank lines
	 * are what make it the end of something.
	 *
	 * That is what turns the scroll from a ping-pong into a scroll-through.
	 * Running backwards is fine for a row of text too wide for its column,
	 * where the rewind is obviously a rewind; over fifteen lines of prose it
	 * means reading the end in reverse to get back to the beginning. Coming
	 * round instead needs somewhere to come round FROM, which is the rule: it
	 * says the text ended and is starting again, rather than leaving a reader
	 * to work out that the sentence they are on is one they have read.
	 *
	 * Only when it does not fit. A short synopsis sits still, and doubling it
	 * would make a page that fits scroll for no reason. */
	if (n <= menu_notes_fit()) return n;

	k = n;
	rows[k++] = MENU_BODY("");
	rows[k++] = MENU_BODY("");
	rows[k++] = MENU_HR;
	rows[k++] = MENU_BODY("");
	rows[k++] = MENU_BODY("");
	*loop_at = k;
	for (i = 0; i < n && k < SYN_ROWS; i++)
		rows[k++] = (menu_row){ lines[i], MENU_BODY_MARK, true };
	return k;
}

/* ---- Muse: covers --------------------------------------------------------- */

static bool file_nonempty(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && st.st_size > 0;
}

/* Album `al`'s cover, when there is one on the card: true, with where in
 * `out`.
 *
 * The first time an album is asked about, a cover already on the card - from
 * an earlier visit - is found by looking. Otherwise the daemon is asked for the
 * one the music carries, and this answers false until cover_answers hears
 * back, a few milliseconds later. */
static bool cover_file(int al, char *out, size_t n)
{
	char base[LIB_PATH * 2];
	cover_state *c;
	unsigned now = plat_now_ms();

	if (!g_cov || al < 0 || al >= g_muse.nalbums) return false;
	c = &g_cov[al];
	ml_cover_base(g_muse_root, &g_muse, al, base, sizeof base);
	if (!base[0]) return false;
	/* Singles' is its own, the one put there for it or one put in its place,
	 * never a song's: its songs have nothing to do with one another, and the
	 * first one's picture became the album's (2026-10-05). So it is looked
	 * for each time and Muse is never asked. */
	if (g_muse.albums[al].singles) {
		snprintf(out, n, "%s.jpg", base);
		if (file_nonempty(out)) return true;
		snprintf(out, n, "%s.png", base);
		return file_nonempty(out);
	}
	if (c->st == COV_JPG || c->st == COV_PNG) {
		snprintf(out, n, "%s.%s", base, c->st == COV_JPG ? "jpg" : "png");
		return true;
	}
	if (c->st == COV_FOLDER) return ml_folder_image(g_muse_root, &g_muse, al, out, n);
	if (c->st == COV_NONE) return false;
	if (c->st == COV_ASKED && now - c->asked_ms < 5000) return false;

	snprintf(out, n, "%s.jpg", base);
	if (file_nonempty(out)) { c->st = COV_JPG; return true; }
	snprintf(out, n, "%s.png", base);
	if (file_nonempty(out)) { c->st = COV_PNG; return true; }
	if (musec_cover_ask(g_muse.tracks[g_muse.albums[al].first].path, base)) {
		c->st = COV_ASKED;
		c->asked_ms = now;
	}
	return false;
}

/* What is known about album `al`'s cover, forgotten when the file it names is
 * no longer on the card, so cover_file looks again and asks Muse for the one
 * the music carries. The transfer screen's: files there come and go under the
 * launcher, and a cover moved off the card stayed "a JPG" until a restart, so
 * its embedded picture was never copied out again (2026-10-05). */
static void cover_recheck(int al)
{
	char base[LIB_PATH * 2], p[LIB_PATH * 2 + 8];
	cover_state *c;

	if (!g_cov || al < 0 || al >= g_muse.nalbums) return;
	c = &g_cov[al];
	if (c->st == COV_JPG || c->st == COV_PNG) {
		ml_cover_base(g_muse_root, &g_muse, al, base, sizeof base);
		snprintf(p, sizeof p, "%s.%s", base, c->st == COV_JPG ? "jpg" : "png");
		if (!file_nonempty(p)) c->st = COV_UNKNOWN;
	} else if (c->st == COV_FOLDER &&
	           !ml_folder_image(g_muse_root, &g_muse, al, p, sizeof p)) {
		c->st = COV_UNKNOWN;
	}
}

/* What the daemon said about the covers it was asked for. Matched by path, not
 * by album number, so an answer that lands after a rescan finds the album it
 * was about or none at all. True when anything landed, so a shelf that only
 * draws when something changes knows it has. */
static bool cover_answers(void)
{
	char base[LIB_PATH * 2], file[LIB_PATH * 2 + 8], b[LIB_PATH * 2];
	bool landed = false;
	size_t fl;
	int i;

	while (musec_cover_take(base, sizeof base, file, sizeof file)) {
		landed = true;
		fl = strlen(file);
		for (i = 0; g_cov && i < g_muse.nalbums; i++) {
			char pic[LIB_PATH * 2 + 8];

			ml_cover_base(g_muse_root, &g_muse, i, b, sizeof b);
			if (strcmp(b, base)) continue;
			/* Nothing in the files: a picture in the album's folder, which
			 * is where a book's cover usually is, before the card. */
			g_cov[i].st = fl > 0 ? (fl > 4 && !strcmp(file + fl - 4, ".png") ? COV_PNG : COV_JPG)
			            : ml_folder_image(g_muse_root, &g_muse, i, pic, sizeof pic)
			            ? COV_FOLDER : COV_NONE;
			break;
		}
	}
	return landed;
}

/* A cover's shape, wherever it is drawn: square, and square-cornered - Eric's
 * call, 2026-09-18; a sleeve is a square. An ARGB8888 surface, replaced when
 * it had to be cropped.
 *
 * Square by taking the middle of it. Covers are square nearly always, and the
 * ones that are not are a scan with a few pixels of scanner either side - the
 * first card has one, 245 by 225 - which cropping loses and stretching would
 * put into the picture. */
static void cover_shape(SDL_Surface **ps)
{
	SDL_Surface *s = *ps, *sq;
	int side = s->w < s->h ? s->w : s->h;

	if (s->w != s->h &&
	    (sq = SDL_CreateRGBSurfaceWithFormat(0, side, side, 32,
	                                         SDL_PIXELFORMAT_ARGB8888))) {
		SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
		SDL_BlitSurface(s, &(SDL_Rect){ (s->w - side) / 2, (s->h - side) / 2,
		                                side, side }, sq, NULL);
		SDL_FreeSurface(s);
		*ps = sq;
	}
}

/* A cover as Now Playing draws it. NULL when the file will not decode. */
static SDL_Texture *load_cover(SDL_Renderer *r, const char *path)
{
	SDL_Surface *raw = IMG_Load(path), *s;
	SDL_Texture *t;

	if (!raw) return NULL;
	s = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	if (!s) return NULL;
	cover_shape(&s);
	t = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	return t;
}

static void np_forget(void)
{
	if (g_np.tex) SDL_DestroyTexture(g_np.tex);
	g_np.tex = NULL;
	g_np.album = -1;
	g_np.done = false;
}

/* Bring the Now Playing cover up to date with album `al`, which may be -1 for
 * nothing. Called every frame; it costs a comparison once the cover is in. */
static void np_cover(app *a, int al)
{
	char path[LIB_PATH * 2 + 8];
	int w, h;

	if (al != g_np.album) { np_forget(); g_np.album = al; }
	if (g_np.done || al < 0) return;
	if (cover_file(al, path, sizeof path)) {
		unsigned t0 = plat_now_ms();

		g_np.tex = load_cover(a->r, path);
		fprintf(stderr, "muse: cover for %s in %u ms\n", g_muse.albums[al].name,
		        plat_now_ms() - t0);
		/* There, and not a picture: the card, as for an album with none. */
		if (!g_np.tex) g_cov[al].st = COV_NONE;
	}
	if (!g_np.tex && g_cov[al].st == COV_NONE) {
		g_np.tex = ui_make_cover(a->r, g_muse.albums[al].name, MUSE_ACCENT, &w, &h);
	}
	g_np.done = g_np.tex != NULL;
}

/* Card `i` on Muse's shelf - game_get_tex's half for Muse. Its album's cover
 * when the card holds one, decoded on the worker like any box art; the
 * generated card when the music carries none; nothing yet while the daemon is
 * being asked, which cover_answers settles a few milliseconds later. */
static void muse_card_want(app *a, int s, int i)
{
	sysview *v = &a->view[s];
	char path[LIB_PATH * 2 + 8];
	int al;

	if (!v->album || i < 0 || i >= v->list.count) return;
	al = v->album[i];
	if (al < 0 || al >= g_muse.nalbums) return;
	if (cover_file(al, path, sizeof path)) {
		if (texload_want(s, i, path, NULL)) return;
		/* No worker - a shot - so here, as game_get_tex does without one. */
		v->tex[i] = load_cover(a->r, path);
		if (v->tex[i]) {
			SDL_QueryTexture(v->tex[i], NULL, NULL, &v->tw[i], &v->th[i]);
			v->cb[i] = 1.0f;
			return;
		}
		g_cov[al].st = COV_NONE;             /* there, and not a picture */
	}
	if (g_cov && g_cov[al].st == COV_NONE) {
		v->tex[i] = ui_make_cover(a->r, v->list.items[i].title, MUSE_ACCENT,
		                          &v->tw[i], &v->th[i]);
		v->cb[i] = 1.0f;
	}
}

/* ---- Muse: the play mode --------------------------------------------------- */

/* Y ON NOW PLAYING, AND NOWHERE ELSE IN MUSE. Eric's call, 2026-09-19.
 *
 * Y used to cycle the mode on every Muse screen, and because a mark on its own
 * is a new shape to learn, each press named the mode on a pill in the middle
 * of the screen for 1400 ms. On Now Playing that pill said what the mark
 * beside the track count was already saying, a moment later and larger.
 *
 * The mode is now set where it is shown. That leaves the shelf and the lists
 * with nothing to announce, so the pill is gone with them, and Y is free there
 * the way X is. The cost is that the mode cannot be changed before something
 * plays, since Now Playing is a screen about a track - and it is kept across
 * restarts, so it is not a thing anyone sets often. */
static void muse_cycle_mode(void)
{
	muq_mode m = (muq_mode)((musec_mode() + 1) % MUQ_MODES);

	musec_set_mode(m);
	db_set_str(db_dev(), "muse.mode", MUSE_MODES[m].key);
}

/* ---- Muse: what it is open over ------------------------------------------ */

/* NULL is the shelf. The in-game menu hands over its style for as long as Muse
 * is up, because a game is paused underneath: the list is drawn over that
 * game's frame rather than a shelf it is not on, and power stops the game
 * rather than the device - which is what that menu's power rule does, and why
 * it has one. Muse is modal and never opens itself, so one of each is enough. */
static const menu_style *g_muse_over;
static void             *g_muse_over_ctx;
static bool              g_muse_gone;    /* that rule ended what was underneath */

static void muse_backdrop(app *a)
{
	draw_shelf(a);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
	SDL_RenderFillRect(a->r, NULL);
}

/* The power button or Auto Off, inside Muse. It closes Muse either way; what
 * else it does is up to whatever Muse is open over. */
static void muse_power(app *a)
{
	if (g_muse_over && g_muse_over->on_power) {
		if (g_muse_over->on_power(a, g_muse_over_ctx) == MENU_DONE)
			g_muse_gone = true;
		return;
	}
	power_off(a);
}

/* ---- Muse: Now Playing ---------------------------------------------------- */

/* How far one step of left or right seeks: ten seconds a tap, and further the
 * longer it is held - a minute a step after a second, five after three.
 * Eric's, 2026-09-29, for books: at ten seconds a step, eleven steps a second,
 * crossing a seventeen-hour book took nine minutes of holding the d-pad, and
 * now takes about twenty seconds. A song is over long before the steps grow,
 * so music is as it was. */
static double seek_step(const in_state *in, in_button b)
{
	Uint32 held = SDL_GetTicks() - in->down_since[b];

	if (in->pressed[b] || held < 1000) return 10;
	return held < 3000 ? 60 : 300;
}

static void mmss(char *out, size_t n, double sec)
{
	int t = sec > 0 ? (int)(sec + 0.5) : 0;

	/* Hours past the hour: a seventeen-hour book read "-1014:46" in minutes. */
	if (t >= 3600) snprintf(out, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
	else           snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

/* The cover where the shelf puts the card it is looking at, in the same kind
 * of light, and the words beside it. Its own screen and not a panel over the
 * shelf: it is the one screen left open for as long as an album lasts, and
 * what is behind a panel is a picture of games. */
#define NP_SIDE 432                              /* the cover, on screen */
#define NP_X    80
#define NP_Y    ((TORTOS_SCREEN_H - NP_SIDE) / 2 - 28)
#define NP_TX   (NP_X + NP_SIDE + 56)            /* the words beside it */
#define NP_TW   (TORTOS_SCREEN_W - NP_TX - 64)

/* A chapter as Now Playing names it: its own title when it has one worth
 * reading, "Chapter 3" when the title is only its number - Dungeon Crawler
 * Carl's thirty-seven are "001" to "037" - or missing. */
static void chapter_label(int i, char *out, size_t n)
{
	const char *t = musec_chapter_title(i);
	const char *p;

	for (p = t; *p && (isdigit((unsigned char)*p) || *p == ' '); p++) { }
	if (t[0] && *p) snprintf(out, n, "%s", t);
	else            snprintf(out, n, "Chapter %d", i + 1);
}

/* One line of the words, sliding when it is wider than the column. */
static void np_line(SDL_Renderer *r, TTF_Font *f, const char *s, int y,
                    unsigned phase, SDL_Color col, unsigned *wait)
{
	unsigned w = ui_pingpong_wait(ui_text_width(f, s) - NP_TW, phase);

	ui_text_marquee(r, f, s, NP_TX, y, NP_TW, phase, col);
	if (w < *wait) *wait = w;
}

/* Returns how long until the picture would change by itself, in ms - 0 while
 * a line is sliding - so the loop can leave the screen alone until then. */
static unsigned np_draw(app *a, const mu_now *mn, const char *next)
{
	SDL_Renderer *r = a->r;
	SDL_Rect cov = { NP_X, NP_Y, NP_SIDE, NP_SIDE };
	TTF_Font *fs = ui_font(UI_F_META);
	/* Muse's green for everything that is not the cover or the words - the
	 * light, the bar, the mode's mark - the same green its shelf card and its
	 * menus wear. It was the cover's own most vivid color once, which made one
	 * mark two colors a screen apart; Eric's call, 2026-09-19. */
	unsigned rgb = MUSE_ACCENT, wait = (unsigned)-1, phase;
	SDL_Color acc = { (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, 255 };
	const char *state = mn->state == MU_PLAYING ? NULL
	                  : mn->state == MU_PAUSED  ? "Paused" : "Stopped";
	int book = musec_is_book() ? ml_album_of(&g_muse, musec_track(mn->index)) : -1;
	double k = mn->len > 0 ? mn->at / mn->len : 0;
	char t0[16], t1[16], line[200], fit[200];
	int y, bar, fill, left, right;

	/* A book played to its end says so, rather than that it stopped. */
	if (mn->state == MU_STOPPED && book >= 0 && g_book_done && g_book_done[book])
		state = "Finished";

	SDL_SetRenderDrawColor(r, UI_BG_R, UI_BG_G, UI_BG_B, 255);
	SDL_RenderClear(r);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	/* The shelf's wash along the foot, and the focused card's glow around
	 * the cover. */
	ui_glow(r, &(SDL_Rect){ 0, TORTOS_SCREEN_H - 240, TORTOS_SCREEN_W, 480 },
	        rgb, 34, 1.7f);
	ui_glow(r, &cov, rgb, 90, 1.7f);
	if (g_np.tex) SDL_RenderCopy(r, g_np.tex, NULL, &cov);

	/* Title, artist and album slide together when they are too long, on one
	 * clock that starts over with each track. */
	phase = mq_phase(MQ_NP, mn->index, (int)strlen(mn->title));
	y = NP_Y + 36;
	{
		/* Where in the queue, and the mode it is playing in beside it. The
		 * count is the order being HEARD, so shuffled it still runs 1 to n.
		 *
		 * The mark half again the text's height, centered on it: at the
		 * text's own height the 1 in repeat one and shuffle's crossing were
		 * too small to read at arm's length. Eric, 2026-09-19.
		 *
		 * Centered on the ink, not the em box: the box keeps a descender's
		 * depth below the baseline that "1 of 12" never uses, so its middle
		 * sat the mark 3.5px low on the device. Cap height from 'H', as the
		 * Wi-Fi panel does. */
		int th = ui_font_height(UI_F_META), gs = th * 3 / 2, gx = NP_TX;
		/* No mark on a book: it plays in order, whatever the mode. */
		int g = musec_is_book() ? -1 : MUSE_MODES[musec_mode()].glyph;
		int asc = fs ? TTF_FontAscent(fs) : th, cap = asc;

		if (fs) {
			int mnx, mxx, mny, mxy, adv;

			if (TTF_GlyphMetrics(fs, 'H', &mnx, &mxx, &mny, &mxy, &adv) == 0)
				cap = mxy;
		}
		/* A book with chapters counts those instead of its one file. */
		if (musec_is_book() && musec_chapters() > 1) {
			snprintf(line, sizeof line, "Chapter %d of %d",
			         musec_chapter_now() + 1, musec_chapters());
			gx += ui_text(r, fs, line, NP_TX, y, -1, UI_TEXT_DIM) + 18;
		} else if (mn->count > 1) {
			snprintf(line, sizeof line, "%d of %d", mn->index + 1, mn->count);
			/* Not tabular: it changes with the track, not every second,
			 * and a 1 in a fixed-width cell read as "1 of  14". */
			gx += ui_text(r, fs, line, NP_TX, y, -1, UI_TEXT_DIM) + 18;
		}
		if (g >= 0) ui_glyph_draw(r, (ui_glyph)g, gx + gs / 2, y + asc - cap / 2, gs, acc);
		/* A book's speed where music's mode mark would be, and only when it
		 * is not the ordinary one. */
		if (musec_is_book() && fabs(musec_speed() - 1.0) > 0.01) {
			snprintf(line, sizeof line, "%gx", musec_speed());
			ui_text(r, fs, line, gx, y, -1, acc);
		}
	}
	/* Room under it for the larger mark and for the title to stand clear of
	 * the line above - which read as one block with it at 6px. */
	y += ui_font_line(UI_F_META) + 24;
	{
		/* A chapter with a title of its own is what is playing; one that
		 * is only a number leaves the book's title there. */
		char chap[128];
		const char *top = mn->title;

		if (musec_is_book() && musec_chapters() > 1) {
			chapter_label(musec_chapter_now() < 0 ? 0 : musec_chapter_now(), chap, sizeof chap);
			if (strncmp(chap, "Chapter ", 8) != 0) top = chap;
		}
		np_line(r, ui_font(UI_F_TITLE), top, y, phase, UI_TEXT, &wait);
	}
	y += ui_font_line(UI_F_TITLE) + 10;
	np_line(r, ui_font(UI_F_MENU), mn->artist, y, phase, UI_TEXT_SOFT, &wait);
	y += ui_font_line(UI_F_MENU);
	np_line(r, ui_font(UI_F_MENU), mn->album, y, phase, UI_TEXT_DIM, &wait);

	/* Where in the track. What is left rather than the whole length on the
	 * right, because the question is usually how long until the next one. */
	bar = NP_Y + NP_SIDE - 132;
	ui_round_rect(r, &(SDL_Rect){ NP_TX, bar, NP_TW, UI_BAR_H }, UI_BAR_H / 2,
	              (SDL_Color){ 255, 255, 255, 34 });
	fill = (int)(NP_TW * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5);
	if (fill > 0)
		ui_round_rect(r, &(SDL_Rect){ NP_TX, bar, fill, UI_BAR_H }, UI_BAR_H / 2,
		              state ? ui_fade(acc, 0.45f) : acc);
	/* Tabular, so the clock ticking does not move what sits beside it. */
	mmss(t0, sizeof t0, mn->at);
	left = NP_TX + ui_text_tabular(r, fs, t0, NP_TX, bar + 16, -1, UI_TEXT_SOFT);
	right = NP_TX + NP_TW;
	if (mn->len > 0) {
		mmss(t1, sizeof t1, mn->len > mn->at ? mn->len - mn->at : 0);
		snprintf(line, sizeof line, "-%s", t1);
		right -= ui_text_tabular(r, fs, line, NP_TX + NP_TW, bar + 16, 1, UI_TEXT_SOFT);
	}
	/* Between the two times rather than on the column's middle, which a book's
	 * hours pushed them into: "16:54:47Stopped". */
	if (state) ui_text(r, fs, state, (left + right) / 2, bar + 16, 0, acc);

	if (next && next[0]) {
		snprintf(line, sizeof line, "Next: %s", next);
		ui_fit_text(fs, line, fit, sizeof fit, NP_TW);
		ui_text(r, fs, fit, NP_TX, NP_Y + NP_SIDE - ui_font_height(UI_F_META), -1,
		        UI_TEXT_DIM);
	}

	ui_text(r, fs, musec_is_book()
	        ? (musec_chapters() > 1
	           ? (state ? "A: play    L1/R1: chapter    Left/Right: seek    Y: speed"
	                    : "A: pause    L1/R1: chapter    Left/Right: seek    Y: speed")
	           : (state ? "A: play    L1/R1: file    Left/Right: seek    Y: speed"
	                    : "A: pause    L1/R1: file    Left/Right: seek    Y: speed"))
	        : (state ? "A: play    L1/R1: track    Left/Right: seek    Y: mode"
	                 : "A: pause    L1/R1: track    Left/Right: seek    Y: mode"),
	        TORTOS_SCREEN_W / 2, TORTOS_SCREEN_H - 72, 0, UI_TEXT_DIM);
	{
		unsigned dot = draw_low_battery_chrome(r);
		if (dot < wait) wait = dot;
	}
	return wait;
}

/* ---- Muse: the pocket lock ------------------------------------------------
 *
 * #51, Eric's design, 2026-09-30. Held F1+F2 - the front function keys, L3/R3
 * in some other CFWs - for a second inside Muse while music plays, or MENU's
 * Lock Screen, puts the backlight out and ignores every button but volume,
 * power and the pair that unlocks. A headset's play, pause and skip still
 * work: musec_poll reads them, and it runs here as everywhere.
 *
 * Power stays power, by Eric's call - powering off is far more common than
 * pocketing a game handheld for music - so a press in a pocket turns the Brick
 * off, cleanly, with a book's place saved, as it would with the screen on.
 * Nothing lights the screen short of unlocking: anything that did would light
 * it in the pocket. Measured before building: a book playing drew about 470
 * mA with the backlight on and 300 with it off. */

/* A chord is armed once both keys have been let go, so the hold that unlocks
 * does not lock again the moment the screen is back, and one lock is one hold. */
static bool g_lock_armed = true;
static bool g_lock_asked;                /* MENU's Lock Screen, for muse_menu */

static void muse_ask_lock(void) { g_lock_asked = true; }

static bool lock_chord(const in_state *in)
{
	Uint32 later;

	if (!in->down[IN_BRIGHTUP] && !in->down[IN_BRIGHTDN]) g_lock_armed = true;
	if (!g_lock_armed || !in->down[IN_BRIGHTUP] || !in->down[IN_BRIGHTDN]) return false;
	later = in->down_since[IN_BRIGHTUP] > in->down_since[IN_BRIGHTDN]
	      ? in->down_since[IN_BRIGHTUP] : in->down_since[IN_BRIGHTDN];
	if (SDL_GetTicks() - later < 1000) return false;
	g_lock_armed = false;
	return true;
}

/* Whether a lock can happen here: music playing, and Muse not opened over a
 * paused game, whose power rule belongs to the game. */
static bool lock_allowed(void)
{
	return musec_playing() && !g_muse_over;
}

/* The volume and brightness keys on a Muse screen, and the chord. F1 and F2
 * are brightness, so pressing both nudges one down and one up, which cancels;
 * once both are held they stop repeating. True when the chord locked. */
static bool muse_keys(app *a)
{
	in_state *in = &a->in;

	if (in_repeat(in, IN_VOLUP)) plat_volume_nudge(+1);
	if (in_repeat(in, IN_VOLDN)) plat_volume_nudge(-1);
	if (in->down[IN_BRIGHTUP] && in->down[IN_BRIGHTDN]) {
		if (in->pressed[IN_BRIGHTUP]) plat_brightness_nudge(+1);
		if (in->pressed[IN_BRIGHTDN]) plat_brightness_nudge(-1);
		return lock_chord(in) && lock_allowed();
	}
	lock_chord(in);                      /* only to re-arm on release */
	if (in_repeat(in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
	if (in_repeat(in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
	return false;
}

/* Locked, until F1+F2 again, power, or Auto Off. Nothing is drawn: the panel
 * is dark and a present would be work for nobody. */
static void muse_locked(app *a)
{
	fprintf(stderr, "muse: locked\n");
	plat_backlight(false);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (!want_quit && a->running) {
		muse_screen_poll();
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		/* Power, or Auto Off once the music has stopped long enough. The
		 * light first, so the power-off animation is seen. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			plat_backlight(true);
			muse_power(a);
			break;
		}
		if (in_repeat(&a->in, IN_VOLUP)) plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN)) plat_volume_nudge(-1);
		if (lock_chord(&a->in)) break;
		SDL_Delay(30);
	}
	plat_backlight(true);
	fprintf(stderr, "muse: unlocked\n");
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* MENU, from any Muse screen: Muse's own menu, the one its shelf has always
 * had. One button, one meaning, wherever in Muse it is pressed - it used to
 * close Muse from Now Playing and the tracks, and open this on the shelf.
 * Eric's call, 2026-09-23.
 *
 * Rescan Folder inside it rebuilds the library, and every album index with it,
 * so a screen holding one cannot carry on: MUSE_REBUILT tells it to let go,
 * and the shelf finds itself again the way it does after its own MENU. */
static muse_exit muse_menu(app *a)
{
	unsigned gen = g_muse_gen;

	tortos_menu_for(a, SCREEN_GAMES);
	/* MENU inside it closed it, and only it: Muse is still up, and may itself
	 * be over a menu SELECT opened it from, which has to stay open. */
	g_menu_closing = false;
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	/* Lock Screen closed the menu to lock from the screen underneath. */
	if (g_lock_asked) {
		g_lock_asked = false;
		if (lock_allowed()) muse_locked(a);
	}
	if (want_quit || !a->running) return MUSE_CLOSE;
	return g_muse_gen != gen ? MUSE_REBUILT : MUSE_BACK;
}

/* MUSE_CLOSE for SELECT and the power button, which leave Muse altogether;
 * MUSE_BACK for B, which is the album's tracks underneath; MUSE_REBUILT when
 * MENU's Rescan Folder replaced the library. MENU otherwise comes back here. */
static muse_exit muse_now_screen(app *a)
{
	struct {
		mu_state st;
		int at, len, index, count, mode, chapter, speed;
		char title[128], artist[128], album[128];
		SDL_Texture *tex;
	} shown, drawn;
	unsigned due = 0;
	muse_exit how = MUSE_BACK;

	mq_reset(MQ_NP);
	memset(&drawn, 0, sizeof drawn);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	while (!want_quit && a->running) {
		const mu_now *mn;
		const char *nt;
		char next[128] = "";
		unsigned now;
		bool touched = false;
		int b;

		muse_screen_poll();
		cover_answers();
		mn = musec_now();
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; how = MUSE_CLOSE; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { muse_power(a); how = MUSE_CLOSE; break; }
		if (a->in.pressed[IN_BACK]) break;
		if (a->in.pressed[IN_SELECT]) { how = MUSE_CLOSE; break; }
		if (a->in.pressed[IN_MENU]) {
			how = muse_menu(a);
			if (how != MUSE_BACK) break;
			memset(&drawn, 0, sizeof drawn);     /* the menu drew over it */
			continue;
		}

		/* A, and only A. X did the same thing here and pausing in the list,
		 * which made one button mean two things a screen apart; it is left
		 * free in Muse for something that needs it. Eric's call, 2026-09-19. */
		if (a->in.pressed[IN_ACCEPT])       musec_toggle();
		/* The mode, on music; on a book, which plays in order whatever the
		 * mode says, its speed. */
		if (a->in.pressed[IN_Y]) {
			if (musec_is_book()) book_cycle_speed();
			else                 muse_cycle_mode();
		}
		if (in_repeat(&a->in, IN_L1))       musec_prev();
		if (in_repeat(&a->in, IN_R1))       musec_next();
		if (in_repeat(&a->in, IN_LEFT))     musec_seek_by(-seek_step(&a->in, IN_LEFT));
		if (in_repeat(&a->in, IN_RIGHT))    musec_seek_by(+seek_step(&a->in, IN_RIGHT));
		if (muse_keys(a)) {
			muse_locked(a);
			if (!a->running || g_muse_gone) { how = MUSE_CLOSE; break; }
			memset(&drawn, 0, sizeof drawn);   /* the screen is back: draw it */
			continue;
		}

		/* The queue's track, not musec_path: that goes blank when an album
		 * ends, and the screen should still show the album that just did. */
		np_cover(a, ml_album_of(&g_muse, musec_track(mn->index)));
		/* What the mode will actually play next. Repeat one says so with
		 * its mark, and "Next:" naming the same song reads as a mistake. */
		/* In a book with chapters, the next chapter, until the last. */
		if (musec_is_book() && musec_chapters() > 1 &&
		    musec_chapter_now() + 1 < musec_chapters())
			chapter_label(musec_chapter_now() + 1, next, sizeof next);
		else if ((musec_is_book() || musec_mode() != MUQ_REPEAT_ONE) && (nt = musec_upcoming()))
			ml_track_name(strrchr(nt, '/') ? strrchr(nt, '/') + 1 : nt,
			              next, sizeof next);

		/* Drawn when what it shows has changed, not every pass. This is the
		 * screen left open for the length of an album, and a present a frame
		 * for forty minutes of one still picture is battery spent on nothing.
		 * The clock is kept in whole seconds, which is all it shows. */
		memset(&shown, 0, sizeof shown);
		shown.st = mn->state;
		shown.at = (int)(mn->at + 0.5);
		shown.len = (int)(mn->len + 0.5);
		shown.index = mn->index;
		shown.count = mn->count;
		memcpy(shown.title, mn->title, sizeof shown.title);
		memcpy(shown.artist, mn->artist, sizeof shown.artist);
		memcpy(shown.album, mn->album, sizeof shown.album);
		shown.tex = g_np.tex;
		shown.mode = musec_is_book() ? -1 : (int)musec_mode();
		shown.chapter = musec_chapter_now();
		shown.speed = (int)(musec_speed() * 100 + 0.5);
		for (b = 0; b < IN_COUNT && !touched; b++)
			touched = a->in.pressed[b] || a->in.down[b];
		now = plat_now_ms();

		if (touched || memcmp(&shown, &drawn, sizeof shown) || (int)(now - due) >= 0) {
			unsigned wait = np_draw(a, mn, next);
			Uint32 osd;

			/* Asked after the draw, which is what retires a line whose
			 * time is up - asked before, it names a moment already past. */
			plat_draw_osd(a->r);
			osd = plat_osd_until();
			plat_present();
			drawn = shown;
			/* At least once a second whatever happens, which is also how
			 * often the clock changes. The volume line needs one more frame
			 * at the moment it is due to go. */
			due = now + (wait < 1000 ? wait : 1000);
			if (osd != UINT32_MAX && (int)(osd - due) < 0) due = osd;
			SDL_Delay(8);
		} else {
			SDL_Delay(16);
		}
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return how;
}

/* ---- Muse: an album's tracks ---------------------------------------------- */

/* The album the queue is on, and the row its track sits at. -1 when nothing
 * is loaded, or when what is loaded is not on the card any more. */
static int muse_playing_album(int *row)
{
	const char *t = musec_track(musec_now()->index);
	int al = ml_album_of(&g_muse, t), i;

	if (row) *row = 0;
	if (al < 0) return -1;
	if (row)
		for (i = 0; i < g_muse.albums[al].n; i++)
			if (!strcmp(g_muse.tracks[g_muse.albums[al].first + i].path, t))
				*row = i;
	return al;
}

/* One album's tracks, over the shelf its cover sits on.
 *
 * THIS USED TO BE THE WHOLE OF MUSE: artists, then their albums, then tracks,
 * with Now Playing as a row at the top, because the covers it browsed by had
 * no shelf of their own yet. They have one now - see muse_shelf_screen - and
 * SELECT lands there, so the two levels above this one had nothing left to
 * do. Eric's, 2026-09-20: the rest of the launcher is pictures, and a list
 * was the wrong front door for the one screen that is all covers.
 *
 * A plays a track and opens Now Playing; on the track already playing it just
 * opens Now Playing, rather than starting it over. B goes back to the shelf.
 * SELECT closes Muse from here, and MENU is Muse's menu, as everywhere in Muse.
 * L1 and R1 are the previous and next track, left and right seek ten seconds.
 * X and Y are free. Leaving does not stop the music - that is what the daemon
 * is for.
 *
 * `now` opens straight onto Now Playing, with these tracks underneath, so B
 * from there lands on the album it is playing. Returns how it was left, so
 * the shelf can close too when SELECT closed Muse - it used to be told
 * nothing, and SELECT from Now Playing landed on the shelf instead. */
static muse_exit muse_tracks(app *a, int album, bool now)
{
	int sel = 0, done = 0, art, i;
	muse_exit how = MUSE_BACK;
	menu_row *rows;
	char (*vals)[24];
	char heading[300], note[300], t0[16], t1[16];
	const ml_album *al;

	if (album < 0 || album >= g_muse.nalbums || g_muse.nartists == 0) return MUSE_BACK;
	al = &g_muse.albums[album];
	art = muse_artist_of(album);
	rows = calloc((size_t)al->n + 4, sizeof *rows);
	vals = calloc((size_t)al->n + 4, sizeof *vals);
	if (!rows || !vals) { free(rows); free(vals); return MUSE_BACK; }

	/* On the track that is playing, when it is one of these. */
	if (muse_playing_album(&i) == album) sel = i;

	if (now) {
		how = muse_now_screen(a);
		if (how != MUSE_BACK) done = 1;
		else if (muse_playing_album(&i) == album) sel = i;
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	while (!done && !want_quit && a->running) {
		const mu_now *mn;
		bool loaded, open_now = false;
		int n = 0, items;
		const char *playing;

		muse_screen_poll();
		cover_answers();
		mn = musec_now();
		loaded = mn->state == MU_PLAYING || mn->state == MU_PAUSED;
		playing = musec_path();
		plat_input_poll(&a->in);

		snprintf(heading, sizeof heading, "%s\n%s", al->name,
		         g_muse.artists[art].name);
		for (i = 0; i < al->n; i++, n++) {
			const ml_track *t = &g_muse.tracks[al->first + i];
			/* Every row a value, if only an empty one: a list with a value
			 * anywhere is laid out in two columns, left-aligned, and one
			 * without is centered. Without this the list sat centered until
			 * one of its tracks played and then jumped left, every name
			 * moving the moment A was pressed. Eric, 2026-09-19. */
			const char *v = "";

			if (playing[0] && !strcmp(playing, t->path))
				v = mn->state == MU_PAUSED ? "paused" : "playing";
			rows[n] = (menu_row){ t->name, v, true };
		}
		items = n;
		if (sel >= items) sel = items ? items - 1 : 0;

		/* What is playing, under a rule, even when it is another album's -
		 * the list is a way to reach music, and the music is the point. */
		if (loaded) {
			mmss(t0, sizeof t0, mn->at);
			mmss(t1, sizeof t1, mn->len);
			snprintf(note, sizeof note, "%s%s  -  %s   %s / %s",
			         mn->state == MU_PAUSED ? "Paused: " : "", mn->title,
			         mn->artist[0] ? mn->artist : mn->album, t0, t1);
			rows[n++] = MENU_RULE;
			rows[n++] = MENU_NOTE(note);
		}

		if (items > 0) {
			if (in_repeat(&a->in, IN_UP))   sel = (sel + items - 1) % items;
			if (in_repeat(&a->in, IN_DOWN)) sel = (sel + 1) % items;
		}
		if (in_repeat(&a->in, IN_LEFT))     musec_seek_by(-seek_step(&a->in, IN_LEFT));
		if (in_repeat(&a->in, IN_RIGHT))    musec_seek_by(+seek_step(&a->in, IN_RIGHT));
		if (in_repeat(&a->in, IN_L1))       musec_prev();
		if (in_repeat(&a->in, IN_R1))       musec_next();
		if (muse_keys(a)) {
			muse_locked(a);
			if (!a->running || g_muse_gone) { how = MUSE_CLOSE; break; }
			continue;
		}

		if (a->in.pressed[IN_ACCEPT] && items > 0) {
			/* The track already playing is not started over: A on it is the
			 * way to its Now Playing, the same as the row. */
			if (!loaded || strcmp(playing, g_muse.tracks[al->first + sel].path)) {
				const char **paths = calloc((size_t)al->n, sizeof *paths);

				if (paths) {
					for (i = 0; i < al->n; i++)
						paths[i] = g_muse.tracks[al->first + i].path;
					/* A book's file from its start: choosing one is choosing
					 * where to listen from, and the place moves with it. */
					muse_play(paths, al->n, sel, 0, al->book,
					          al->book ? book_speed(album) : 1.0,
					          g_muse.artists[art].name, al->name);
					free(paths);
				}
			}
			open_now = true;
		}
		if (a->in.pressed[IN_BACK]) done = 1;
		if (a->in.pressed[IN_SELECT]) { how = MUSE_CLOSE; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { muse_power(a); how = MUSE_CLOSE; break; }
		/* After a rescan `al` is gone with the old library: not one more
		 * frame from it. */
		if (a->in.pressed[IN_MENU]) {
			how = muse_menu(a);
			if (how != MUSE_BACK) break;
			continue;
		}

		/* Back from Now Playing onto the track it had reached, which may be
		 * several past the one chosen. */
		if (open_now) {
			how = muse_now_screen(a);
			if (how != MUSE_BACK) break;
			if (muse_playing_album(&i) == album) sel = i;
			continue;
		}

		muse_backdrop(a);
		menu_draw(a, heading, rows, n, sel, menu_std_width(a), MUSE_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(8);
	}

	free(rows);
	free(vals);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return how;
}

/* SELECT, from anywhere but a running game - the shelf, every menu, and the
 * in-game menu, where the game waits paused underneath: Muse, and straight to
 * Now Playing when something is playing or paused. Nothing on a card with no
 * music, which is also a card with no Muse on its shelf.
 *
 * `over` is the menu it opened over, or NULL for the shelf and the screens
 * that draw it. True when that menu's power rule ended it too. */
static bool muse_open(app *a, const menu_style *over, void *ctx)
{
	const mu_now *mn = musec_now();

	if (g_muse.ntracks == 0) return false;
	g_muse_over = over;
	g_muse_over_ctx = ctx;
	g_muse_gone = false;
	/* ChaOS: Muse's shelf is off the row: on it for as long as SELECT has
	 * Muse open (a Rescan inside rebuilds it too, see build_muse_shelf) */
	if (!g_muse_shelf) {
		g_muse_attach = true;
		muse_shelf_attach(a);
	}
	muse_shelf_screen(a, mn->state == MU_PLAYING || mn->state == MU_PAUSED);
	if (g_muse_attach) {
		g_muse_attach = false;
		muse_shelf_detach(a);
	}
	g_muse_over = NULL;
	g_muse_over_ctx = NULL;
	/* Nothing Muse did closes the menu it may have been opened over. */
	g_menu_closing = false;
	return g_muse_gone;
}

/* A on a book: where it was left, straight onto Now Playing with its files
 * underneath. The beginning for a book never started or finished; nothing new
 * for the book already playing or paused, which A only opens. */
static muse_exit muse_book(app *a, int al)
{
	const mu_now *mn = musec_now();
	bool loaded = mn->state == MU_PLAYING || mn->state == MU_PAUSED;

	if (!loaded || muse_playing_album(NULL) != al) {
		const ml_album *b = &g_muse.albums[al];
		const char **paths = calloc((size_t)b->n, sizeof *paths);
		double at;
		int i, start = book_place(al, &at);

		if (!paths) return MUSE_BACK;
		for (i = 0; i < b->n; i++) paths[i] = g_muse.tracks[b->first + i].path;
		fprintf(stderr, "muse: %s from file %d at %.1f\n", b->name, start + 1, at);
		muse_play(paths, b->n, start, at, true, book_speed(al),
		          g_muse.artists[muse_artist_of(al)].name, b->name);
		free(paths);
	}
	return muse_tracks(a, al, true);
}

/* A on card `k` of Muse's shelf: its album's tracks, over the shelf, or a
 * book where it was left. */
static muse_exit muse_album(app *a, const sysview *v, int k)
{
	if (!v->album || k < 0 || k >= v->list.count) return MUSE_BACK;
	if (g_muse.albums[v->album[k]].book) return muse_book(a, v->album[k]);
	return muse_tracks(a, v->album[k], false);
}

/* Muse's shelf again after its menu, whose Rescan Folder rebuilds every view
 * on the card, so the view being held is found again rather than trusted.
 * NULL when there is no Muse left to show. */
static sysview *muse_shelf_refind(app *a)
{
	sysview *v;
	int i, muse = -1;

	for (i = 0; i < a->sys.count; i++)
		if (is_muse(&a->sys.systems[i])) { muse = i; break; }
	if (muse < 0 || a->view[muse].list.count <= 0) return NULL;
	a->sys_cursor = muse;
	a->screen = SCREEN_GAMES;
	v = &a->view[muse];
	if (v->cursor >= v->list.count) v->cursor = v->list.count - 1;
	cf_reset(&v->cf, v->cursor);
	return v;
}

/* Which card on Muse's shelf is that album, or -1. */
static int muse_card_of(const sysview *v, int album)
{
	int k;

	for (k = 0; v->album && k < v->list.count; k++)
		if (v->album[k] == album) return k;
	return -1;
}

/* MUSE'S SHELF, OPENED ON TOP OF WHATEVER WAS ON SCREEN. Eric's, 2026-09-20.
 *
 * SELECT used to open a list of artists, because this shelf belongs to the
 * main loop - it is the games shelf, drawn for Muse's own view - and SELECT
 * can be pressed inside a menu, on Play Time, or in the in-game menu over a
 * paused game, none of which the main loop is driving. A list could draw over
 * any of those and the shelf could not, so the front door to a screen made
 * entirely of covers was text. The rest of the launcher is pictures.
 *
 * So the shelf is driven from here as well: the same view, the same cards, the
 * same movement, and draw_shelf doing the drawing - the one loop in this file
 * that borrows another screen's. What it does NOT borrow is update_games'
 * input, because B has to come back here rather than walk out to the systems
 * row, and MENU has to survive a Rescan rebuilding every view underneath it.
 *
 * The screen and the cursor are put back on the way out, so whatever opened
 * Muse is still there when it closes. */
static void muse_shelf_screen(app *a, bool now)
{
	int prev_screen = a->screen, prev_sys = a->sys_cursor;
	bool vert = CARD_DIRS[g_dir].vertical;
	in_button back = vert ? IN_DOWN : IN_LEFT, fwd = vert ? IN_UP : IN_RIGHT;
	in_button jup = vert ? IN_LEFT : IN_UP, jdn = vert ? IN_RIGHT : IN_DOWN;
	sysview *v;
	int muse = -1, i, playing;
	bool done = false;

	for (i = 0; i < a->sys.count; i++)
		if (is_muse(&a->sys.systems[i])) { muse = i; break; }
	if (muse < 0 || a->view[muse].list.count <= 0) return;

	a->sys_cursor = muse;
	enter_system(a);                      /* the screen, the coverflow, the window */
	v = &a->view[muse];

	/* On the album that is playing, so SELECT lands where the music is - on
	 * the books when it is a book. */
	playing = muse_playing_album(NULL);
	if (playing >= 0 && g_muse.albums[playing].book != muse_books_shown())
		muse_show(v, g_muse.albums[playing].book);
	if (playing >= 0) {
		int k = muse_card_of(v, playing);

		if (k >= 0) { v->cursor = k; cf_reset(&v->cf, k); }
	}
	if (now && playing >= 0) {
		muse_exit how = muse_tracks(a, playing, true);

		if (how == MUSE_CLOSE || g_muse_gone) done = true;
		else if (how == MUSE_REBUILT) {
			sysview *nv = muse_shelf_refind(a);

			if (nv) v = nv; else done = true;
		}
	}

	while (!done && !want_quit && a->running) {
		int n = v->list.count, dir = 0;

		muse_screen_poll();
		cover_answers();
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { muse_power(a); break; }
		/* B and SELECT both leave, which is the rule everywhere in Muse: one
		 * button in, the same button out, and B for the level below. */
		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_SELECT]) break;

		if (n > 0) {
			if (in_repeat(&a->in, back)) { v->cursor = (v->cursor - 1 + n) % n; dir = -1; }
			if (in_repeat(&a->in, fwd))  { v->cursor = (v->cursor + 1) % n; dir = +1; }
			if (in_repeat(&a->in, jdn))  v->cursor = shelf_letter_jump(v, +1);
			if (in_repeat(&a->in, jup))  v->cursor = shelf_letter_jump(v, -1);
			if (in_repeat(&a->in, IN_L1)) v->cursor = ((v->cursor - CF_WINDOW) % n + n) % n;
			if (in_repeat(&a->in, IN_R1)) v->cursor = (v->cursor + CF_WINDOW) % n;
			cf_set_cursor_dir(&v->cf, v->cursor, n, dir);
		}
		if (muse_keys(a)) {
			muse_locked(a);
			if (!a->running || g_muse_gone) break;
			continue;
		}

		if (a->in.pressed[IN_ACCEPT]) {
			/* SELECT from the tracks or Now Playing closes all of Muse, and
			 * a rescan from their MENU leaves this shelf to be found again. */
			muse_exit how = muse_album(a, v, v->cursor);

			if (how == MUSE_CLOSE || g_muse_gone) break;
			if (how == MUSE_REBUILT) {
				sysview *nv = muse_shelf_refind(a);

				if (!nv) break;
				v = nv;
			}
		} else if (a->in.pressed[IN_MENU]) {
			/* Muse's own menu. Sort By reorders this shelf and Rescan
			 * Folder rebuilds it, so it is found again either way. */
			sysview *nv;

			if (muse_menu(a) == MUSE_CLOSE) break;
			if (!(nv = muse_shelf_refind(a))) break;
			v = nv;
		}

		tick_tint(a);
		draw_shelf(a);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(8);
	}

	evict_far(v, TEX_KEEP_FAR);
	a->screen = prev_screen;
	a->sys_cursor = prev_sys;
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* ---- Muse: Album Art -------------------------------------------------------- */

/* The artist an album is under, and its folder relative to the Music folder. */
static int muse_artist_of(int al)
{
	int ar;

	for (ar = 0; ar < g_muse.nartists - 1; ar++)
		if (al < g_muse.artists[ar].first + g_muse.artists[ar].n) break;
	return ar;
}

static void muse_album_dir(int al, char *out, size_t n)
{
	const char *p = g_muse.albums[al].n > 0 ? g_muse.tracks[g_muse.albums[al].first].path : "";
	const char *slash = strrchr(p, '/');

	snprintf(out, n, "%.*s", slash ? (int)(slash - p) : 0, p);
}

/* Which albums Album Art asks about: every one with no cover on the card, or
 * with one smaller on its short side than the shelf draws it - the album
 * frame's height, 461px - which the service has not already supplied. Eric's
 * rule, 2026-09-19. A cover the service gave is remembered, because its own
 * can be small too - La Strada's is 360px - and asking again would only fetch
 * the same one; an album it could not find is asked about on every run. */
static int museart_jobs(museart_job *jobs, int max)
{
	int crisp = (int)(TORTOS_SCREEN_H * CF_LAYOUT_ALBUMS.size + 0.5f);
	int i, n = 0;

	for (i = 0; i < g_muse.nalbums && n < max; i++) {
		char dir[LIB_PATH], key[LIB_PATH + 16], p[LIB_PATH * 2 + 8];
		museart_job *j = &jobs[n];
		int w = 0, h = 0;
		bool have;

		/* Not a book: MusicBrainz knows records, and a book's cover is its
		 * own file or the picture in its folder. Nor Singles, which is no
		 * record at all and has its own cover. */
		if (g_muse.albums[i].n <= 0 || g_muse.albums[i].book ||
		    g_muse.albums[i].singles) continue;
		muse_album_dir(i, dir, sizeof dir);
		snprintf(key, sizeof key, "museart.%s", dir);
		ml_cover_base(g_muse_root, &g_muse, i, j->base, sizeof j->base);
		snprintf(p, sizeof p, "%s.jpg", j->base);
		have = file_nonempty(p);
		if (!have) { snprintf(p, sizeof p, "%s.png", j->base); have = file_nonempty(p); }
		/* Remembered only while the cover it gave is on the card: one deleted
		 * since is asked about again, where the memory alone kept every such
		 * album off the list for good (2026-10-05). */
		if (have && db_has(db_lib(), key)) continue;
		if (have && museart_image_size(p, &w, &h) && (w < h ? w : h) >= crisp) continue;
		snprintf(j->artist, sizeof j->artist, "%s", g_muse.artists[muse_artist_of(i)].name);
		snprintf(j->album, sizeof j->album, "%s", g_muse.albums[i].name);
		j->tracks = g_muse.albums[i].n;
		j->id = i;
		n++;
	}
	return n;
}

/* A cover arrived for album `al`: remember where it came from, and drop the old
 * one from the shelf and from Now Playing so both draw the new one. The whole
 * worker queue is disowned, not just this card, because a decode of the OLD
 * file may already be in flight and would otherwise land on top. */
static void album_art_landed(app *a, int al, const char *rg)
{
	char dir[LIB_PATH], key[LIB_PATH + 16];
	int s;

	if (al < 0 || al >= g_muse.nalbums) return;
	muse_album_dir(al, dir, sizeof dir);
	snprintf(key, sizeof key, "museart.%s", dir);
	db_set_str(db_lib(), key, rg[0] ? rg : "fetched");
	if (g_cov) g_cov[al].st = COV_JPG;
	for (s = 0; s < a->sys.count; s++) {
		sysview *v = &a->view[s];
		int k;

		if (!is_muse(&a->sys.systems[s]) || !v->tex || !v->album) continue;
		for (k = 0; k < v->list.count; k++)
			if (v->album[k] == al && v->tex[k]) {
				SDL_DestroyTexture(v->tex[k]);
				v->tex[k] = NULL;
			}
	}
	texload_bump();
	faces_stale();
	if (g_np.album == al) np_forget();
}

/* ---- Muse: album covers from the web page (Down To The Wire) ------------
 *
 * Album Art's rules, answered for the page at the other end of the cable,
 * which asks MusicBrainz and the Cover Art Archive itself: the Pixel has no
 * network. Which albums want a cover is museart_jobs, which release a reply
 * names is museart_pick, and a cover that lands is remembered as Album Art
 * remembers its own. See hare_set_albums. */

/* Where an album's cover goes, as a path the page uploads to: the card's
 * Music/<artist>/.media/<album>.jpg is music/<artist>/.media/<album>.jpg. */
static bool album_cover_path(int al, char *out, size_t n)
{
	char base[LIB_PATH * 2];
	size_t root = strlen(g_muse_root);

	ml_cover_base(g_muse_root, &g_muse, al, base, sizeof base);
	if (strncmp(base, g_muse_root, root) || strncmp(base + root, "/Music/", 7))
		return false;
	return snprintf(out, n, "music/%s.jpg", base + root + 7) < (int)n;
}

/* The album a pick answered and its release group, until its cover lands.
 * One at a time: the page uploads each cover straight after its pick. */
static int  g_pick_al = -1;
static char g_pick_rg[64];

static char *albums_wanted(size_t *len, char *why, size_t wn)
{
	museart_job *jobs = calloc((size_t)(g_muse.nalbums > 0 ? g_muse.nalbums : 1),
	                           sizeof *jobs);
	hj o = { 0 };
	bool first = true;
	int n, i, k;

	if (!jobs) { snprintf(why, wn, "out of memory"); return NULL; }
	n = museart_jobs(jobs, g_muse.nalbums);
	hj_lit(&o, "{\"albums\":[");
	for (i = 0; i < n; i++) {
		char artists[MUSEART_TRIES][128], albums[MUSEART_TRIES][128];
		char url[1024], cover[LIB_PATH * 2];
		int tries = museart_tries(jobs[i].artist, jobs[i].album, artists, albums);

		if (!album_cover_path(jobs[i].id, cover, sizeof cover)) continue;
		if (!first) hj_lit(&o, ",");
		first = false;
		hj_lit(&o, "{\"id\":");
		hj_int(&o, jobs[i].id);
		hj_lit(&o, ",\"artist\":");
		hj_str(&o, jobs[i].artist);
		hj_lit(&o, ",\"album\":");
		hj_str(&o, jobs[i].album);
		/* The searches in Album Art's order, each asked only if the one
		 * before named nothing. */
		hj_lit(&o, ",\"searches\":[");
		for (k = 0; k < tries; k++) {
			if (!museart_search_url(artists[k], albums[k], url, sizeof url)) continue;
			if (k) hj_lit(&o, ",");
			hj_str(&o, url);
		}
		hj_lit(&o, "]}");
	}
	hj_lit(&o, "]}");
	free(jobs);
	return hj_done(&o, len, why, wn);
}

static char *album_pick(int id, const char *json, size_t n, size_t *len,
                        char *why, size_t wn)
{
	char rg[64] = "", cover[LIB_PATH * 2] = "", base[LIB_PATH * 2], *slash;
	hj o = { 0 };

	if (id < 0 || id >= g_muse.nalbums || g_muse.albums[id].book) {
		snprintf(why, wn, "no such album");
		return NULL;
	}
	if (museart_pick(json, n, g_muse.albums[id].n, rg, sizeof rg) &&
	    album_cover_path(id, cover, sizeof cover)) {
		/* The .media folder, which an album whose files never carried a
		 * cover does not have: the upload needs it to exist. */
		ml_cover_base(g_muse_root, &g_muse, id, base, sizeof base);
		if ((slash = strrchr(base, '/'))) { *slash = '\0'; mkdir(base, 0755); }
		g_pick_al = id;
		snprintf(g_pick_rg, sizeof g_pick_rg, "%s", rg);
	} else {
		rg[0] = cover[0] = '\0';
	}
	hj_lit(&o, "{\"rg\":");
	hj_str(&o, rg);
	hj_lit(&o, ",\"cover\":");
	hj_str(&o, cover);
	hj_lit(&o, "}");
	return hj_done(&o, len, why, wn);
}

/* Every upload, from the transfer screen: a game's cover is shrunk, and the
 * cover of the album last picked is taken as Album Art takes one, the PNG the
 * files carried removed and the release group remembered. */
static void xfer_after_write(const char *abs)
{
	char base[LIB_PATH * 2], p[LIB_PATH * 2 + 8];

	hareart_after_write(abs);
	if (g_pick_al < 0 || g_pick_al >= g_muse.nalbums) return;
	ml_cover_base(g_muse_root, &g_muse, g_pick_al, base, sizeof base);
	snprintf(p, sizeof p, "%s.jpg", base);
	if (strcmp(p, abs)) return;
	snprintf(p, sizeof p, "%s.png", base);
	remove(p);
	if (g_logs_app) album_art_landed(g_logs_app, g_pick_al, g_pick_rg);
	g_pick_al = -1;
}

/* Album Art, from Muse's menu: MusicBrainz and the Cover Art Archive, for the
 * albums museart_jobs chooses. The covers change on the shelf behind the panel
 * as they arrive. B stops. */
static void album_art_screen(app *a)
{
	museart_job *jobs;
	museart_status st;
	menu_row rows[3];
	char head[300], fit[200], where[64], counts[96];
	bool working, done = false;
	int n;

	if (!net_online()) {
		menu_row row = { "No network", NULL, false };

		draw_shelf(a);
		menu_draw(a, "Album Art", &row, 1, -1, 0, MUSE_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(1600);
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		return;
	}
	jobs = calloc((size_t)(g_muse.nalbums > 0 ? g_muse.nalbums : 1), sizeof *jobs);
	if (!jobs) return;
	n = museart_jobs(jobs, g_muse.nalbums);
	/* The one async slot, which an account answer from a game just quit may
	 * still be holding - the same reason Box Art gives it up first. */
	sync_abandon();
	working = museart_begin(jobs, n, "/tmp/tortos-musicbrainz.json");
	free(jobs);
	fprintf(stderr, "album art: %d album%s to ask about\n", n, n == 1 ? "" : "s");

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (!done && !want_quit && a->running) {
		if (working && museart_step(plat_now_ms()) == 0) {
			working = false;
			museart_status_get(&st);
			fprintf(stderr, "album art: %d found, %d not found, %d failed%s%s\n",
			        st.found, st.missing, st.failed, st.problem[0] ? " - " : "",
			        st.problem);
		}
		museart_status_get(&st);
		if (st.landed >= 0) album_art_landed(a, st.landed, st.rg);

		ui_fit_text(ui_font(UI_F_LABEL),
		            working ? (st.now[0] ? st.now : "starting")
		            : st.problem[0] ? st.problem
		            : n == 0 ? "Every album has a crisp cover" : "Done",
		            fit, sizeof fit, TORTOS_SCREEN_W * 3 / 4);
		snprintf(head, sizeof head, "Album Art\n%s", fit);
		snprintf(where, sizeof where, "%d of %d", st.done, st.n);
		if (st.failed)
			snprintf(counts, sizeof counts, "%d found, %d not found, %d failed",
			         st.found, st.missing, st.failed);
		else
			snprintf(counts, sizeof counts, "%d found, %d not found",
			         st.found, st.missing);
		rows[0] = (menu_row){ "Albums", where,  false };
		rows[1] = (menu_row){ "Covers", counts, false };
		rows[2] = (menu_row){ working ? "B to stop" : "B to close", NULL, false };

		plat_input_poll(&a->in);
		if (a->in.quit_requested) { museart_cancel(); a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			museart_cancel();
			power_off(a);
			return;
		}
		if (menu_leaving(a)) done = true;
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		/* Idle does not count while covers are arriving: it is the device
		 * doing what it was asked, and powering off halfway loses the rest. */
		if (working) a->idle.since_ms = plat_now_ms();

		tick_tint(a);
		draw_shelf(a);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw(a, head, rows, 3, -1, menu_std_width(a), MUSE_ACCENT);
		plat_draw_osd(a->r);
		plat_present();
		SDL_Delay(8);
	}
	museart_cancel();
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

static void synopsis_screen(app *a, const char *title, const char *text,
                            unsigned accent)
{
	char (*lines)[CHV_WRAP_COLS];
	menu_row *rows;
	int fixed = menu_std_width(a);
	int n, done = 0, loop_at = 0;

	if (!text || !*text) return;
	lines = calloc(SYN_WRAP_LINES, sizeof *lines);
	rows  = calloc(SYN_ROWS, sizeof *rows);
	if (!lines || !rows) { free(lines); free(rows); return; }

	n = syn_layout(text, fixed, lines, rows, &loop_at);

	/* Opened, so its scroll starts at the top. Without this, closing a long
	 * synopsis and opening it again resumed halfway down: the clock's keys -
	 * the row count and the panel height - are identical on the way back in,
	 * so nothing told it a reader had arrived. */
	mq_reset(MQ_VSCROLL);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	while (!done && !want_quit && a->running) {
		plat_input_poll(&a->in);
		if (a->in.quit_requested) break;
		if (menu_leaving(a) || a->in.pressed[IN_ACCEPT] || a->in.pressed[IN_X]) done = 1;
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);
		/* Nothing else watches power for this screen, the same as every other
		 * loop the launcher runs outside plat_resident_wait. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); break; }

		draw_shelf(a);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw_ex(a, title, rows, n, -1, fixed, accent, NULL, false, loop_at);
		plat_present();
		SDL_Delay(8);
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	free(lines);
	free(rows);
}

static void cheevos_screen(app *a, SDL_Texture *bg, bool over_shelf)
{
	int n = chv_count(), sel = 0, i, done = 0;
	menu_row *rows;
	unsigned *vcols;
	char (*vals)[16];
	char heading[192];

	if (n <= 0) return;
	rows  = calloc((size_t)n, sizeof *rows);
	vals  = calloc((size_t)n, sizeof *vals);
	vcols = calloc((size_t)n, sizeof *vcols);
	if (!rows || !vals || !vcols) { free(rows); free(vals); free(vcols); return; }

	for (i = 0; i < n; i++) {
		const cheevo *c = chv_at(i);

		snprintf(vals[i], sizeof vals[i], "%d", c->points);
		rows[i].label = c->title;
		rows[i].value = vals[i];
		rows[i].live  = c->earned || c->earned_now;
		/* Said by color on EVERY earned row, not only the one under the
		 * cursor. live already lifts the label one step, which alone was too
		 * little to scan a hundred-entry set by. */
		vcols[i] = rows[i].live ? UI_EARNED_RGB : 0u;
	}
	/* Two lines: the game on one, the counts on the other.
	 *
	 * On one line this was "Hagane: The Final Conflict   0/36   0/415 points",
	 * 1031 pixels against a 778 pixel panel - and menu_draw centers a heading,
	 * so it lost BOTH ends: the H and the word "points". Dropping the title
	 * fixed the clipping and threw away something worth keeping. Eric's
	 * suggestion, and it is better than either. */
	snprintf(heading, sizeof heading, "%s\n%d/%d cheevos   %d/%d points",
	         chv_game_title(), chv_earned(), n,
	         chv_points_earned(), chv_points_total());

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	while (!done && !want_quit) {
		plat_input_poll(&a->in);

		if (in_repeat(&a->in, IN_UP))   sel = (sel + n - 1) % n;
		if (in_repeat(&a->in, IN_DOWN)) sel = (sel + 1) % n;
		/* A set runs to well over a hundred entries, so the shoulder buttons
		 * page it the same way they page a shelf. */
		if (in_repeat(&a->in, IN_L1))   sel = sel > 8 ? sel - 8 : 0;
		if (in_repeat(&a->in, IN_R1))   sel = sel < n - 9 ? sel + 8 : n - 1;
		if (menu_leaving(a)) done = 1;
		if (over_shelf && a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);
		/* A used to close this screen. It opens the achievement instead,
		 * which is the only thing on it there was ever anything more to say
		 * about; BACK and MENU still close, so nothing lost a way out. */
		if (a->in.pressed[IN_ACCEPT] && cheevo_detail_screen(a, bg, over_shelf, chv_at(sel)))
			done = 1;
		/* Power still stops the game from in here. Not trapping it would
		 * make this screen the one place in the launcher that ignores it.
		 *
		 * The note is what was missing: stopping the game is not the same as
		 * powering off, and only the evdev watchdog inside plat_resident_wait
		 * sets that flag - a loop which is not running while this screen is
		 * up. So power here stopped the game and then went back to the shelf,
		 * with the menu still drawn over it until something was pressed.
		 *
		 * Opened from the game details screen there is no game to stop, and
		 * power means what it means everywhere else on the shelf. */
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			if (over_shelf) { power_off(a); break; }
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = 1;
		}

		chv_backdrop(a, bg, over_shelf);
		/* visits_all: every achievement can be opened, earned or not, so
		 * `live` here is about color and not about reach. */
		menu_draw_ex(a, heading, rows, n, sel, menu_std_width(a), a->tint,
		             vcols, true, 0);
		plat_present();
		SDL_Delay(8);
	}

	free(rows);
	free(vals);
	free(vcols);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* The standard width, so the panel does not resize as the Overlay row goes
 * through names, or on the way into the shader's settings, which use it too. */
static int gm_width(app *a)
{
	return menu_std_width(a);
}

/* ---- ChaOS: screenshots, the game being played's ---------------------------
 *
 * Full screen, newest first: the picture as large as a whole-number scale
 * lets it be, as the game was drawn, and a line under it saying which and
 * when. Left and right go through them; X deletes the one shown, after X
 * again to be sure; B or MENU goes back. It opens on the one looked at
 * last, so a password can be read again in two presses; kept per game in
 * the library database, so it is still that one after the game is closed. */
static int gm_count_shots(void)
{
	static char names[SHOTS_MAX][SHOTS_NAME];
	return g_shots_dir[0] ? shots_list(g_shots_dir, names, SHOTS_MAX) : 0;
}

static void shots_draw(app *a, SDL_Texture *tex, int tw, int th, const char *name,
                       int sel, int n, int asking, float shown)
{
	char when[48];
	static SDL_Texture *layer;     /* the caption, drawn here to fade as one */
	static SDL_Renderer *made_for; /* the renderer it belongs to (draw_letter_popup) */

	if (made_for != a->r) { layer = NULL; made_for = a->r; }

	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
	SDL_RenderClear(a->r);
	if (tex && tw > 0 && th > 0) {
		int k = TORTOS_SCREEN_W / tw < TORTOS_SCREEN_H / th ? TORTOS_SCREEN_W / tw
		                                                     : TORTOS_SCREEN_H / th;
		SDL_Rect d;
		if (k >= 1) { d.w = tw * k; d.h = th * k; }
		else {      /* bigger than the screen: fitted */
			float f = (float)TORTOS_SCREEN_W / tw < (float)TORTOS_SCREEN_H / th
			        ? (float)TORTOS_SCREEN_W / tw : (float)TORTOS_SCREEN_H / th;
			d.w = (int)(tw * f); d.h = (int)(th * f);
		}
		d.x = (TORTOS_SCREEN_W - d.w) / 2;
		d.y = (TORTOS_SCREEN_H - d.h) / 2;
		/* the game's pixels, square and sharp, as the game drew them */
		SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
		SDL_RenderCopy(a->r, tex, NULL, &d);
	}
	if (asking) {
		/* the menus' own question, over the picture: Keep first, so A pressed
		 * by habit keeps it */
		menu_row rows[2] = { { "Keep", NULL, true }, { "Delete", NULL, true } };
		menu_draw(a, "Delete Screenshot?", rows, 2, asking - 1, 0, MENU_ACCENT);
		return;
	}
	if (shown > 0.0f) {
		/* A menu panel along the bottom: when, as a menu's row reads, then
		 * which of how many and the buttons, as a menu's note reads. Drawn
		 * on a layer of its own so it fades out whole, leaving the picture
		 * clear, and back in when the picture changes or left or right is
		 * pressed. */
		TTF_Font *fm = ui_font(UI_F_MENU), *fn = ui_font(UI_F_META);
		int lm = ui_font_line(UI_F_MENU), ln = ui_font_line(UI_F_META);
		int pad = UI_PANEL_BORDER + 10, w;
		char note[128];
		SDL_Rect panel;

		shots_label(name, when, sizeof when);
		snprintf(note, sizeof note, "%s%d of %d%s     X  Delete     B  Back",
		         sel > 0 ? "\xE2\x80\xB9  " : "", sel + 1, n,
		         sel < n - 1 ? "  \xE2\x80\xBA" : "");
		panel.w = ui_text_width(fn, note);
		w = ui_text_width(fm, when);
		if (w > panel.w) panel.w = w;
		panel.w += pad * 2 + 24;
		panel.h = lm + ln + pad * 2 + 4;
		panel.x = (TORTOS_SCREEN_W - panel.w) / 2;
		panel.y = TORTOS_SCREEN_H - panel.h - 10;
		if (!layer) {
			layer = SDL_CreateTexture(a->r, SDL_PIXELFORMAT_RGBA8888,
			                          SDL_TEXTUREACCESS_TARGET,
			                          TORTOS_SCREEN_W, TORTOS_SCREEN_H);
			if (layer) SDL_SetTextureBlendMode(layer, SDL_BLENDMODE_BLEND);
		}
		if (layer && shown < 1.0f && SDL_SetRenderTarget(a->r, layer) == 0) {
			SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_NONE);
			SDL_SetRenderDrawColor(a->r, 0, 0, 0, 0);
			SDL_RenderClear(a->r);
			SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		} else {
			shown = 1.0f;            /* straight onto the screen */
		}
		ui_panel(a->r, &panel, MENU_RADIUS, MENU_ACCENT);
		ui_text(a->r, fm, when, TORTOS_SCREEN_W / 2, panel.y + pad + 2, 0, UI_TEXT);
		ui_text(a->r, fn, note, TORTOS_SCREEN_W / 2, panel.y + pad + 2 + lm, 0, UI_TEXT_DIM);
		if (shown < 1.0f) {
			plat_draw_to_screen();
			SDL_SetTextureAlphaMod(layer, (Uint8)(shown * 255));
			SDL_RenderCopy(a->r, layer, NULL, NULL);
		}
	}
}

static void shots_screen(app *a)
{
	static char names[SHOTS_MAX][SHOTS_NAME];
	SDL_Texture *tex = NULL;
	int n = shots_list(g_shots_dir, names, SHOTS_MAX), sel = 0, loaded = -1, i;
	char key[LIB_PATH * 2 + 16], last[SHOTS_NAME];

	/* The one looked at last, when it is still there */
	snprintf(key, sizeof key, "shots.last:%s", g_shots_dir);
	db_get_str(db_lib(), key, last, sizeof last, "");
	for (i = 0; last[0] && i < n; i++)
		if (!strcmp(names[i], last)) { sel = i; break; }
	int tw = 0, th = 0, asking = 0;     /* asking: 0, or 1 + the row on */
	bool done = false;
	Uint32 cap_at = SDL_GetTicks();     /* when the caption last came up */
	char path[LIB_PATH * 2 + SHOTS_NAME];

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (n > 0 && !done && !want_quit) {
		plat_input_poll(&a->in);
		if (asking) {
			if (in_repeat(&a->in, IN_UP) || in_repeat(&a->in, IN_DOWN)) asking = 3 - asking;
			if (a->in.pressed[IN_ACCEPT]) {
				if (asking == 2 && shots_delete(g_shots_dir, names[sel])) {
					n = shots_list(g_shots_dir, names, SHOTS_MAX);
					if (sel >= n) sel = n - 1;
					loaded = -1;
				}
				asking = 0;
			} else if (a->in.pressed[IN_BACK] || a->in.pressed[IN_MENU]) {
				asking = 0;
			}
		} else {
			/* left or right brings the caption back, at the ends too */
			if (in_repeat(&a->in, IN_LEFT))  { if (sel > 0) sel--;     cap_at = SDL_GetTicks(); }
			if (in_repeat(&a->in, IN_RIGHT)) { if (sel < n - 1) sel++; cap_at = SDL_GetTicks(); }
			if (a->in.pressed[IN_X]) asking = 1;
			if (menu_leaving(a)) done = true;
		}
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			plat_note_power_pressed();
			plat_resident_line("STOP");
			done = true;
		}
		if (n <= 0) break;
		if (loaded != sel) {
			SDL_Surface *sf;
			if (tex) SDL_DestroyTexture(tex);
			tex = NULL;
			snprintf(path, sizeof path, "%s/%s", g_shots_dir, names[sel]);
			if ((sf = IMG_Load(path))) {
				tex = SDL_CreateTextureFromSurface(a->r, sf);
				SDL_FreeSurface(sf);
			}
			if (tex) SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
			loaded = sel;
		}

		if (asking) cap_at = SDL_GetTicks();   /* back after the question */
		{
			/* up for 1.5 s, then out over 0.4 s */
			Uint32 el = SDL_GetTicks() - cap_at;
			float shown = el < 1500 ? 1.0f : el < 1900 ? 1.0f - (float)(el - 1500) / 400.0f : 0.0f;
			shots_draw(a, tex, tw, th, names[sel], sel, n, asking, shown);
		}
		plat_present();
		SDL_Delay(8);
	}
	if (tex) SDL_DestroyTexture(tex);
	db_set_str(db_lib(), key, n > 0 ? names[sel] : "");
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* The in-game menu's context. `resume` is what the epilogue turns on: whether
 * the player is going back to the game or the game is over. */
typedef struct {
	app         *a;
	SDL_Texture *bg;       /* the paused frame, as Diatom last wrote it */
	SDL_Texture *ov;       /* ChaOS: the overlay, drawn over it, or NULL */
	gm_bufs      bufs;
	menu_style   st;
	bool         resume;
	int          shots;        /* ChaOS: this game's screenshots */
	Uint32       shot_said;    /* when Take Screenshot last said "Saved", or 0 */
	bool         shot_ok;
} gm_ctx;

/* The overlay as Diatom draws it: around the game's picture, never over it
 * (port/brick.c, bezel_over). Four strips, each from the same part of the
 * overlay, scaled as the whole overlay is to the screen. */
static void draw_overlay_around(app *a, SDL_Texture *ov)
{
	SDL_Rect g, d[4];
	int tw, th, i;

	if (!plat_resident_rect(&g)) { SDL_RenderCopy(a->r, ov, NULL, NULL); return; }
	SDL_QueryTexture(ov, NULL, NULL, &tw, &th);
	d[0] = (SDL_Rect){ 0, 0, TORTOS_SCREEN_W, g.y };
	d[1] = (SDL_Rect){ 0, g.y + g.h, TORTOS_SCREEN_W, TORTOS_SCREEN_H - g.y - g.h };
	d[2] = (SDL_Rect){ 0, g.y, g.x, g.h };
	d[3] = (SDL_Rect){ g.x + g.w, g.y, TORTOS_SCREEN_W - g.x - g.w, g.h };
	for (i = 0; i < 4; i++) {
		SDL_Rect sr = { d[i].x * tw / TORTOS_SCREEN_W, d[i].y * th / TORTOS_SCREEN_H,
		                d[i].w * tw / TORTOS_SCREEN_W, d[i].h * th / TORTOS_SCREEN_H };
		if (d[i].w > 0 && d[i].h > 0) SDL_RenderCopy(a->r, ov, &sr, &d[i]);
	}
}

/* The console whose game is paused: the owner, for one played from
 * Favorites. */
static int gm_owner(app *a)
{
	sysview *sv = &a->view[a->sys_cursor];
	return shelf_owner(a, a->sys_cursor, sv->cursor);
}

/* The paused frame again: shaded when Diatom drew one, plain when not. */
static void gm_load_bg(gm_ctx *c)
{
	const char *pv = plat_resident_last_shaded();
	SDL_Surface *sf;

	if (!pv) pv = plat_resident_last_preview();
	if (!pv || !*pv || !(sf = IMG_Load(pv))) return;
	if (c->bg) SDL_DestroyTexture(c->bg);
	c->bg = SDL_CreateTextureFromSurface(c->a->r, sf);
	SDL_FreeSurface(sf);
}

/* The overlay, from its PNG, for the backdrop: what the game will be under. */
static void gm_load_overlay(gm_ctx *c)
{
	app *a = c->a;
	int o = gm_owner(a);
	char path[LIB_PATH * 3], buf[8];
	SDL_Surface *sf;

	if (c->ov) { SDL_DestroyTexture(c->ov); c->ov = NULL; }
	if (!overlay_label(a, o, buf, sizeof buf)) return;
	overlay_dir(a, o, path, sizeof path);
	snprintf(path + strlen(path), sizeof path - strlen(path), "/%s", a->view[o].overlay);
	if (!(sf = IMG_Load(path))) return;
	c->ov = SDL_CreateTextureFromSurface(a->r, sf);
	SDL_FreeSurface(sf);
	if (c->ov) SDL_SetTextureBlendMode(c->ov, SDL_BLENDMODE_BLEND);
}

/* The console's shader settings, to the paused game at once: Diatom draws the
 * paused frame with them, and that becomes the backdrop. */
static void gm_apply_shader(gm_ctx *c)
{
	app *a = c->a;
	int o = gm_owner(a);
	look_console lc = look_console_of(a->sys.systems[o].tag);

	if (lc == LOOK_NONE) return;
	if (!look_write_cfg(LOOK_CFG_PATH, lc, &a->view[o].look)) return;
	plat_resident_line("SETSHADER\tpath=%s\tpreview=%s", LOOK_CFG_PATH, LOOK_SHADED_PATH);
	if (plat_resident_sync_shaded(2000)) gm_load_bg(c);
}

/* ChaOS: Display Mode, to the paused game at once. Diatom answers with the new
 * rect (DISPLAY), which is where the backdrop is drawn; with a shader on, the
 * shaded frame is drawn again at the new size, and the speed check starts
 * over, as the shader's cost moves with the size. */
static void gm_apply_overlay(gm_ctx *c);

static void gm_cycle_display(gm_ctx *c, int d)
{
	app *a = c->a;
	int o = gm_owner(a);
	sysview *v = &a->view[o];

	v->dmode = (v->dmode + d + DMODE_COUNT) % DMODE_COUNT;
	display_save(a, o);
	overlay_reload(a, o);                  /* that mode's overlay, if any */
	plat_resident_line("SETDISPLAY\tmode=%s", DMODES[v->dmode].name);
	plat_resident_sync_rect(1000);
	gm_apply_overlay(c);
	if (look_console_of(a->sys.systems[o].tag) != LOOK_NONE && v->look.on) gm_apply_shader(c);
	else gm_load_bg(c);
}

/* And the overlay: to Diatom for when the game goes on, and to the backdrop. */
static void gm_apply_overlay(gm_ctx *c)
{
	app *a = c->a;

	plat_resident_line("SETBEZEL\tpath=%s",
	                   overlay_write_dtov(a, gm_owner(a), LOOK_DTOV_PATH) ? LOOK_DTOV_PATH : "");
	gm_load_overlay(c);
}

static int gm_menu_build(void *ctx, menu_row *rows, int max,
                         const char **heading)
{
	gm_ctx *c = ctx;

	(void)max;
	*heading = NULL;       /* no title: the game behind it is the title */
	/* "Saved" for a moment after Take Screenshot */
	if (c->shot_said && SDL_GetTicks() - c->shot_said > 1500) c->shot_said = 0;
	return gm_build(c->a, rows, &c->bufs, c->shots,
	                c->shot_said ? (c->shot_ok ? "Saved" : "Couldn't save") : NULL);
}

/* The paused game, not the shelf. Diatom reports its rect with every DISPLAY
 * message, so the preview lands where the game actually is. */
static void gm_backdrop(app *a, void *ctx)
{
	gm_ctx *c = ctx;

	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 255);
	SDL_RenderClear(a->r);
	draw_paused_frame(a, c->bg);
	if (c->ov) draw_overlay_around(a, c->ov);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	/* Y held: the game as it will look, undimmed (menu_style.peek). */
	if (a->in.down[IN_Y]) return;
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
	SDL_RenderFillRect(a->r, NULL);
	/* The low-battery disc, as on the shelf: this menu is drawn by this
	 * process, so it is drawn here; in the game Diatom draws it. This loop
	 * draws every frame, so the blink needs nothing more. */
}

/* Power here stops the GAME. The device is not going anywhere, and the wait
 * loop under this menu has to be told or it goes straight back to waiting on
 * a game nobody is running. */
static menu_result gm_power(app *a, void *ctx)
{
	(void)a; (void)ctx;
	plat_note_power_pressed();
	plat_resident_line("STOP");
	return MENU_DONE;
}

static menu_result gm_key(app *a, void *ctx, in_button key, int sel)
{
	gm_ctx *c = ctx;

	/* ChaOS. Applied to the paused game at once, not on resume: the point
	 * of these rows being here is judging them against the game. Left and
	 * right turn the shader on and off; A opens its settings. The overlay
	 * goes round on left, right and A. */
	if (sel == GM_SHADER && (key == IN_LEFT || key == IN_RIGHT)) {
		int o = gm_owner(a);

		a->view[o].look.on = !a->view[o].look.on;
		look_save(a, o);
		gm_apply_shader(c);
		return MENU_STAY;
	}
	if (sel == GM_DISPLAY && (key == IN_LEFT || key == IN_RIGHT || key == IN_ACCEPT)) {
		gm_cycle_display(c, key == IN_LEFT ? -1 : 1);
		return MENU_STAY;
	}
	if (sel == GM_OVERLAY && (key == IN_LEFT || key == IN_RIGHT || key == IN_ACCEPT)) {
		overlay_cycle(a, gm_owner(a), key == IN_LEFT ? -1 : 1);
		gm_apply_overlay(c);
		return MENU_STAY;
	}
	if (key != IN_ACCEPT) return MENU_STAY;

	switch ((gm_row)sel) {
	case GM_CONTINUE:
		c->resume = true;
		return MENU_DONE;
	case GM_SAVE:
	case GM_LOAD: {
		int slot = slot_strip(a, c->bg, sel == GM_SAVE);

		if (slot) {
			sysview *sv = &a->view[a->sys_cursor];
			int o = shelf_owner(a, a->sys_cursor, sv->cursor);   /* see slot_strip */
			char sp[LIB_PATH * 2], pp[LIB_PATH * 2];

			slot_state_path(a, o, &sv->list.items[sv->cursor],
			                slot, sp, sizeof sp);
			if (sel == GM_SAVE) {
				plat_resident_line("SAVE\tpath=%s", sp);
				/* The paused frame is what the save holds, and Diatom already
				 * wrote it as the pause preview: copy it beside the state so
				 * the strip can show what is inside the slot. */
				slot_preview_path(a, o, &sv->list.items[sv->cursor],
				                  slot, pp, sizeof pp);
				copy_file(plat_resident_last_preview(), pp);
			} else {
				plat_resident_line("LOAD\tpath=%s", sp);
			}
			c->resume = true;
			return MENU_DONE;
		}
		/* Backed out: fall through to the menu, still paused. */
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		break;
	}
	case GM_SHOT:
		/* What the screen showed as the pause began: the game with its
		 * shader and overlay. The bare frame only when Diatom sent no
		 * panel capture (an older Diatom). */
		{
			const char *g = plat_resident_last_glass();
			c->shot_ok = shot_keep(g && *g ? g : plat_resident_last_preview());
		}
		c->shot_said = SDL_GetTicks();
		c->shots = gm_count_shots();
		break;
	case GM_SHOTS:
		shots_screen(a);
		c->shots = gm_count_shots();
		break;
	case GM_SHADER:
		shader_screen(a, gm_owner(a), c);
		break;
	case GM_BUTTONS: {
		sysview *sv = &a->view[a->sys_cursor];
		console_controls_screen(a, gm_owner(a), sv->list.items[sv->cursor].file, c, &c->st);
		break;
	}
	case GM_CHEEVOS:
		cheevos_screen(a, c->bg, false);
		break;
	case GM_RESET:
		plat_resident_line("RESET");
		c->resume = true;
		return MENU_DONE;
	case GM_QUIT:
		/* The wait loop carries on until EXIT arrives; quitting is asking,
		 * not tearing down. */
		plat_resident_line("STOP");
		return MENU_DONE;
	default: break;
	}

	/* A nested screen may have been the one that took the power press or ran
	 * the clock out - the slot strip and the achievements list both stop the
	 * game and close themselves, which would leave this menu drawn over a game
	 * that had already been told to stop. Only those two set the flag, and
	 * both are reached from right here, so this is where it is asked. */
	if (plat_run_power_pressed()) return MENU_DONE;
	return MENU_STAY;
}

/* ---- ChaOS: a console's shader settings -----------------------------------
 *
 * The shader's own page: the switch, which shader it is (only the one made
 * for this console), then for PT, Game System and a row for each group of
 * settings, which opens that group's page; for crt-lottes-fast and lcd3x,
 * which have few, the settings themselves; and Reset. A page has only the
 * rows that matter with the settings as they are: no grid rows with LCD Dot
 * chosen, nothing about glare with it off, no corner with a flat screen.
 *
 * Left and right change a value; held, a number speeds up. A on a choice
 * moves it on, as on every other menu. From the in-game menu each change is
 * applied to the paused game at once and shown behind the panel, and holding
 * Y hides the panel to see it. Saved per console as it changes. */
enum { SH_SWITCH, SH_NAME, SH_SYSTEM, SH_GROUP, SH_RESET = SH_GROUP + LG_COUNT, SH_NONE };

typedef struct {
	app        *a;
	int         sys;
	gm_ctx     *game;          /* NULL from the shelf */
	int         group;         /* -1: the shader's page */
	int         ids[MENU_RUN_ROWS];   /* per built row: SH_* or a look_param */
	char        vals[MENU_RUN_ROWS][32];
	char        heading[96];
	menu_style  st;
	/* Held left or right: how long, to speed a number up. */
	int         run, last_sel;
	in_button   last_key;
	Uint32      last_ms;
} shader_ctx;

static int shader_speed_rows(shader_ctx *c, menu_row *rows, int n);

static int shader_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	shader_ctx *c = ctx;
	look_settings *ls = &c->a->view[c->sys].look;
	look_console lc = look_console_of(c->a->sys.systems[c->sys].tag);
	look_shader own = look_shader_of(lc);
	look_shader sh;
	int n = 0, g;

	(void)max;
	/* PT's Pixel Effect only at Integer, where it is drawn */
	ls->any_size = c->a->view[c->sys].dmode != 0;
	/* a Game Boy console drawn with lcd3x has lcd3x's page */
	sh = look_active_shader(lc, ls);
	*heading = c->heading;
	if (c->group < 0) {
		snprintf(c->heading, sizeof c->heading, "%s Shader", c->a->sys.systems[c->sys].name);
		c->ids[n] = SH_SWITCH;
		rows[n++] = (menu_row){ "Shader", ls->on ? "On" : "Off", true, .toggle = true };
		if (ls->on) {
			/* A CRT console chooses between two, and so does a Game Boy
			 * console; the others have one. */
			if (own == LS_CRT || own == LS_PT) {
				c->ids[n] = SH_NONE + 1 + (int)(own == LS_CRT ? LP_CRT_STYLE : LP_PT_STYLE);
				rows[n++] = (menu_row){ "Style", look_style_name(lc, ls), true, .cycles = true };
			} else {
				c->ids[n] = SH_NAME;
				rows[n++] = (menu_row){ "Style", look_style_name(lc, ls), false };
			}
			n = shader_speed_rows(c, rows, n);
			if (sh != LS_PT) {
				look_param ps[LP_COUNT];
				int k, np = look_group_rows(ls, sh, LG_PAGE, ps);

				c->ids[n] = SH_NONE; rows[n++] = MENU_RULE;
				for (k = 0; k < np; k++) {
					c->ids[n] = SH_NONE + 1 + (int)ps[k];
					look_format(ls, ps[k], c->vals[n], sizeof c->vals[n]);
					rows[n] = (menu_row){ look_params[ps[k]].label, c->vals[n], true,
			                     .toggle = look_param_is_switch(ps[k]),
			                     .cycles = !look_param_is_switch(ps[k]) };
					n++;
				}
			}
			if (sh == LS_PT) {
				c->ids[n] = SH_SYSTEM;
				look_format(ls, LP_GAME_SYSTEM, c->vals[n], sizeof c->vals[n]);
				rows[n] = (menu_row){ "Game System", c->vals[n], true, .cycles = true };
				n++;
				c->ids[n] = SH_NONE; rows[n++] = MENU_RULE;
			}
			for (g = 0; sh == LS_PT && g < LG_COUNT; g++) {
				static const look_param SUMMARY[LG_COUNT] = {
					LP_BACKING_COLOR, LP_PIXEL_EFFECT, LP_SHADOW_OPACITY, LP_GLARE_AND_SHEEN
				};
				/* Pixels holds the Pixel Effect alone, which is drawn
				 * only at Integer */
				if (g == LG_PIXELS && ls->any_size) continue;
				c->ids[n] = SH_GROUP + g;
				look_format(ls, SUMMARY[g], c->vals[n], sizeof c->vals[n]);
				rows[n] = (menu_row){ look_group_label((look_group)g), c->vals[n], true, true };
				n++;
			}
			c->ids[n] = SH_NONE; rows[n++] = MENU_RULE;
			c->ids[n] = SH_RESET;
			rows[n++] = (menu_row){ "Reset to Recommended", NULL, true };
		}
	} else {
		look_param ps[LP_COUNT];
		int k, np = look_group_rows(ls, sh, (look_group)c->group, ps);

		snprintf(c->heading, sizeof c->heading, "%s", look_group_label((look_group)c->group));
		for (k = 0; k < np; k++) {
			c->ids[n] = SH_NONE + 1 + (int)ps[k];
			look_format(ls, ps[k], c->vals[n], sizeof c->vals[n]);
			rows[n] = (menu_row){ look_params[ps[k]].label, c->vals[n], true,
			                     .toggle = look_param_is_switch(ps[k]),
			                     .cycles = !look_param_is_switch(ps[k]) };
			n++;
		}
	}
	if (c->game) {
		c->ids[n] = SH_NONE;
		rows[n++] = MENU_NOTE("Hold " CTL_PEEK_NAME " to see the game");
	}
	return n;
}

/* ChaOS's speed check, in a game: whether it keeps its speed with the shader
 * as set, from what Diatom measured (plat_resident_shade_speed). The whole
 * frame against the console's frame time decides; the shader's own share is
 * beside it, so a slow core is not taken for a slow shader. Just after a
 * change, before the game has run with it, Diatom's figures are an estimate,
 * and say so. */
static int shader_speed_rows(shader_ctx *c, menu_row *rows, int n)
{
	plat_shade_speed sp;
	char *v;

	if (!c->game || !plat_resident_shade_speed(&sp)) return n;
	v = c->vals[n];
	if (sp.busy_ms < 0)
		snprintf(v, sizeof c->vals[n], "%s", "Unknown");
	else
		snprintf(v, sizeof c->vals[n], "%s%s",
		         sp.busy_ms <= sp.frame_ms * 0.85 ? "Full Speed"
		         : sp.busy_ms <= sp.frame_ms ? "Near the Limit" : "Too Slow",
		         sp.measured ? "" : " (est.)");
	c->ids[n] = SH_NONE;
	rows[n++] = (menu_row){ "Speed", v, false };
	v = c->vals[n];
	snprintf(v, sizeof c->vals[n], "%.1f ms", sp.shade_ms);
	c->ids[n] = SH_NONE;
	rows[n++] = (menu_row){ "Shader Time", v, false };
	if (sp.busy_ms >= 0) {
		v = c->vals[n];
		snprintf(v, sizeof c->vals[n], "%.1f of %.1f ms", sp.busy_ms, sp.frame_ms);
		c->ids[n] = SH_NONE;
		rows[n++] = (menu_row){ "Frame Time", v, false };
	}
	return n;
}

/* Kept, and in a game shown at once. */
static void shader_changed(shader_ctx *c)
{
	look_save(c->a, c->sys);
	if (c->game) gm_apply_shader(c->game);
}

static void shader_backdrop(app *a, void *ctx)
{
	shader_ctx *c = ctx;
	gm_backdrop(a, c->game);
}

static menu_result shader_key(app *a, void *ctx, in_button key, int sel)
{
	shader_ctx *c = ctx;
	look_settings *ls = &a->view[c->sys].look;
	int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;
	int id = sel >= 0 && sel < MENU_RUN_ROWS ? c->ids[sel] : SH_NONE;

	if (id == SH_SWITCH && (d || key == IN_ACCEPT)) {
		ls->on = !ls->on;
		shader_changed(c);
	} else if (id == SH_SYSTEM && (d || key == IN_ACCEPT)) {
		if (look_step(ls, LP_GAME_SYSTEM, d ? d : 1, 1)) shader_changed(c);
	} else if (id >= SH_GROUP && id < SH_GROUP + LG_COUNT && key == IN_ACCEPT) {
		c->group = id - SH_GROUP;
		menu_run(a, &c->st, shader_build, shader_key, c);
		c->group = -1;
	} else if (id == SH_RESET && key == IN_ACCEPT) {
		look_recommended(look_console_of(a->sys.systems[c->sys].tag), ls);
		shader_changed(c);
	} else if (id > SH_NONE && (d || key == IN_ACCEPT)) {
		look_param p = (look_param)(id - SH_NONE - 1);
		Uint32 now = SDL_GetTicks();
		int mult;

		/* Held: a step at a time for the first few, then five, then ten,
		 * so a 0-100% setting crosses in about two seconds. A pause or
		 * another row starts again from one. */
		if (d && sel == c->last_sel && key == c->last_key && now - c->last_ms < 300)
			c->run++;
		else
			c->run = 0;
		c->last_sel = sel;
		c->last_key = key;
		c->last_ms = now;
		mult = c->run >= 20 ? 10 : c->run >= 8 ? 5 : 1;
		if (look_step(ls, p, d ? d : 1, d ? mult : 1)) shader_changed(c);
	}
	return MENU_STAY;
}

/* The shader pages' width: the menus' own, widened if a row of these would
 * not fit at its longest - "Game System  < Game Boy Color >" - so a value is
 * never cut short and the panel does not change size as one is cycled. */
static int shader_width(app *a, int sys, int base)
{
	look_console lc = look_console_of(a->sys.systems[sys].tag);
	char head[96];
	int i, k, w = base;

	for (i = 0; i < LP_COUNT; i++) {
		const look_param_info *in = &look_params[i];
		if (!in->words || !look_param_kept(lc, (look_param)i)) continue;
		for (k = 0; in->words[k]; k++) {
			menu_row r = { in->label, in->words[k], true, .cycles = true };
			int rw = menu_measure(&r, 1, NULL);
			if (rw > w) w = rw;
		}
	}
	snprintf(head, sizeof head, "%s Shader", a->sys.systems[sys].name);
	k = menu_measure(NULL, 0, head);
	return k > w ? k : w;
}

static void shader_screen(app *a, int sys, void *game)
{
	shader_ctx *c = calloc(1, sizeof *c);

	if (!c) return;
	c->a = a;
	c->sys = sys;
	c->game = game;
	c->group = -1;
	c->last_sel = -1;
	c->last_key = IN_NONE;
	if (c->game) {
		c->st = c->game->st;           /* its width, tint, power rule and peek */
		c->st.backdrop = shader_backdrop;
		c->st.root = false;
		c->st.fixed_w = shader_width(a, sys, c->st.fixed_w);
	} else {
		c->st.fixed_w = shader_width(a, sys, menu_shelf_width(a));
		c->st.follow_tint = true;
	}
	menu_run(a, &c->st, shader_build, shader_key, c);
	free(c);
}

/* For a shot: one frame of the shader's page (group -1) or a group's, over
 * the shelf, with row `sel` selected. */
static bool shot_speed;          /* --speed: the in-game page, with figures */

static void shader_shot(app *a, int group, int sel)
{
	shader_ctx c;
	menu_row rows[MENU_RUN_ROWS];
	const char *heading;
	int n;

	memset(&c, 0, sizeof c);
	c.a = a;
	c.sys = a->sys_cursor;
	c.group = group;
	{
		static gm_ctx g;
		if (shot_speed) c.game = &g;   /* only its being there is asked */
	}
	n = shader_build(&c, rows, MENU_RUN_ROWS, &heading);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
	SDL_RenderFillRect(a->r, NULL);
	menu_draw(a, heading, rows, n, sel, shader_width(a, c.sys, menu_shelf_width(a)), a->tint);
}

static void game_menu(app *a)
{
	gm_ctx c = { 0 };

	c.a = a;
	c.shots = gm_count_shots();
	/* The frame as the shader drew it, when it did: what the player was
	 * looking at. And the overlay over it. */
	gm_load_bg(&c);
	gm_load_overlay(&c);
	c.st.fixed_w     = gm_width(a);
	c.st.peek        = true;
	c.st.root        = true;
	c.st.follow_tint = true;
	c.st.backdrop    = gm_backdrop;
	c.st.on_power    = gm_power;

	/* B and MENU are the runner's, and both mean Continue here. The runner has
	 * to be the one to say so: it flushes the input on its way out, so asking
	 * a->in afterwards would always say no button was pressed. MENU from a
	 * screen this menu opened - the save slots, Achievements - comes back as
	 * MENU_LEFT_BACK too, so it is Continue from there as well. */
	g_menu_closing = false;
	if (menu_run(a, &c.st, gm_menu_build, gm_key, &c) == MENU_LEFT_BACK)
		c.resume = true;
	g_menu_closing = false;

	if (c.bg) SDL_DestroyTexture(c.bg);
	if (c.ov) SDL_DestroyTexture(c.ov);

	/* Asked to quit with the menu open. Leaving here without saying anything
	 * strands the game: it is paused, waiting on this socket, and the wait
	 * loop below would go straight back to waiting on IT. Each on the other,
	 * and neither reachable by a signal. Seen on the device 2026-08-29.
	 *
	 * STOP rather than RESUME - the process is going away, and a game that
	 * ends writes its state on the way out. */
	if (want_quit && !c.resume) plat_resident_line("STOP");

	if (c.resume) {
		/* Hand the pages back the way Diatom expects to find them.
		 *
		 * Diatom does not repaint the area outside its picture every frame -
		 * its pages start opaque black and it writes only the rect - which is
		 * sound while it owns the framebuffer and false the moment this
		 * process has drawn a full-screen menu into the same pages. At any
		 * display mode that does not fill the panel, resuming showed the game
		 * correctly sized with this menu still surrounding it, and flickering:
		 * Diatom cycles three pages and only the two this process presents
		 * into had been dirtied, so the border alternated menu, menu, black at
		 * the refresh rate.
		 *
		 * Twice because this process alternates two pages and one present only
		 * clears the one it lands on. Before RESUME and never after:
		 * afterwards Diatom is drawing, and this would be a second presenter. */
		present_black(a);
		present_black(a);
		/* SELECT in this menu opens Muse, so the music may have started or
		 * stopped while the game was paused. Said before the game moves, not
		 * a tick after it. */
		plat_resident_quiet(musec_playing());
		/* And the volume and the mute switch, both of which this menu had.
		 *
		 * On RESUME Diatom forgets its volume and reads it back from the
		 * codec. With the switch down in this menu that read finds the gain
		 * this side wrote for the cut and takes it for level 0, so putting
		 * the switch back up in the game brought the sound back at 0: silent
		 * until a volume key. Sending the level after RESUME, as every RUN
		 * does, leaves Diatom nothing to guess. The mute goes first, or the
		 * level would turn the sound back on with the switch still down. */
		plat_resident_line("SETMUTE\ton=%d", plat_muted() ? 1 : 0);
		plat_resident_line("RESUME");
		{
			int vol = plat_volume_get();

			if (vol >= 0)
				plat_resident_line("SETLEVEL\tkind=volume\tindex=%d\tcount=%d",
				                   vol, plat_volume_max() + 1);
		}
	}
	/* Nothing presents from here: the next frame on screen is the game's. */
}

/* Bring the resident emulator back.
 *
 * With one process per game a core that segfaults takes down that game and
 * nothing else. With a resident emulator it takes down the emulator, and every
 * launch for the rest of the session would fall back to the slow path -- until
 * the launcher itself exits and launch.sh starts a new one. So the launcher
 * starts one too, once the fallback game has given the display back. Never
 * while a game is running: two GL contexts and a launcher is one more than
 * this device should be asked for. */
static void respawn_resident(app *a)
{
	char elf[CFG_STR * 2], save[CFG_STR * 2], bios[CFG_STR * 2];
	char cores[CFG_STR * 2];
	char *argv[10];
	(void)a;

	if (plat_resident_ready()) return;

	/* The resident is Diatom. It maps every core in the directory it is given
	 * and keeps them, so the directory is the only thing handed over and
	 * nothing here changes when a system is added. */
	snprintf(elf, sizeof elf, "%s/diatom", P_ROOT);
	if (access(elf, X_OK) != 0) return;
	snprintf(save, sizeof save, "%s/Saves", P_CARD);
	snprintf(bios, sizeof bios, "%s/Bios", P_CARD);
	/* --cores here as well as in launch.sh. There are two places that start
	 * the resident emulator - the boot script and this - and a flag added to
	 * only one of them means every restart after the first silently loses it.
	 * That is exactly what happened: the log showed a premapped boot, then a
	 * dlopen on every launch once this path had restarted it. */
	snprintf(cores, sizeof cores, "%s/cores", P_ROOT);
	argv[0] = elf;
	argv[1] = (char *)"--socket";
	argv[2] = (char *)plat_resident_socket();
	argv[3] = (char *)"--cores";  argv[4] = cores;
	argv[5] = (char *)"--save";   argv[6] = save;
	argv[7] = (char *)"--system"; argv[8] = bios;
	argv[9] = NULL;
	fprintf(stderr, "resident emulator is gone, starting diatom\n");
	plat_spawn_detached(argv, NULL, P_ROOT);
}

static void launch_game(app *a);

/* ChaOS: a game first. Nothing of ChaOS's own runs while one does - an
 * update's download stops and its unpacking freezes, covers still to resize
 * wait - and all of it carries on once the game is over; the shelf's notice
 * says how far it has got (draw_upd_notice). Muse is the exception, by
 * design: music plays on through a game. */
static void launch(app *a)
{
	upd_hold(true);
	art_shrink_hold(true);
	launch_game(a);
	art_shrink_hold(false);
	upd_hold(false);
}

/* ChaOS: PortMaster, fetched the first time a port is started (ports.h).
 * True once it is on the card. */
static bool ports_setup_screen(app *a, const char *title, unsigned accent)
{
	char now[96];
	bool ok = false;

	if (!net_online()) {
		pick_say(a, title, "Ports need PortMaster:\nconnect to Wi-Fi to get it", accent);
		return false;
	}
	if (!confirm_panel(a, "PortMaster",
	                   "Ports run with PortMaster.\nDownload it now? (about 25 MB)",
	                   "Download"))
		return false;
	sync_abandon();
	if (!ports_setup_begin(P_ROMS)) {
		pick_say(a, title, ports_setup_problem(), accent);
		return false;
	}
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	for (;;) {
		ports_setup_state st = ports_setup_step();

		if (st == PS_DONE) { ok = true; break; }
		if (st == PS_FAILED) { pick_say(a, title, ports_setup_problem(), accent); break; }
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { ports_setup_cancel(); a->running = false; return false; }
		if (a->in.pressed[IN_POWER]) { ports_setup_cancel(); power_off(a); return false; }
		if (a->in.pressed[IN_BACK]) { ports_setup_cancel(); break; }
		if (in_repeat(&a->in, IN_VOLUP)) plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN)) plat_volume_nudge(-1);
		if (st == PS_DOWNLOAD)
			snprintf(now, sizeof now, "Downloading PortMaster\n%.1f MB",
			         ports_setup_bytes() / 1048576.0);
		else
			snprintf(now, sizeof now, "Unpacking PortMaster\nThis takes a minute");
		pick_frame(a, title, NULL, 0, 0, NULL, 0, 0, now, accent);
		SDL_Delay(30);
	}
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return ok;
}

/* ChaOS: a port off the Ports shelf. Not a game Diatom runs: a script, run
 * with bash through ChaOS/ports/run.sh, with the screen, the sound and the
 * memory handed over whole - the launcher lets go of its display as the
 * standalone emulator path does, and the idle resident emulator is ended and
 * started again after. */
static bool pm_launch_runtimes(app *a, const char *script, const char *title, unsigned accent);
static int pm_libs(app *a, const char *port_dir, const char *title, unsigned accent,
                   char *left, size_t ln);
static bool port_game_dir(const char *script, char *out, size_t n);

/* A port's libraries looked at before it starts (pm_libs), until once
 * nothing was missing: then a mark in .userdata/ports/checked says so and
 * it isn't looked at again, unless a run stops on a library */
static void port_checked_path(const char *romfile, char *out, size_t n)
{
	snprintf(out, n, "%s/ports/checked/%s", P_USERDATA, romfile);
}

static void port_libs_before(app *a, const char *script, const char *romfile,
                             const char *title, unsigned accent)
{
	char dir[LIB_PATH * 2], mark[LIB_PATH * 2], left[LIBPACK_NAME];

	port_checked_path(romfile, mark, sizeof mark);
	if (access(mark, F_OK) == 0 || !port_game_dir(script, dir, sizeof dir)) return;
	if (pm_libs(a, dir, title, accent, left, sizeof left) == 0) {
		char d[LIB_PATH];
		FILE *f;

		snprintf(d, sizeof d, "%s/ports/checked", P_USERDATA);
		mkdir(d, 0755);
		if ((f = fopen(mark, "w"))) fclose(f);
	}
}

/* ---- ChaOS: power during a port -------------------------------------------
 *
 * The port is frozen (plat_run_port) and this asks what now, in the menus'
 * own panel: Resume or Power Off. The port has the screen - the launcher let
 * its renderer go - so the panel is drawn by SDL's software renderer into a
 * picture of the frozen frame and written straight to the framebuffer page
 * on show, then the frame put back as it was before the port goes on. */
static const menu_row *port_power_rows(int *n)
{
	static menu_row rows[3];

	/* in the order plat_run_port reads the answer: 0 go on, 1 power off,
	 * 2 out to ChaOS - drawn Resume, Close Port, Power Off */
	rows[0] = (menu_row){ "Resume", NULL, true };
	rows[0].hint = "Back to the game, just as it was";
	rows[1] = (menu_row){ "Close Port", NULL, true };
	rows[1].hint = "Back to ChaOS. What the port hasn't saved is lost";
	rows[2] = (menu_row){ "Power Off", NULL, true };
	rows[2].hint = "Turn the Brick off. What the port hasn't saved is lost";
	*n = 3;
	return rows;
}

/* One frame of it, over whatever a->r holds: the device's and --port-power's */
static void port_power_draw(app *a, int sel)
{
	int n;
	const menu_row *rows = port_power_rows(&n);

	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 150);
	SDL_RenderFillRect(a->r, NULL);
	menu_draw(a, "Power", rows, n, sel, 0, MENU_ACCENT);
}

static int port_power_prompt(void *ctx)
{
#ifdef __linux__
	app *a = ctx;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	SDL_Surface *bg = NULL, *fr = NULL;
	SDL_Renderer *sw = NULL, *was = a->r;
	uint8_t *mem = MAP_FAILED, *page, *save = NULL;
	size_t len = 0;
	int fd, sel = 0, said = -1, y, drawn = -1;
	in_state in;

	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0) return -1;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0 ||
	    var.bits_per_pixel != 32 || var.xres != TORTOS_SCREEN_W || var.yres != TORTOS_SCREEN_H)
		goto out;
	len = (size_t)fix.line_length * var.yres_virtual;
	mem = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mem == MAP_FAILED) goto out;
	/* the page on show now, as the port last flipped it */
	page = mem + (size_t)var.yoffset * fix.line_length + (size_t)var.xoffset * 4;
	if ((size_t)(page - mem) + (size_t)fix.line_length * var.yres > len) goto out;
	save = malloc((size_t)fix.line_length * var.yres);
	bg = SDL_CreateRGBSurfaceWithFormat(0, TORTOS_SCREEN_W, TORTOS_SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
	fr = SDL_CreateRGBSurfaceWithFormat(0, TORTOS_SCREEN_W, TORTOS_SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!save || !bg || !fr) goto out;
	memcpy(save, page, (size_t)fix.line_length * var.yres);
	for (y = 0; y < TORTOS_SCREEN_H; y++)
		memcpy((uint8_t *)bg->pixels + (size_t)y * bg->pitch, page + (size_t)y * fix.line_length,
		       (size_t)TORTOS_SCREEN_W * 4);
	sw = SDL_CreateSoftwareRenderer(fr);
	if (!sw) goto out;
	a->r = sw;
	if (!ui_init(sw, P_FONT)) { ui_quit(); goto out; }
	a->menu_w = 0;
	fprintf(stderr, "port: power - frozen, asking\n");

	memset(&in, 0, sizeof in);
	plat_backlight(true);
	plat_child_grab(true);
	for (;;) {
		bool down;

		if (drawn != sel) {
			SDL_BlitSurface(bg, NULL, fr, NULL);
			port_power_draw(a, sel);
			SDL_RenderFlush(sw);
			for (y = 0; y < TORTOS_SCREEN_H; y++) {
				uint32_t *d = (uint32_t *)(page + (size_t)y * fix.line_length);
				const uint32_t *p = (const uint32_t *)((uint8_t *)fr->pixels + (size_t)y * fr->pitch);
				int x;

				/* opaque: the display engine reads the top byte as alpha */
				for (x = 0; x < TORTOS_SCREEN_W; x++) d[x] = p[x] | 0xff000000u;
			}
			drawn = sel;
		}
		usleep(16 * 1000);
		plat_child_input(&in);
		if (plat_power_event(&down)) { said = 0; break; }   /* again: back to it */
		if (in_repeat(&in, IN_DOWN)) sel = (sel + 1) % 3;
		if (in_repeat(&in, IN_UP)) sel = (sel + 2) % 3;
		if (in_repeat(&in, IN_VOLUP)) plat_volume_nudge(+1);
		if (in_repeat(&in, IN_VOLDN)) plat_volume_nudge(-1);
		if (in.pressed[IN_BACK]) { said = 0; break; }
		/* Resume 0, Close Port 2, Power Off 1 */
		if (in.pressed[IN_ACCEPT]) { said = sel == 0 ? 0 : sel == 1 ? 2 : 1; break; }
	}
	/* the button that answered, let go before the port has the pad back */
	while (in.down[IN_ACCEPT] || in.down[IN_BACK]) { usleep(16 * 1000); plat_child_input(&in); }
	plat_child_grab(false);
	fprintf(stderr, "port: power - %s\n", said == 1 ? "power off" : said == 2 ? "close" : "resume");
	/* the frame back as it was, for a port that only draws when it changes */
	if (said == 0) memcpy(page, save, (size_t)fix.line_length * var.yres);
	ui_quit();
out:
	a->r = was;
	if (sw) SDL_DestroyRenderer(sw);
	if (bg) SDL_FreeSurface(bg);
	if (fr) SDL_FreeSurface(fr);
	free(save);
	if (mem != MAP_FAILED) munmap(mem, len);
	close(fd);
	return said;
#else
	(void)ctx;
	return -1;
#endif
}

/* The library a port's log says it couldn't load: the loader's own words,
 * "error while loading shared libraries: libX.so.N: cannot open..." */
static void port_missing_lib(const char *log, char *out, size_t n)
{
	FILE *f = fopen(log, "r");
	char line[1024];
	static const char key[] = "error while loading shared libraries: ";

	out[0] = '\0';
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		char *p = strstr(line, key);

		if (p) {
			p += sizeof key - 1;
			snprintf(out, n, "%.*s", (int)strcspn(p, ": \r\n"), p);
		}
	}
	fclose(f);
}

/* A port's own folder, from its script: the GAMEDIR= line most of them
 * carry ("/$directory/ports/doomengines"), as the folder on the card */
static bool port_game_dir(const char *script, char *out, size_t n)
{
	FILE *f = fopen(script, "r");
	char line[512];
	bool ok = false;

	if (!f) return false;
	while (!ok && fgets(line, sizeof line, f)) {
		char *p = strstr(line, "GAMEDIR=");
		char *q;

		if (!p || (p != line && p[-1] != ' ' && p[-1] != '\t')) continue;
		q = strstr(p, "/ports/");
		if (!q) continue;
		q += 7;
		snprintf(out, n, "%s/Ports/%.*s", P_ROMS, (int)strcspn(q, "\"'/ \r\n"), q);
		ok = q[0] && q[0] != '"' && q[0] != '$';
	}
	fclose(f);
	return ok;
}

static void port_launch(app *a, int o, const char *romfile, const char *title)
{
	const system_cfg *s = &a->sys.systems[o];
	char script[LIB_PATH * 2], runner[CFG_STR * 2], why[96], ud[LIB_PATH + 32];
	char *argv[4];
	const char *envkv[2];
	bool music;
	int how = PORT_ENDED, rc;

	snprintf(script, sizeof script, "%s/%s/%s", P_ROMS, s->folder, romfile);
	snprintf(runner, sizeof runner, "%s/ports/run.sh", P_ROOT);
	if (access(runner, R_OK) != 0 || access("/bin/bash", X_OK) != 0) {
		pick_say(a, title, "Ports need ChaOS's full update", s->accent);
		return;
	}
	if (!ports_script_ok(script, why, sizeof why)) {
		pick_say(a, title, why, s->accent);
		return;
	}
	if (!ports_pm_ready(P_ROMS) && !ports_setup_screen(a, title, s->accent)) return;
	if (!a->running) return;
	/* A runtime it needs and hasn't got - a port put on the card some
	 * other way, or one PortMaster's own app installed - fetched first */
	if (!pm_launch_runtimes(a, romfile, title, s->accent) || !a->running) return;
	/* and any library it needs that the Brick hasn't, from ChaOS's pack */
	port_libs_before(a, script, romfile, title, s->accent);
	if (!a->running) return;

	remember_place(a);
	snprintf(ud, sizeof ud, "CHAOS_USERDATA=%s", P_USERDATA);
	envkv[0] = ud;
	envkv[1] = NULL;
	argv[0] = (char *)"/bin/sh";
	argv[1] = runner;
	argv[2] = script;
	argv[3] = NULL;

	/* Muse holds the sound card while it plays; the port needs it */
	music = musec_playing();
	if (music) musec_toggle();
	plat_resident_end();

	anim_launch(a, 190);
	free_all_textures(a);
	texload_fonts(false);
	ui_quit();
	plat_input_quit();
	plat_video_quit();

	fprintf(stderr, "port %s: starting\n", romfile);
	rc = plat_run_port(argv, envkv, NULL, &how, port_power_prompt, a);
	fprintf(stderr, "port %s exited %d (%s)\n", romfile, rc,
	        how == PORT_POWER ? "power held" : how == PORT_QUIT ? "select+start" : "itself");
	/* and in the port's own log, where whoever reads it about a port that
	 * stopped looks first */
	{
		char lp[LIB_PATH + 48];
		FILE *lf;

		snprintf(lp, sizeof lp, "%s/ports/last.log", P_USERDATA);
		if ((lf = fopen(lp, "a"))) {
			fprintf(lf, "ChaOS: the port ended %s, status %d\n",
			        how == PORT_POWER ? "on power held" : how == PORT_QUIT ? "on Select+Start"
			                                                     : "by itself", rc);
			fclose(lf);
		}
		/* and a copy kept per port, ports/logs/<script>.log: last.log is
		 * only ever the latest, and testing a few in a row lost the rest */
		{
			char dir[LIB_PATH + 16], to[LIB_PATH * 2], buf[8192];
			FILE *in, *out;
			size_t got;

			snprintf(dir, sizeof dir, "%s/ports/logs", P_USERDATA);
			mkdir(dir, 0755);
			snprintf(to, sizeof to, "%s/%.*s.log", dir,
			         (int)(strlen(romfile) > 3 ? strlen(romfile) - 3 : strlen(romfile)), romfile);
			if ((in = fopen(lp, "rb"))) {
				if ((out = fopen(to, "wb"))) {
					while ((got = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, got, out);
					fclose(out);
				}
				fclose(in);
			}
		}
	}

	if (!plat_video_init() || !plat_input_init()) { a->running = false; return; }
	a->r = plat_renderer();
	ui_init(a->r, P_FONT);
	texload_fonts(true);
	a->menu_w = 0;
	prime_sys_window(a);
	prime_window(a, a->sys_cursor);
	respawn_resident(a);
	if (music) musec_toggle();

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	if (access(TORTOS_POWEROFF_FLAG, F_OK) == 0) { a->running = false; return; }
	/* Power held through the port: off, as asked */
	if (how == PORT_POWER) { power_off(a); return; }
	/* A port that stopped on a library the Brick hasn't got says so,
	 * rather than dropping back to the shelf without a word. Looked for in
	 * its own log too, where most ports send their output. */
	if (how == PORT_ENDED) {
		char lp[LIB_PATH + 48], missing[96] = "";

		snprintf(lp, sizeof lp, "%s/ports/last.log", P_USERDATA);
		port_missing_lib(lp, missing, sizeof missing);
		if (!missing[0]) {
			char dir[LIB_PATH * 2], gl[LIB_PATH * 2 + 16];

			if (port_game_dir(script, dir, sizeof dir)) {
				snprintf(gl, sizeof gl, "%s/log.txt", dir);
				port_missing_lib(gl, missing, sizeof missing);
			}
		}
		if (missing[0]) {
			char msg[200], mark[LIB_PATH * 2], dir[LIB_PATH * 2], left[LIBPACK_NAME] = "";
			int still = -1;

			fprintf(stderr, "port %s: needs %s, which the Brick hasn't got\n", romfile, missing);
			/* looked at again, now and next time: the pack may have it */
			port_checked_path(romfile, mark, sizeof mark);
			unlink(mark);
			if (port_game_dir(script, dir, sizeof dir))
				still = pm_libs(a, dir, title, s->accent, left, sizeof left);
			if (still == 0)
				snprintf(msg, sizeof msg, "Got what it was missing:\nstart it again");
			else
				snprintf(msg, sizeof msg, "This port needs %s,\nwhich ChaOS doesn't have yet",
				         left[0] ? left : missing);
			pick_say(a, title, msg, s->accent);
		}
	}
	render(a);
}

/* ---- ChaOS: PortMaster's catalog, browsed and installed ------------------
 *
 * PortMaster's own app is a Python program drawn in its own style; this is
 * the same catalog (src/pmcat.h) in ChaOS's menus. The Ports shelf's first
 * card opens it, and so does PortMaster in the ChaOS menu.
 *
 *   PortMaster          the sections, Genres, Installed, Updates, and
 *                       getting or updating PortMaster itself
 *   a list              titles on the left, the one under the cursor on the
 *                       right with its screenshot, fetched as you go
 *   a port              its picture, what it needs, its description, and
 *                       Install, Update or Remove
 *
 * Installing is what PortMaster does: the zip, checked against the
 * catalog's MD5 and unpacked over Roms/Ports (so a port's own files that
 * aren't in the zip - game files you copied in, saves - stay through an
 * update), then any runtime it needs into PortMaster/libs, unpacked once
 * there (res/ports/bin/mount), and its screenshot as its box art. */


static pm_catalog g_pmc;
static bool       g_pmc_loaded;
static bool       g_pm_rtr_only;   /* "Ready to Run Only" */

static void rescan_all(app *a);    /* with scan_all, below */


static void pm_dir(char *out, size_t n) { snprintf(out, n, "%s/ports", P_USERDATA); }
static void pm_cat_path(char *out, size_t n) { snprintf(out, n, "%s/ports/ports.json", P_USERDATA); }
static void pm_ports_dir(char *out, size_t n) { snprintf(out, n, "%s/Ports", P_ROMS); }

/* What an install unpacked (pmcat_manifest_remove): .userdata, so it
 * outlives anything done to the card's Ports folder */
static void pm_manifest_path(const char *zip, char *out, size_t n)
{
	snprintf(out, n, "%s/ports/installed/%.*s.txt", P_USERDATA,
	         (int)(strlen(zip) > 4 ? strlen(zip) - 4 : strlen(zip)), zip);
}

static void pm_img_path(const pm_port *p, char *out, size_t n)
{
	char url[400], file[96];

	out[0] = '\0';
	if (pmcat_shot(p, url, sizeof url, file, sizeof file))
		snprintf(out, n, "%s/ports/img/%s", P_USERDATA, file);
}

static bool pm_reload(void)
{
	char cat[LIB_PATH], ports[LIB_PATH];

	if (g_pmc_loaded) pmcat_free(&g_pmc);
	pm_cat_path(cat, sizeof cat);
	pm_ports_dir(ports, sizeof ports);
	g_pmc_loaded = pmcat_load(&g_pmc, cat, ports, NULL);
	if (g_pmc_loaded) fprintf(stderr, "portmaster: catalog of %d ports\n", g_pmc.n);
	return g_pmc_loaded;
}

/* How old the catalog on the card is, in seconds; -1 when there is none */
static long pm_cat_age(void)
{
	char cat[LIB_PATH];
	struct stat st;

	pm_cat_path(cat, sizeof cat);
	if (stat(cat, &st) != 0 || st.st_size == 0) return -1;
	return (long)(time(NULL) - st.st_mtime);
}

/* A child, waited on with the screen up: `what` over a progress line from
 * `file` growing toward `total` bytes (either may be absent). B gives up,
 * which kills it. Its exit status, or -1 for given up or killed. */
static int pm_wait(app *a, pid_t pid, const char *title, const char *what,
                   const char *file, long total, unsigned accent, bool spawned)
{
	char now[160];
	int st = 0, rc = -1;

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	for (;;) {
		pid_t r = waitpid(pid, &st, WNOHANG);

		if (r == pid) { rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1; break; }
		if (r < 0 && errno != EINTR) break;
		plat_input_poll(&a->in);
		if (a->in.quit_requested || a->in.pressed[IN_POWER] || a->in.pressed[IN_BACK]) {
			kill(pid, SIGTERM);
			kill(-pid, SIGTERM);
			waitpid(pid, NULL, 0);
			if (a->in.quit_requested) a->running = false;
			else if (a->in.pressed[IN_POWER]) power_off(a);
			rc = -1;
			break;
		}
		if (in_repeat(&a->in, IN_VOLUP)) plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN)) plat_volume_nudge(-1);
		if (file) {
			struct stat fs;
			double got = stat(file, &fs) == 0 ? fs.st_size / 1048576.0 : 0.0;

			if (total > 0)
				snprintf(now, sizeof now, "%s\n%.1f of %.1f MB", what, got, total / 1048576.0);
			else
				snprintf(now, sizeof now, "%s\n%.1f MB", what, got);
		} else {
			snprintf(now, sizeof now, "%s", what);
		}
		pick_frame(a, title, NULL, 0, 0, NULL, 0, 0, now, accent);
		SDL_Delay(30);
	}
	if (spawned) net_spawn_done(pid);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return rc;
}

/* A download to `path`, through a temporary, with the panel up. `md5`, when
 * there is one, is checked after (in a child: a runtime runs to hundreds of
 * MB, and the panel keeps drawing while it is read). */
static bool pm_fetch(app *a, const char *title, const char *what, const char *url,
                     const char *path, long size, const char *md5, unsigned accent,
                     char *why, size_t wn)
{
	char part[LIB_PATH + 8];
	pid_t pid;
	int rc;

	snprintf(part, sizeof part, "%s.part", path);
	unlink(part);
	pid = net_get_spawn(url, part, 3600);
	if (pid <= 0) { snprintf(why, wn, "Couldn't start the download"); return false; }
	rc = pm_wait(a, pid, title, what, part, size, accent, true);
	if (rc != 0) {
		unlink(part);
		snprintf(why, wn, rc < 0 ? "Stopped" : "Couldn't download it:\ncheck Wi-Fi and try again");
		return false;
	}
	if (md5 && md5[0]) {
		pid = fork();
		if (pid == 0) {
			char got[40];

			_exit(ra_md5_file(part, got) && !strcasecmp(got, md5) ? 0 : 1);
		}
		if (pid < 0 || pm_wait(a, pid, title, "Checking the download", NULL, 0, accent, false) != 0) {
			unlink(part);
			snprintf(why, wn, "The download was damaged:\ntry again");
			fprintf(stderr, "portmaster: %s failed its MD5\n", url);
			return false;
		}
	}
	if (rename(part, path) != 0) {
		unlink(part);
		snprintf(why, wn, "Couldn't save it to the card");
		return false;
	}
	return true;
}

/* A program run to its end with the panel up: unzip, rm, the unpacker */
static int pm_run(app *a, const char *title, const char *what, unsigned accent,
                  const char *const argv[], const char *path_prefix)
{
	pid_t pid = fork();

	if (pid < 0) return -1;
	if (pid == 0) {
		int devnull = open("/dev/null", O_RDWR);

		setpgid(0, 0);
		if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); close(devnull); }
		if (path_prefix) {
			char path[LIB_PATH * 2];
			const char *old = getenv("PATH");

			snprintf(path, sizeof path, "%s:%s", path_prefix, old ? old : "/usr/bin:/bin");
			setenv("PATH", path, 1);
		}
		execv(argv[0], (char *const *)argv);
		_exit(127);
	}
	setpgid(pid, pid);
	return pm_wait(a, pid, title, what, NULL, 0, accent, false);
}

static const char *pm_unzip_bin(void)
{
	return access("/usr/bin/unzip", X_OK) == 0 ? "/usr/bin/unzip" : "/bin/unzip";
}

/* The catalog, fetched now. True if there is one to show after, new or not. */
static bool pm_catalog_fetch(app *a, unsigned accent)
{
	char dir[LIB_PATH], cat[LIB_PATH], why[96];

	pm_dir(dir, sizeof dir);
	mkdir(dir, 0755);
	pm_cat_path(cat, sizeof cat);
	if (!net_online()) {
		if (pm_cat_age() < 0) pick_say(a, "PortMaster", "Connect to Wi-Fi to see\nPortMaster's ports", accent);
		return pm_cat_age() >= 0;
	}
	sync_abandon();
	if (!pm_fetch(a, "PortMaster", "Getting the list of ports", PMCAT_URL, cat, 0, NULL,
	              accent, why, sizeof why)) {
		if (strcmp(why, "Stopped")) pick_say(a, "PortMaster", why, accent);
		return pm_cat_age() >= 0;
	}
	return true;
}

/* ---- a port's runtimes and art -------------------------------------------- */

/* What a runtime is called to a person: "godot_4.3.squashfs" is "Godot 4.3" */
static void pm_runtime_name(const char *file, char *out, size_t n)
{
	static const struct { const char *pre, *name; } k[] = {
		{ "frt_", "Godot (FRT) " }, { "godot", "Godot " }, { "dotnet", ".NET " },
		{ "mono", "Mono " }, { "zulu", "Java " }, { "weston", "Weston " },
		{ "gmtoolkit", "GameMaker toolkit" }, { "renpy", "Ren'Py " },
		{ "pyxel", "Pyxel " }, { "python", "Python " }, { "solarus", "Solarus " },
		{ "mesa", "Mesa " }, { "rlvm", "RLVM" }, { "qt6", "Qt " }, { "ags", "AGS " },
	};
	size_t i;

	for (i = 0; i < sizeof k / sizeof k[0]; i++) {
		size_t l = strlen(k[i].pre);

		if (!strncasecmp(file, k[i].pre, l)) {
			const char *v = file + l;
			char ver[24] = "";
			size_t j = 0;

			while (*v && (*v < '0' || *v > '9')) v++;
			while (v[j] && j + 1 < sizeof ver && ((v[j] >= '0' && v[j] <= '9') || v[j] == '.')) {
				ver[j] = v[j];
				j++;
			}
			while (j > 0 && ver[j - 1] == '.') ver[--j] = '\0';
			snprintf(out, n, "%s%s", k[i].name, k[i].name[strlen(k[i].name) - 1] == ' ' ? ver : "");
			return;
		}
	}
	snprintf(out, n, "%.*s", (int)strcspn(file, "."), file);
}

/* Every runtime the port needs, fetched into PortMaster/libs if it isn't
 * there, and unpacked. False, with why, if one couldn't be. */
static bool pm_runtimes(app *a, const pm_port *p, unsigned accent, char *why, size_t wn)
{
	char rt[8][64], libs[LIB_PATH], path[LIB_PATH * 2], url[400], md5[40], nice[64], what[128];
	char shim[LIB_PATH], bin[LIB_PATH];
	int n = pmcat_runtimes(p, rt, 8), i;
	long size;

	if (n == 0) return true;
	snprintf(libs, sizeof libs, "%s/Ports/PortMaster/libs", P_ROMS);
	mkdir(libs, 0755);
	snprintf(bin, sizeof bin, "%s/ports/bin", P_ROOT);
	snprintf(shim, sizeof shim, "%s/mount", bin);
	for (i = 0; i < n; i++) {
		struct stat st;

		pm_runtime_name(rt[i], nice, sizeof nice);
		snprintf(path, sizeof path, "%s/%s", libs, rt[i]);
		if (stat(path, &st) != 0 || st.st_size == 0) {
			if (!pmcat_runtime(&g_pmc, rt[i], url, sizeof url, md5, sizeof md5, &size)) {
				snprintf(why, wn, "It needs %s,\nwhich PortMaster doesn't list", nice);
				return false;
			}
			snprintf(what, sizeof what, "Getting %s, which it runs on", nice);
			if (!pm_fetch(a, p->title, what, url, path, size, md5, accent, why, wn)) return false;
			fprintf(stderr, "portmaster: runtime %s fetched\n", rt[i]);
		}
		/* Unpacked now rather than on the first start, which would be a
		 * long wait on a black screen */
		if (access(shim, R_OK) == 0) {
			const char *argv[] = { "/bin/sh", shim, "--chaos-prepare", path, NULL };

			snprintf(what, sizeof what, "Unpacking %s\nThis takes a minute", nice);
			if (pm_run(a, p->title, what, accent, argv, bin) != 0) {
				snprintf(why, wn, "Couldn't unpack %s", nice);
				return false;
			}
		}
	}
	return true;
}

/* ---- libraries a port needs that the Brick hasn't got --------------------- */

static void pm_lib_dir(char *out, size_t n) { snprintf(out, n, "%s/ports/lib", P_USERDATA); }

/* ChaOS's pack's list (libpack.h), fetched when there is none or it's a day
 * old, quietly: a port that needs nothing from it never waits on it. Its
 * text, or NULL. */
static char *pm_libpack_list(app *a, unsigned accent, size_t *len)
{
	char path[LIB_PATH], dir[LIB_PATH], why[96];
	struct stat st;
	FILE *f;
	char *buf = NULL;

	pm_dir(dir, sizeof dir);
	mkdir(dir, 0755);
	snprintf(path, sizeof path, "%s/libpack.json", dir);
	if ((stat(path, &st) != 0 || time(NULL) - st.st_mtime > 86400) && net_online())
		pm_fetch(a, "Ports", "Getting ChaOS's list of libraries", LIBPACK_LIST, path, 0, NULL,
		         accent, why, sizeof why);
	if (stat(path, &st) != 0 || st.st_size <= 0 || st.st_size > (1 << 20)) return NULL;
	if (!(f = fopen(path, "rb"))) return NULL;
	buf = malloc((size_t)st.st_size + 1);
	if (buf && fread(buf, 1, (size_t)st.st_size, f) != (size_t)st.st_size) { free(buf); buf = NULL; }
	fclose(f);
	if (buf) { buf[st.st_size] = '\0'; *len = (size_t)st.st_size; }
	return buf;
}

/* What `port_dir`'s programs need and nothing has, fetched from ChaOS's
 * pack into .userdata/ports/lib (run.sh puts it on their library path),
 * with what each fetched library needs in turn. Returns how many are still
 * missing after, naming the first in `left`. */
static int pm_libs(app *a, const char *port_dir, const char *title, unsigned accent,
                   char *left, size_t ln)
{
	char lib[LIB_PATH], want[24][LIBPACK_NAME], file[LIBPACK_NAME], md5[40];
	const char *have[5];
	char *list = NULL;
	size_t len = 0;
	int n, i, still = 0;

	left[0] = '\0';
	pm_lib_dir(lib, sizeof lib);
	have[0] = "/lib"; have[1] = "/usr/lib"; have[2] = "/usr/lib/chaos-ports"; have[3] = lib;
	have[4] = NULL;
	n = libpack_missing(port_dir, have, want, 24);
	if (n == 0) return 0;
	fprintf(stderr, "ports: %s needs %d librar%s the Brick hasn't got, %s first\n", port_dir, n,
	        n == 1 ? "y" : "ies", want[0]);
	mkdir(lib, 0755);
	list = pm_libpack_list(a, accent, &len);
	for (i = 0; i < n; i++) {
		char url[300], path[LIB_PATH * 2], what[128], why[160];
		long size;

		if (!list || !libpack_find(list, len, want[i], file, sizeof file, md5, sizeof md5, &size)) {
			if (!still++) snprintf(left, ln, "%s", want[i]);
			fprintf(stderr, "ports: %s isn't in ChaOS's pack\n", want[i]);
			continue;
		}
		snprintf(url, sizeof url, "%s%s", LIBPACK_BASE, file);
		snprintf(path, sizeof path, "%s/%s", lib, want[i]);
		snprintf(what, sizeof what, "Getting %s, which it needs", want[i]);
		if (!pm_fetch(a, title, what, url, path, size, md5, accent, why, sizeof why)) {
			if (!still++) snprintf(left, ln, "%s", want[i]);
			if (!strcmp(why, "Stopped")) break;
			continue;
		}
		fprintf(stderr, "ports: %s fetched\n", want[i]);
		/* and what that one needs, onto the end of the list */
		{
			char more[32][LIBPACK_NAME], so[LIBPACK_NAME];
			int nm = 0, k, j;

			libpack_elf(path, more, 32, &nm, so, sizeof so);
			for (k = 0; k < nm && n < 24; k++) {
				char p2[LIB_PATH * 2];
				bool had = false;
				const char *const *d;

				for (j = 0; j < n && !had; j++) had = !strcmp(want[j], more[k]);
				for (d = have; *d && !had; d++) {
					snprintf(p2, sizeof p2, "%s/%s", *d, more[k]);
					had = access(p2, F_OK) == 0;
				}
				if (!had) snprintf(want[n++], LIBPACK_NAME, "%s", more[k]);
			}
		}
	}
	free(list);
	return still;
}

/* Its screenshot as its box art on the Ports shelf */
static void pm_box_art(app *a, const pm_port *p, unsigned accent)
{
	char img[LIB_PATH], url[400], file[96], why[96], stem[LIB_NAME];
	size_t l;

	pm_img_path(p, img, sizeof img);
	if (!img[0] || !p->script[0]) return;
	if (access(img, R_OK) != 0 && net_online()) {
		char dir[LIB_PATH];

		snprintf(dir, sizeof dir, "%s/ports/img", P_USERDATA);
		mkdir(dir, 0755);
		pmcat_shot(p, url, sizeof url, file, sizeof file);
		if (!pm_fetch(a, p->title, "Getting its picture", url, img, 0, NULL, accent, why, sizeof why))
			return;
	}
	snprintf(stem, sizeof stem, "%s", p->script);
	l = strlen(stem);
	if (l > 3 && !strcasecmp(stem + l - 3, ".sh")) stem[l - 3] = '\0';
	{
		char media[LIB_PATH];

		snprintf(media, sizeof media, "%s/Ports/.media", P_ROMS);
		mkdir(media, 0755);
	}
	cover_save(a, img, "Ports", stem);
}

/* Before a port starts: the runtimes it needs, by PortMaster's catalog,
 * fetched and unpacked if they aren't there. True to go ahead - also when
 * there is no catalog to ask, or the port isn't in it, and its own script
 * says what's wrong. */
static bool pm_launch_runtimes(app *a, const char *script, const char *title, unsigned accent)
{
	char libs[LIB_PATH], rt[8][64], why[160], nice[64], msg[200];
	bool loaded_here = !g_pmc_loaded, ok = true, missing = false;
	int i, at = -1, n;

	if (pm_cat_age() < 0) return true;
	if (!g_pmc_loaded && !pm_reload()) return true;
	for (i = 0; i < g_pmc.n && at < 0; i++)
		if (!strcasecmp(g_pmc.p[i].script, script)) at = i;
	if (at < 0) goto done;
	n = pmcat_runtimes(&g_pmc.p[at], rt, 8);
	snprintf(libs, sizeof libs, "%s/Ports/PortMaster/libs", P_ROMS);
	for (i = 0; i < n && !missing; i++) {
		char path[LIB_PATH * 2], unpacked[LIB_PATH * 2];
		size_t l = strlen(rt[i]);

		snprintf(path, sizeof path, "%s/%s", libs, rt[i]);
		snprintf(unpacked, sizeof unpacked, "%s/.chaos/%.*s/.chaos-stamp", libs,
		         (int)(l > 9 ? l - 9 : l), rt[i]);
		if (access(path, R_OK) != 0 || access(unpacked, R_OK) != 0) {
			missing = true;
			pm_runtime_name(rt[i], nice, sizeof nice);
		}
	}
	if (!missing) goto done;
	if (!net_online()) {
		/* Only the download needs Wi-Fi: one already here is unpacked */
		bool have_all = true;

		for (i = 0; i < n; i++) {
			char path[LIB_PATH * 2];

			snprintf(path, sizeof path, "%s/%s", libs, rt[i]);
			if (access(path, R_OK) != 0) have_all = false;
		}
		if (!have_all) {
			snprintf(msg, sizeof msg, "It runs on %s:\nconnect to Wi-Fi once to get it", nice);
			pick_say(a, title, msg, accent);
			ok = false;
			goto done;
		}
	}
	fprintf(stderr, "port %s: getting its runtime (%s) first\n", script, nice);
	if (!pm_runtimes(a, &g_pmc.p[at], accent, why, sizeof why)) {
		if (strcmp(why, "Stopped")) pick_say(a, title, why, accent);
		ok = false;
	}
done:
	if (loaded_here && g_pmc_loaded) { pmcat_free(&g_pmc); g_pmc_loaded = false; }
	return ok;
}

/* ---- install, update, remove ---------------------------------------------- */

static bool pm_install(app *a, int idx, unsigned accent)
{
	const pm_port *p = &g_pmc.p[idx];
	char url[400], md5[40], zip[LIB_PATH], ports[LIB_PATH], why[160], what[96];
	bool update = (p->flags & PMF_INSTALLED) != 0;

	if (!net_online()) { pick_say(a, p->title, "Connect to Wi-Fi to install ports", accent); return false; }
	if (!pmcat_source(p, url, sizeof url, md5, sizeof md5)) {
		pick_say(a, p->title, "PortMaster has no download for this one", accent);
		return false;
	}
	/* PortMaster first: every port's script runs through it */
	if (!ports_pm_ready(P_ROMS) && !ports_setup_screen(a, p->title, accent)) return false;
	if (!a->running) return false;
	sync_abandon();

	pm_ports_dir(ports, sizeof ports);
	mkdir(ports, 0755);
	snprintf(zip, sizeof zip, "%s/.chaos-%s", ports, p->zip);
	snprintf(what, sizeof what, update ? "Downloading the update" : "Downloading");
	fprintf(stderr, "portmaster: %s %s (v%d)\n", update ? "updating" : "installing", p->zip, p->version);
	if (!pm_fetch(a, p->title, what, url, zip, p->size, md5, accent, why, sizeof why)) {
		if (strcmp(why, "Stopped")) pick_say(a, p->title, why, accent);
		return false;
	}
	{
		/* Over what is there: a port's own files that aren't in the zip -
		 * the game's data you copied in, its saves - stay */
		const char *argv[] = { pm_unzip_bin(), "-oq", zip, "-d", ports, NULL };
		char man[LIB_PATH], old[LIB_PATH + 8], dir[LIB_PATH];
		int rc;

		/* What it unpacks, kept, so a removal can leave the saves.
		 * An update's list is added to the last one's: a file the old
		 * version had and the new one doesn't is still the port's. */
		snprintf(dir, sizeof dir, "%s/ports/installed", P_USERDATA);
		mkdir(dir, 0755);
		pm_manifest_path(p->zip, man, sizeof man);
		snprintf(old, sizeof old, "%s.old", man);
		rename(man, old);
		if (pmcat_zip_names(zip, man)) {
			FILE *fo = fopen(old, "r"), *fn = fo ? fopen(man, "a") : NULL;
			char ln[1100];

			while (fn && fgets(ln, sizeof ln, fo)) fputs(ln, fn);
			if (fn) fclose(fn);
			if (fo) fclose(fo);
			unlink(old);
		} else {
			rename(old, man);
		}
		rc = pm_run(a, p->title, "Unpacking", accent, argv, NULL);

		unlink(zip);
		if (rc != 0) {
			pick_say(a, p->title, "Couldn't unpack it:\nis the card full?", accent);
			return false;
		}
	}
	if (!pm_runtimes(a, p, accent, why, sizeof why)) {
		if (strcmp(why, "Stopped")) pick_say(a, p->title, why, accent);
		fprintf(stderr, "portmaster: %s installed without its runtime: %s\n", p->zip, why);
	}
	{
		char dir[LIB_PATH * 2], left[LIBPACK_NAME], msg[200];

		snprintf(dir, sizeof dir, "%s/%s", ports, p->dir);
		if (p->dir[0] && pm_libs(a, dir, p->title, accent, left, sizeof left) > 0) {
			snprintf(msg, sizeof msg, "It needs %s,\nwhich ChaOS doesn't have yet:\nit may not start", left);
			pick_say(a, p->title, msg, accent);
		}
	}
	pm_box_art(a, p, accent);
	sync();
	fprintf(stderr, "portmaster: %s %s\n", p->zip, update ? "updated" : "installed");
	{
		char ports_dir[LIB_PATH];

		pm_ports_dir(ports_dir, sizeof ports_dir);
		pmcat_installed(&g_pmc, ports_dir);
	}
	rescan_all(a);
	return true;
}

/* A choice of two, and Cancel: 1 the first, 2 the second, 0 neither */
typedef struct {
	const char *heading, *say, *one, *one_hint, *two, *two_hint;
	int chose;
} pm_choice_ctx;

static int pm_choice_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	pm_choice_ctx *c = ctx;
	int n = 0;

	(void)max;
	*heading = c->heading;
	rows[n] = (menu_row){ c->one, NULL, true };
	rows[n++].hint = c->one_hint;
	rows[n] = (menu_row){ c->two, NULL, true };
	rows[n++].hint = c->two_hint;
	rows[n++] = (menu_row){ "Cancel", NULL, true };
	rows[n++] = MENU_RULE;
	rows[n++] = MENU_NOTE(c->say);
	return n;
}

static menu_result pm_choice_key(app *a, void *ctx, in_button key, int sel)
{
	pm_choice_ctx *c = ctx;

	(void)a;
	if (key != IN_ACCEPT) return MENU_STAY;
	c->chose = sel == 0 ? 1 : sel == 1 ? 2 : 0;
	return MENU_DONE;
}

static int pm_choice(app *a, pm_choice_ctx *c, unsigned accent)
{
	menu_style st = { 0 };

	c->chose = 0;
	st.accent = accent;
	st.fixed_w = menu_std_width(a);
	menu_run(a, &st, pm_choice_build, pm_choice_key, c);
	return c->chose;
}

/* The script's name less .sh: what its box art is called */
static void pm_stem(const char *script, char *out, size_t n)
{
	size_t l;

	snprintf(out, n, "%s", script);
	l = strlen(out);
	if (l > 3 && !strcasecmp(out + l - 3, ".sh")) out[l - 3] = '\0';
}

/* A port's own files, by its manifest, leaving what it made as it ran.
 * In a child, with the panel up: a big port is thousands of files. */
static void pm_remove_keep(app *a, const char *zip, const char *title, unsigned accent)
{
	char man[LIB_PATH], ports[LIB_PATH];
	pid_t pid;

	pm_manifest_path(zip, man, sizeof man);
	pm_ports_dir(ports, sizeof ports);
	if (access(man, R_OK) != 0) return;
	pid = fork();
	if (pid == 0) _exit(pmcat_manifest_remove(ports, man) >= 0 ? 0 : 1);
	if (pid > 0) pm_wait(a, pid, title, "Removing", NULL, 0, accent, false);
	unlink(man);
}

static bool pm_remove(app *a, int idx, unsigned accent)
{
	const pm_port *p = &g_pmc.p[idx];
	char msg[200], man[LIB_PATH], art[LIB_PATH * 2], stem[LIB_NAME], sh[LIB_PATH * 2];
	pm_choice_ctx c = { 0 };
	int how;

	pm_manifest_path(p->zip, man, sizeof man);
	snprintf(msg, sizeof msg, "Remove %s", p->title);
	c.heading = msg;
	c.say = "Saves and settings stay on the card if you keep them";
	c.one = "Keep Saves and Settings";
	c.one_hint = "Install it again later and carry on where you left off";
	c.two = "Remove Everything";
	c.two_hint = "Its whole folder, with any game files and saves in it";
	how = pm_choice(a, &c, accent);
	if (how == 0) return false;
	if (how == 2 && !confirm_panel(a, "Remove Everything",
	                               "Its saves and any game files\nyou copied in go too.", "Remove"))
		return false;
	pm_stem(p->script, stem, sizeof stem);
	box_art_path("Ports", stem, art, sizeof art);
	snprintf(sh, sizeof sh, "%s/Ports/%s", P_ROMS, p->script);
	if (how == 1 && access(man, R_OK) == 0) {
		pm_remove_keep(a, p->zip, p->title, accent);
	} else if (how == 1) {
		/* Installed some other way: nothing says which files are its own,
		 * so its folder stays whole and only the script goes */
		if (p->script[0]) unlink(sh);
	} else {
		char dir[LIB_PATH * 2];

		snprintf(dir, sizeof dir, "%s/Ports/%s", P_ROMS, p->dir);
		if (p->dir[0] && strcmp(p->dir, ".") && strcmp(p->dir, "..") &&
		    strcasecmp(p->dir, "PortMaster")) {
			const char *argv[] = { "/bin/rm", "-rf", dir, NULL };

			pm_run(a, p->title, "Removing", accent, argv, NULL);
		}
		if (p->script[0]) unlink(sh);
		unlink(man);
	}
	unlink(art);
	sync();
	fprintf(stderr, "portmaster: %s removed (%s)\n", p->zip, how == 1 ? "saves kept" : "everything");
	{
		char ports_dir[LIB_PATH];

		pm_ports_dir(ports_dir, sizeof ports_dir);
		pmcat_installed(&g_pmc, ports_dir);
	}
	rescan_all(a);
	return true;
}

/* PortMaster off the card. Keeping: every port installed from here loses
 * its own files and keeps what it made; PortMaster's settings wait in
 * .PortMaster.kept for the next install (ports.c); ports put on the card
 * some other way stay as they are. Everything: Roms/Ports, and the ports'
 * home in .userdata. True if it went. */
static bool pm_uninstall(app *a, unsigned accent)
{
	pm_choice_ctx c = { 0 };
	char dir[LIB_PATH * 2], kept[LIB_PATH], cfg[LIB_PATH + 16], to[LIB_PATH + 16];
	int how;

	c.heading = "Uninstall PortMaster";
	c.say = "Your ports go with it. Keep their saves to carry on later";
	c.one = "Keep Saves and Settings";
	c.one_hint = "Reinstall PortMaster and your ports later and carry on";
	c.two = "Remove Everything";
	c.two_hint = "Every port, its saves, and PortMaster's settings";
	how = pm_choice(a, &c, accent);
	if (how == 0) return false;
	if (how == 2 && !confirm_panel(a, "Remove Everything",
	                               "Every port and every save\nin Ports goes. This can't be undone.",
	                               "Remove"))
		return false;
	sync_abandon();
	if (how == 1) {
		DIR *d;
		struct dirent *e;

		snprintf(dir, sizeof dir, "%s/ports/installed", P_USERDATA);
		if ((d = opendir(dir))) {
			while ((e = readdir(d))) {
				char zip[300], man[LIB_PATH + 300], ln[1100], art[LIB_PATH * 2], stem[LIB_NAME];
				size_t l = strlen(e->d_name);
				FILE *f;

				if (l <= 4 || strcmp(e->d_name + l - 4, ".txt")) continue;
				snprintf(zip, sizeof zip, "%.*s.zip", (int)(l - 4), e->d_name);
				/* its box art, by the script its manifest names */
				snprintf(man, sizeof man, "%s/%s", dir, e->d_name);
				if ((f = fopen(man, "r"))) {
					while (fgets(ln, sizeof ln, f)) {
						size_t k = strcspn(ln, "\r\n");

						ln[k] = '\0';
						if (k > 3 && !strchr(ln, '/') && !strcasecmp(ln + k - 3, ".sh")) {
							pm_stem(ln, stem, sizeof stem);
							box_art_path("Ports", stem, art, sizeof art);
							unlink(art);
						}
					}
					fclose(f);
				}
				pm_remove_keep(a, zip, "Uninstall PortMaster", accent);
			}
			closedir(d);
		}
		/* its settings, for next time */
		ports_pm_kept_dir(P_ROMS, kept, sizeof kept);
		mkdir(kept, 0755);
		snprintf(cfg, sizeof cfg, "%s/Ports/PortMaster/config", P_ROMS);
		snprintf(to, sizeof to, "%s/config", kept);
		if (access(cfg, F_OK) == 0) {
			const char *rm[] = { "/bin/rm", "-rf", to, NULL };

			pm_run(a, "Uninstall PortMaster", "Removing", accent, rm, NULL);
			rename(cfg, to);
		}
		snprintf(dir, sizeof dir, "%s/Ports/PortMaster", P_ROMS);
		{
			const char *rm[] = { "/bin/rm", "-rf", dir, NULL };

			pm_run(a, "Uninstall PortMaster", "Removing PortMaster", accent, rm, NULL);
		}
	} else {
		char home[LIB_PATH], inst[LIB_PATH];

		snprintf(dir, sizeof dir, "%s/Ports", P_ROMS);
		snprintf(home, sizeof home, "%s/ports/home", P_USERDATA);
		snprintf(inst, sizeof inst, "%s/ports/installed", P_USERDATA);
		{
			const char *rm[] = { "/bin/rm", "-rf", dir, home, inst, NULL };

			pm_run(a, "Uninstall PortMaster", "Removing everything", accent, rm, NULL);
		}
	}
	sync();
	fprintf(stderr, "portmaster: uninstalled (%s)\n", how == 1 ? "saves kept" : "everything");
	rescan_all(a);
	return !ports_pm_ready(P_ROMS);
}

/* ---- the screenshot under the cursor -------------------------------------- */

typedef struct {
	int          want;      /* the port whose picture is wanted */
	int          shown;     /* the one `tex` is */
	int          fetching;  /* the one being downloaded, -1 none */
	unsigned     since;     /* when `want` last changed */
	SDL_Texture *tex;
	int          tw, th;
	bool         failed;    /* no picture to be had for `want` */
} pm_pic;

static void pm_pic_init(pm_pic *pp) { memset(pp, 0, sizeof *pp); pp->want = pp->shown = pp->fetching = -1; }

static void pm_pic_free(pm_pic *pp)
{
	if (pp->fetching >= 0) net_async_abort();
	if (pp->tex) SDL_DestroyTexture(pp->tex);
	pm_pic_init(pp);
}

static void pm_pic_want(pm_pic *pp, int idx)
{
	if (pp->want == idx) return;
	pp->want = idx;
	pp->since = plat_now_ms();
	pp->failed = false;
}

/* Each frame: the download in flight, the next to start (after the cursor
 * has rested a moment, so scrolling through a list doesn't fetch all of
 * it), and the texture once its file is here */
static void pm_pic_step(app *a, pm_pic *pp)
{
	char img[LIB_PATH], url[400], file[96];

	if (pp->fetching >= 0) {
		int got = net_async_poll();

		if (got != 0) {
			if (got < 0 && pp->fetching == pp->want) pp->failed = true;
			pp->fetching = -1;
		}
	}
	if (pp->want < 0 || pp->shown == pp->want) return;
	pm_img_path(&g_pmc.p[pp->want], img, sizeof img);
	if (!img[0]) { pp->failed = true; return; }
	if (access(img, R_OK) == 0) {
		SDL_Surface *s = IMG_Load(img);

		if (pp->tex) SDL_DestroyTexture(pp->tex);
		pp->tex = NULL;
		if (s) {
			pp->tex = SDL_CreateTextureFromSurface(a->r, s);
			pp->tw = s->w;
			pp->th = s->h;
			SDL_FreeSurface(s);
			if (pp->tex) SDL_SetTextureScaleMode(pp->tex, SDL_ScaleModeLinear);
		} else {
			unlink(img);   /* not a picture: asked for again next time */
			pp->failed = true;
		}
		pp->shown = pp->want;
		return;
	}
	if (pp->fetching < 0 && !pp->failed && plat_now_ms() - pp->since > 180 && net_online()) {
		char dir[LIB_PATH];

		snprintf(dir, sizeof dir, "%s/ports/img", P_USERDATA);
		mkdir(dir, 0755);
		pmcat_shot(&g_pmc.p[pp->want], url, sizeof url, file, sizeof file);
		if (net_get_async(url, img, 20)) pp->fetching = pp->want;
	}
}

/* The picture into `box`, whole, centered; a quiet line when there is none */
static void pm_pic_draw(app *a, pm_pic *pp, SDL_Rect box)
{
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	ui_round_rect(a->r, &box, 10, (SDL_Color){ 0, 0, 0, 90 });
	if (pp->tex && pp->shown == pp->want && pp->tw > 0 && pp->th > 0) {
		float k = (float)box.w / pp->tw < (float)box.h / pp->th ? (float)box.w / pp->tw
		                                                       : (float)box.h / pp->th;
		SDL_Rect d;

		d.w = (int)(pp->tw * k);
		d.h = (int)(pp->th * k);
		d.x = box.x + (box.w - d.w) / 2;
		d.y = box.y + (box.h - d.h) / 2;
		SDL_RenderCopy(a->r, pp->tex, NULL, &d);
	} else {
		const char *say = pp->failed || !net_online() ? "No picture" : "Loading...";

		ui_text(a->r, ui_font(UI_F_META), say, box.x + box.w / 2,
		        box.y + box.h / 2 - ui_font_line(UI_F_META) / 2, 0, UI_TEXT_DIM);
	}
}

/* ---- drawing ------------------------------------------------------------- */

#define PM_PANEL ((SDL_Rect){ 28, 28, TORTOS_SCREEN_W - 56, TORTOS_SCREEN_H - 56 })

static void pm_frame_begin(app *a, unsigned accent, const char *heading, const char *meta)
{
	SDL_Rect panel = PM_PANEL;
	TTF_Font *fh = ui_font(UI_F_LABEL), *fs = ui_font(UI_F_META);
	char fit[200];
	int band = 84;

	tick_tint(a);
	draw_shelf(a);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 150);
	SDL_RenderFillRect(a->r, NULL);
	ui_glow(a->r, &panel, accent, 60, 1.5f);
	ui_panel(a->r, &panel, MENU_RADIUS, accent);
	ui_fit_text(fh, heading, fit, sizeof fit, panel.w - 360);
	ui_text(a->r, fh, fit, panel.x + panel.w / 2,
	        panel.y + (band - ui_font_line(UI_F_LABEL)) / 2, 0, UI_TEXT);
	if (meta && meta[0])
		ui_text(a->r, fs, meta, panel.x + panel.w - 36,
		        panel.y + (band - ui_font_line(UI_F_META)) / 2, 1, UI_TEXT_DIM);
	SDL_SetRenderDrawColor(a->r, (Uint8)(accent >> 16), (Uint8)(accent >> 8), (Uint8)accent, 70);
	SDL_RenderFillRect(a->r, &(SDL_Rect){ panel.x + 32, panel.y + band, panel.w - 64, 2 });
}

static void pm_footer(app *a, const char *keys)
{
	SDL_Rect panel = PM_PANEL;

	ui_text(a->r, ui_font(UI_F_META), keys, panel.x + panel.w / 2,
	        panel.y + panel.h - 24 - ui_font_line(UI_F_META), 0, UI_TEXT_DIM);
}

static bool g_pm_shot;   /* --pm: drawn for a screenshot, not shown */

static void pm_frame_end(app *a)
{
	if (g_pm_shot) return;
	plat_draw_osd(a->r);
	plat_present();
}

/* A small tag: what a port is, beside its name */
static int pm_tag(app *a, const char *s, int right, int cy, SDL_Color fg, SDL_Color bg)
{
	TTF_Font *f = ui_font(UI_F_META);
	int w = ui_text_width(f, s) + 18, h = ui_font_line(UI_F_META) + 4;
	SDL_Rect r = { right - w, cy - h / 2, w, h };

	ui_round_rect(a->r, &r, h / 2, bg);
	ui_text(a->r, f, s, r.x + w / 2, r.y + 2, 0, fg);
	return w;
}

#define PM_GREEN  ((SDL_Color){ 120, 220, 150, 255 })
#define PM_AMBER  ((SDL_Color){ 255, 196, 92, 255 })
#define PM_RED    ((SDL_Color){ 255, 120, 110, 255 })

/* "action, fps" as "Action, FPS" */
static void pm_genre_label(const char *g, char *out, size_t n)
{
	size_t o = 0;

	while (*g && o + 1 < n) {
		size_t l = strcspn(g, ",");
		char one[32];

		snprintf(one, sizeof one, "%.*s", (int)(l < 31 ? l : 31), g);
		if (!strcmp(one, "fps") || !strcmp(one, "rpg")) {
			size_t i;

			for (i = 0; one[i]; i++) one[i] = (char)toupper((unsigned char)one[i]);
		} else if (one[0]) {
			one[0] = (char)toupper((unsigned char)one[0]);
		}
		o += (size_t)snprintf(out + o, n - o, "%s%s", o ? ", " : "", one);
		if (o >= n) { out[n - 1] = '\0'; return; }
		g += l;
		while (*g == ',' || *g == ' ') g++;
	}
	if (o == 0 && n) out[0] = '\0';
}

/* What a port needs, for a person, in one line */
static const char *pm_needs(const pm_port *p)
{
	if (p->flags & PMF_NO) return p->why;
	return p->flags & PMF_RTR ? "Ready to run" : "Needs your own game files";
}

/* ---- a port --------------------------------------------------------------- */

static void pm_detail_draw(app *a, int idx, pm_pic *pp, const char (*desc)[CHV_WRAP_COLS],
                           int ndesc, int top, unsigned accent, const char *keys)
{
	const pm_port *p = &g_pmc.p[idx];
	SDL_Rect panel = PM_PANEL;
	SDL_Rect shot = { panel.x + 36, panel.y + 108, 448, 336 };
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	int x = shot.x + shot.w + 32, w = panel.x + panel.w - 36 - x, y = shot.y;
	int lm = ui_font_line(UI_F_MENU), ls = ui_font_line(UI_F_META);
	char line[256], fit[256];
	int i, vis, dy;

	pm_frame_begin(a, accent, p->title, NULL);
	pm_pic_draw(a, pp, shot);

#define INFO(label, val, col) do { \
		ui_text(a->r, fs, label, x, y, -1, UI_TEXT_DIM); \
		y += ls; \
		ui_fit_text(fs, val, fit, sizeof fit, w); \
		ui_text(a->r, fs, fit, x, y, -1, col); \
		y += ls + 12; \
	} while (0)

	{
		char g[96];

		pm_genre_label(p->genres, g, sizeof g);
		INFO("Ported by", p->porter[0] ? p->porter : "unknown", UI_TEXT);
		INFO("Genre", g[0] ? g : "-", UI_TEXT);
	}
	INFO("Needs", pm_needs(p), p->flags & PMF_NO ? PM_RED : p->flags & PMF_RTR ? PM_GREEN : PM_AMBER);
	{
		char rt[4][64], nice[64];
		int nr = pmcat_runtimes(p, rt, 4);

		if (nr > 0) {
			pm_runtime_name(rt[0], nice, sizeof nice);
			snprintf(line, sizeof line, "%s%s", nice, nr > 1 ? " and more" : "");
			INFO("Runs on", line, UI_TEXT_SOFT);
		}
	}
	if (p->flags & PMF_INSTALLED)
		snprintf(line, sizeof line, p->flags & PMF_UPDATE ? "Installed, update ready"
		                                                  : "Installed");
	else
		snprintf(line, sizeof line, "%.1f MB download", p->size / 1048576.0);
	INFO(p->flags & PMF_INSTALLED ? "On this Brick" : "Size", line,
	     p->flags & PMF_UPDATE ? PM_AMBER : UI_TEXT_SOFT);
	if (p->flags & (PMF_POWER | PMF_EXP))
		INFO("Note", p->flags & PMF_EXP ? "Experimental: it may not work well"
		                                : "Made for stronger devices: it may run slowly", PM_AMBER);
#undef INFO

	/* The description under it all, as much as fits; Y reads the whole */
	dy = shot.y + shot.h + 28;
	vis = (panel.y + panel.h - 24 - ls - 20 - dy) / lm;
	for (i = 0; i < vis && top + i < ndesc; i++)
		ui_text(a->r, fm, desc[top + i], shot.x, dy + i * lm, -1, UI_TEXT_SOFT);
	pm_footer(a, keys);
	pm_frame_end(a);
}

static void pm_detail_screen(app *a, int idx, unsigned accent)
{
	char (*desc)[CHV_WRAP_COLS] = calloc(80, CHV_WRAP_COLS);
	char *text = malloc(8192), *inst = malloc(4096), keys[160];
	pm_pic pp;
	int ndesc = 0;
	SDL_Rect panel = PM_PANEL;

	if (!desc || !text || !inst) { free(desc); free(text); free(inst); return; }
	pm_pic_init(&pp);
	pm_pic_want(&pp, idx);
	pp.since = 0;
	pmcat_text(&g_pmc.p[idx], "desc", text, 8192);
	pmcat_text(&g_pmc.p[idx], "inst", inst, 4096);
	ndesc = wrap_text(ui_font(UI_F_MENU), text[0] ? text : "No description.", panel.w - 72, desc, 80);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (a->running) {
		const pm_port *p = &g_pmc.p[idx];

		pm_pic_step(a, &pp);
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); break; }
		if (menu_leaving(a)) break;
		if (in_repeat(&a->in, IN_VOLUP)) plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN)) plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		if (a->in.pressed[IN_Y]) {
			/* All of it, and how to set it up */
			char *all = malloc(12800);

			if (all) {
				snprintf(all, 12800, "%s%s%s", text, inst[0] ? "\n\nTo play: " : "", inst);
				synopsis_screen(a, p->title, all, accent);
				free(all);
			}
			continue;
		}
		if (a->in.pressed[IN_ACCEPT] && (!(p->flags & PMF_INSTALLED) || p->flags & PMF_UPDATE)) {
			bool was = (p->flags & PMF_INSTALLED) != 0;

			if (p->flags & PMF_NO) {
				char msg[200];

				snprintf(msg, sizeof msg, "%s.\nInstall it anyway?", p->why);
				if (!confirm_panel(a, "Won't run here", msg, "Install")) continue;
			}
			if (pm_install(a, idx, accent) && !was && !(p->flags & PMF_RTR)) {
				/* Not ready to run: what to copy, and where */
				char *say = malloc(4608);

				if (say) {
					snprintf(say, 4608, "%s needs files from your own copy of the game, in "
					         "Ports/%s on the card.\n\n%s", p->title, p->dir,
					         inst[0] ? inst : "PortMaster's page for it says which.");
					synopsis_screen(a, "Before you play", say, accent);
					free(say);
				}
			}
			continue;
		}
		if (a->in.pressed[IN_X] && p->flags & PMF_INSTALLED) {
			pm_remove(a, idx, accent);
			continue;
		}
		snprintf(keys, sizeof keys, "%s%sY: read all   B: back",
		         p->flags & PMF_UPDATE ? "A: update   "
		         : p->flags & PMF_INSTALLED ? "" : "A: install   ",
		         p->flags & PMF_INSTALLED ? "X: remove   " : "");
		pm_detail_draw(a, idx, &pp, (const char (*)[CHV_WRAP_COLS])desc, ndesc, 0, accent, keys);
		SDL_Delay(8);
	}
	pm_pic_free(&pp);
	free(desc); free(text); free(inst);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* ---- a list of ports ------------------------------------------------------ */

static void pm_list_draw(app *a, const char *heading, const int *idx, int n, int sel, int first,
                         int vis, pm_pic *pp, unsigned accent)
{
	SDL_Rect panel = PM_PANEL;
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	int rh = menu_row_h(), lm = ui_font_line(UI_F_MENU), ls = ui_font_line(UI_F_META);
	int lx = panel.x + 20, lw = 452, y0 = panel.y + 100;
	SDL_Rect shot = { lx + lw + 24, y0, panel.x + panel.w - 36 - (lx + lw + 24), 0 };
	char meta[48], fit[256], line[200];
	int i;

	snprintf(meta, sizeof meta, "%d port%s", n, n == 1 ? "" : "s");
	pm_frame_begin(a, accent, heading, meta);
	if (n == 0) {
		ui_text(a->r, fm, g_pm_rtr_only ? "Nothing ready to run here" : "Nothing here yet",
		        panel.x + panel.w / 2, panel.y + panel.h / 2 - lm / 2, 0, UI_TEXT_DIM);
		pm_footer(a, "B: back");
		pm_frame_end(a);
		return;
	}
	for (i = first; i < n && i < first + vis; i++) {
		const pm_port *p = &g_pmc.p[idx[i]];
		int y = y0 + (i - first) * rh, cy = y + rh / 2, right = lx + lw - 12, tw = 0;
		SDL_Color col = p->flags & PMF_NO ? UI_TEXT_DIM : UI_TEXT;

		if (i == sel)
			ui_round_rect(a->r, &(SDL_Rect){ lx, y, lw, rh }, rh / 4, (SDL_Color){ 255, 255, 255, 34 });
		if (p->flags & PMF_UPDATE)
			tw = pm_tag(a, "Update", right, cy, (SDL_Color){ 30, 22, 0, 255 }, PM_AMBER) + 8;
		else if (p->flags & PMF_INSTALLED)
			tw = pm_tag(a, "Installed", right, cy, (SDL_Color){ 0, 30, 12, 255 }, PM_GREEN) + 8;
		else if (!(p->flags & PMF_RTR))
			tw = pm_tag(a, "Files", right, cy, UI_TEXT, (SDL_Color){ 255, 255, 255, 40 }) + 8;
		ui_fit_text(fm, p->title, fit, sizeof fit, lw - 28 - tw);
		ui_text(a->r, fm, fit, lx + 16, cy - lm / 2 - TTF_FontDescent(fm) / 2, -1, col);
	}
	/* where in the list: a thin rail at the left of the rows */
	if (n > vis) {
		int track = vis * rh, th = track * vis / n, ty = y0 + (track - th) * first / (n - vis);

		SDL_SetRenderDrawColor(a->r, (Uint8)(accent >> 16), (Uint8)(accent >> 8), (Uint8)accent, 160);
		SDL_RenderFillRect(a->r, &(SDL_Rect){ lx - 10, ty, 3, th > 12 ? th : 12 });
	}
	/* the one under the cursor */
	{
		const pm_port *p = &g_pmc.p[idx[sel]];
		int y;
		SDL_Color nc = p->flags & PMF_NO ? PM_RED : p->flags & PMF_RTR ? PM_GREEN : PM_AMBER;

		shot.h = shot.w * 3 / 4;
		pm_pic_draw(a, pp, shot);
		y = shot.y + shot.h + 18;
		ui_fit_text(fm, p->title, fit, sizeof fit, shot.w);
		ui_text(a->r, fm, fit, shot.x, y, -1, UI_TEXT);
		y += lm + 4;
		ui_fit_text(fs, pm_needs(p), fit, sizeof fit, shot.w);
		ui_text(a->r, fs, fit, shot.x, y, -1, nc);
		y += ls + 4;
		{
			char g[96];

			pm_genre_label(p->genres, g, sizeof g);
			snprintf(line, sizeof line, "%s%s%.1f MB", g, g[0] ? "  \xC2\xB7  " : "",
			         p->size / 1048576.0);
		}
		ui_fit_text(fs, line, fit, sizeof fit, shot.w);
		ui_text(a->r, fs, fit, shot.x, y, -1, UI_TEXT_DIM);
		y += ls + 4;
		if (p->porter[0]) {
			snprintf(line, sizeof line, "Ported by %s", p->porter);
			ui_fit_text(fs, line, fit, sizeof fit, shot.w);
			ui_text(a->r, fs, fit, shot.x, y, -1, UI_TEXT_DIM);
		}
	}
	pm_footer(a, "A: open   \xE2\x80\xB9 \xE2\x80\xBA: page   L2 R2: letter   B: back");
	draw_letter_popup(a);
	pm_frame_end(a);
}

static int pm_list_build(pm_section s, const char *genre, int **out)
{
	int n = pmcat_list(&g_pmc, s, genre, NULL, 0), k, m = 0;
	int *idx = malloc((size_t)(n ? n : 1) * sizeof *idx);

	*out = idx;
	if (!idx) return 0;
	n = pmcat_list(&g_pmc, s, genre, idx, n);
	for (k = 0; k < n; k++)
		if (!g_pm_rtr_only || g_pmc.p[idx[k]].flags & PMF_RTR) idx[m++] = idx[k];
	return m;
}

/* L2 and R2 in a list of ports: to the next initial, or back to the start
 * of this one and then the one before, as on the shelves
 * (shelf_letter_jump). The list is ready-to-run first, so the letters run
 * A to Z twice when it has both kinds; the scrub goes through both. */
static int pm_letter_jump(const int *idx, int n, int cur, int dir)
{
	char c0;
	int i;

	if (n <= 1) return cur;
	c0 = shelf_initial(g_pmc.p[idx[cur]].title);
	if (dir > 0) {
		for (i = cur + 1; i < n; i++)
			if (shelf_initial(g_pmc.p[idx[i]].title) != c0) return i;
		return 0;                          /* past the end: round to the top */
	}
	i = cur;
	while (i > 0 && shelf_initial(g_pmc.p[idx[i - 1]].title) == c0) i--;
	if (i != cur) return i;
	i = cur == 0 ? n - 1 : cur - 1;        /* before the top: round to the end */
	c0 = shelf_initial(g_pmc.p[idx[i]].title);
	while (i > 0 && shelf_initial(g_pmc.p[idx[i - 1]].title) == c0) i--;
	return i;
}

static void pm_list_screen(app *a, const char *heading, pm_section s, const char *genre,
                           unsigned accent)
{
	Uint32 held = 0;
	SDL_Rect panel = PM_PANEL;
	int *idx = NULL, n = pm_list_build(s, genre, &idx), sel = 0, first = 0;
	int vis = (panel.h - 100 - 24 - ui_font_line(UI_F_META) - 16) / menu_row_h();
	pm_pic pp;

	pm_pic_init(&pp);
	sync_abandon();
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (a->running) {
		int was = sel;

		if (n > 0) pm_pic_want(&pp, idx[sel]);
		pm_pic_step(a, &pp);
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); break; }
		if (menu_leaving(a)) break;
		if (in_repeat(&a->in, IN_VOLUP)) plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN)) plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		if (n > 0) {
			if (in_repeat(&a->in, IN_DOWN)) sel = sel + 1 < n ? sel + 1 : 0;
			if (in_repeat(&a->in, IN_UP)) sel = sel > 0 ? sel - 1 : n - 1;
			if (in_repeat(&a->in, IN_RIGHT) || in_repeat(&a->in, IN_R1))
				sel = sel + vis < n ? sel + vis : n - 1;
			if (in_repeat(&a->in, IN_LEFT) || in_repeat(&a->in, IN_L1))
				sel = sel - vis > 0 ? sel - vis : 0;
			/* L2 and R2 scrub by letter, with the letter shown; holding up
			 * or down for two seconds shows it too, as on the shelves */
			if (in_repeat(&a->in, IN_R2)) {
				sel = pm_letter_jump(idx, n, sel, +1);
				letter_show(shelf_initial(g_pmc.p[idx[sel]].title));
			}
			if (in_repeat(&a->in, IN_L2)) {
				sel = pm_letter_jump(idx, n, sel, -1);
				letter_show(shelf_initial(g_pmc.p[idx[sel]].title));
			}
			if (a->in.down[IN_UP] || a->in.down[IN_DOWN]) {
				if (!held) held = plat_now_ms();
				if (plat_now_ms() - held >= 2000) letter_show(shelf_initial(g_pmc.p[idx[sel]].title));
			} else {
				held = 0;
			}
			if (a->in.pressed[IN_ACCEPT]) {
				int at = idx[sel];

				pm_pic_free(&pp);
				pm_detail_screen(a, at, accent);
				if (g_menu_closing || !a->running) break;
				/* An install or a removal can move it out of this list */
				free(idx);
				n = pm_list_build(s, genre, &idx);
				for (sel = 0; sel < n && idx[sel] != at; sel++) ;
				if (sel >= n) sel = n > 0 ? (was < n ? was : n - 1) : 0;
				continue;
			}
		}
		if (sel < first) first = sel;
		if (sel >= first + vis) first = sel - vis + 1;
		pm_list_draw(a, heading, idx, n, sel, first, vis, &pp, accent);
		SDL_Delay(8);
	}
	pm_pic_free(&pp);
	free(idx);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* ---- Genres --------------------------------------------------------------- */

typedef struct {
	char names[32][24];
	char labels[32][32];
	char counts[32][12];
	int  n;
	unsigned accent;
} pm_genre_ctx;

static int pm_genre_count(const char *g)
{
	int *idx = NULL, n = pm_list_build(PMS_MAY, g, &idx);

	free(idx);
	return n;
}

static int pm_genre_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	pm_genre_ctx *c = ctx;
	int i, n = 0;

	*heading = "Genres";
	for (i = 0; i < c->n && n < max; i++)
		rows[n++] = (menu_row){ c->labels[i], c->counts[i], true, true };
	return n;
}

static menu_result pm_genre_key(app *a, void *ctx, in_button key, int sel)
{
	pm_genre_ctx *c = ctx;
	char head[64];

	if (key != IN_ACCEPT || sel < 0 || sel >= c->n) return MENU_STAY;
	/* What may run, of the genre: the ones that can't are no use browsing
	 * by kind; Show All has them */
	snprintf(head, sizeof head, "%s", c->labels[sel]);
	pm_list_screen(a, head, PMS_MAY, c->names[sel], c->accent);
	return g_menu_closing ? MENU_DONE : MENU_STAY;
}

/* ---- PortMaster's first page ---------------------------------------------- */

enum { PMH_MAY, PMH_ALL, PMH_GENRES, PMH_INSTALLED, PMH_UPDATES, PMH_RTR,
       PMH_REFRESH, PMH_PM, PMH_UNINSTALL, PMH_ROWS };

typedef struct {
	char counts[PMH_ROWS][16];
	char when[48], note[96];
	unsigned accent;
} pm_home_ctx;

static void pm_home_counts(pm_home_ctx *c)
{
	static const pm_section sec[] = { PMS_MAY, PMS_ALL, 0, PMS_INSTALLED, PMS_UPDATES };
	int i;
	long age = pm_cat_age();

	for (i = 0; i <= PMH_UPDATES; i++) {
		int *idx = NULL;

		c->counts[i][0] = '\0';
		if (i == PMH_GENRES || !g_pmc_loaded) continue;
		snprintf(c->counts[i], sizeof c->counts[i], "%d", pm_list_build(sec[i], NULL, &idx));
		free(idx);
	}
	if (age < 0) snprintf(c->when, sizeof c->when, "never");
	else if (age < 3600) snprintf(c->when, sizeof c->when, "just now");
	else if (age < 86400) snprintf(c->when, sizeof c->when, "%ld hour%s ago", age / 3600, age < 7200 ? "" : "s");
	else snprintf(c->when, sizeof c->when, "%ld day%s ago", age / 86400, age < 172800 ? "" : "s");
	snprintf(c->note, sizeof c->note, g_pmc_loaded ? "%d ports in PortMaster's catalog" : "No catalog yet",
	         g_pmc.n);
}

static int pm_home_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	pm_home_ctx *c = ctx;
	bool have = g_pmc_loaded, pm = ports_pm_ready(P_ROMS);
	int n = 0;

	(void)max;
	*heading = "PortMaster";
	rows[n] = (menu_row){ "May Run on ChaOS", c->counts[PMH_MAY], have, true };
	rows[n++].hint = "Nothing it needs rules out the Brick";
	rows[n] = (menu_row){ "Show All", c->counts[PMH_ALL], have, true };
	rows[n++].hint = "Every port, with the ones that can't run here marked";
	rows[n] = (menu_row){ "Genres", NULL, have, true };
	rows[n++].hint = "What may run, by kind of game";
	rows[n] = (menu_row){ "Installed", c->counts[PMH_INSTALLED], have, true };
	rows[n++].hint = "The ports on this Brick from the catalog";
	rows[n] = (menu_row){ "Updates", c->counts[PMH_UPDATES], have && strcmp(c->counts[PMH_UPDATES], "0"), true };
	rows[n++].hint = "Installed ports with a newer version";
	rows[n] = (menu_row){ "Ready to Run Only", g_pm_rtr_only ? "On" : "Off", true };
	rows[n].toggle = true;
	rows[n++].hint = "Hide the ports that need files from a game you own";
	rows[n] = (menu_row){ "Refresh List", c->when, true };
	rows[n++].hint = "Get PortMaster's latest catalog";
	rows[n] = (menu_row){ pm ? "Update PortMaster" : "Get PortMaster", NULL, true };
	rows[n++].hint = pm ? "Its newest release; your ports, saves and settings stay"
	                    : "Ports run with PortMaster: download it (about 25 MB)";
	rows[n] = (menu_row){ "Uninstall PortMaster", NULL, pm, true };
	rows[n++].hint = "Remove it and its ports, keeping their saves if you like";
	rows[n++] = MENU_RULE;
	rows[n++] = MENU_NOTE(c->note);
	return n;
}

static menu_result pm_home_key(app *a, void *ctx, in_button key, int sel)
{
	pm_home_ctx *c = ctx;
	static const char *const heads[] = { "May Run on ChaOS", "All Ports",
	                                     NULL, "Installed", "Updates" };
	static const pm_section sec[] = { PMS_MAY, PMS_ALL, 0, PMS_INSTALLED, PMS_UPDATES };

	if (sel == PMH_RTR && (key == IN_ACCEPT || key == IN_LEFT || key == IN_RIGHT)) {
		g_pm_rtr_only = !g_pm_rtr_only;
		db_set_int(db_dev(), "pm_rtr_only", g_pm_rtr_only);
		pm_home_counts(c);
		return MENU_STAY;
	}
	if (key != IN_ACCEPT) return MENU_STAY;
	switch (sel) {
	case PMH_MAY: case PMH_ALL: case PMH_INSTALLED: case PMH_UPDATES:
		if (!g_pmc_loaded) return MENU_STAY;
		pm_list_screen(a, heads[sel], sec[sel], NULL, c->accent);
		break;
	case PMH_GENRES: {
		pm_genre_ctx g;
		menu_style st = { 0 };
		int i, k = 0;

		memset(&g, 0, sizeof g);
		g.accent = c->accent;
		g.n = pmcat_genres(&g_pmc, g.names, 32);
		if (g.n > 32) g.n = 32;
		for (i = 0; i < g.n; i++) {
			int cnt = pm_genre_count(g.names[i]);

			if (!cnt) continue;
			memmove(g.names[k], g.names[i], 24);
			pm_genre_label(g.names[k], g.labels[k], sizeof g.labels[k]);
			snprintf(g.counts[k], sizeof g.counts[k], "%d", cnt);
			k++;
		}
		g.n = k;
		st.accent = c->accent;
		st.fixed_w = menu_std_width(a);
		menu_run(a, &st, pm_genre_build, pm_genre_key, &g);
		break;
	}
	case PMH_REFRESH:
		if (pm_catalog_fetch(a, c->accent)) pm_reload();
		break;
	case PMH_PM:
		if (!net_online()) { pick_say(a, "PortMaster", "Connect to Wi-Fi first", c->accent); break; }
		ports_setup_screen(a, "PortMaster", c->accent);
		break;
	case PMH_UNINSTALL:
		if (pm_uninstall(a, c->accent)) return MENU_DONE;
		break;
	default: break;
	}
	pm_home_counts(c);
	return g_menu_closing ? MENU_DONE : MENU_STAY;
}

static void pm_screen(app *a, unsigned accent);

/* Get PortMaster, in the ChaOS menu until it is installed: then the Ports
 * shelf appears, PortMaster its first card, and the browser opens */
static void pm_get(app *a)
{
	if (!ports_setup_screen(a, "PortMaster", MENU_ACCENT) || !a->running) return;
	rescan_all(a);
	pm_screen(a, MENU_ACCENT);
}

/* PortMaster, from the Ports shelf's first card or the ChaOS menu */
static void pm_screen(app *a, unsigned accent)
{
	pm_home_ctx c;
	menu_style st = { 0 };
	long age = pm_cat_age();

	memset(&c, 0, sizeof c);
	c.accent = accent;
	g_pm_rtr_only = db_get_int(db_dev(), "pm_rtr_only", 0) != 0;
	/* A day old or none at all: fetched first (with no Wi-Fi, the one on
	 * the card, or a word about Wi-Fi when there is none) */
	if (age < 0 || age > 86400) pm_catalog_fetch(a, accent);
	if (!a->running) return;
	pm_reload();
	pm_home_counts(&c);
	st.accent = accent;
	st.fixed_w = menu_std_width(a);
	menu_run(a, &st, pm_home_build, pm_home_key, &c);
	/* 2.5 MB is a lot to keep for a screen nobody is on */
	if (g_pmc_loaded) { pmcat_free(&g_pmc); g_pmc_loaded = false; }
}

/* ---- ChaOS: RomM (src/romm.h) ---------------------------------------------
 *
 * The ChaOS menu's RomM: signing in by RomM's pairing, its library by system
 * with each game's cover, a game downloaded onto its shelf, saves synced, and
 * BIOS fetched. Everything here waits behind a panel, B giving up; nothing
 * runs during a game. */

static romm_conn g_romm;
static bool      g_romm_loaded;

static void romm_dir(char *out, size_t n) { snprintf(out, n, "%s/romm", P_USERDATA); }

static romm_conn *romm_get(void)
{
	if (!g_romm_loaded) {
		char d[LIB_PATH];

		romm_dir(d, sizeof d);
		romm_load(&g_romm, d);
		g_romm_loaded = true;
	}
	return &g_romm;
}

static void romm_keep(void)
{
	char d[LIB_PATH];

	romm_dir(d, sizeof d);
	mkdir(d, 0700);
	romm_save(&g_romm, d);
}

/* systems.cfg's folder for a tag: where a RomM platform's games go */
static bool romm_folder_for(const char *tag, char *out, size_t n)
{
	static systems_cfg cfg;
	static bool loaded;
	char path[CFG_STR * 2];
	int i;

	if (!loaded) {
		snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
		loaded = cfg_load_systems(path, &cfg);
	}
	for (i = 0; loaded && i < cfg.count; i++)
		if (tag && !strcmp(cfg.systems[i].tag, tag)) {
			snprintf(out, n, "%s", cfg.systems[i].folder);
			return true;
		}
	return false;
}

/* A blocking call behind "Talking to RomM" */
static void romm_busy(app *a, const char *what)
{
	pick_frame(a, "RomM", NULL, 0, 0, NULL, 0, 0, what, MENU_ACCENT);
}

/* The name this Brick goes by in RomM's list of devices */
static void romm_device_name(char *out, size_t n)
{
	char host[64] = "";

	if (gethostname(host, sizeof host) != 0 || !host[0]) snprintf(host, sizeof host, "Brick");
	snprintf(out, n, "ChaOS (%s)", host);
}

/* ---- signing in ------------------------------------------------------------ */

static void romm_signed_in(app *a)
{
	char why[160], name[96];

	romm_busy(a, "Signed in\nGetting your account");
	romm_whoami(&g_romm, why, sizeof why);
	romm_device_name(name, sizeof name);
	if (!romm_register(&g_romm, name, why, sizeof why))
		fprintf(stderr, "romm: no device for save sync: %s\n", why);
	romm_keep();
	fprintf(stderr, "romm: signed in to %s as %s\n", g_romm.url, g_romm.user);
}

/* RomM 5: a code on the screen, approved in RomM's web page */
static bool romm_pair_screen(app *a)
{
	romm_pair p;
	char why[160], name[96], msg[600];
	unsigned t0, next;
	int r, interval;

	if (!net_online()) { pick_say(a, "RomM", "Connect to Wi-Fi first", MENU_ACCENT); return false; }
	romm_busy(a, "Talking to RomM");
	romm_device_name(name, sizeof name);
	r = romm_pair_begin(&g_romm, name, &p, why, sizeof why);
	if (r < 0) { pick_say(a, "RomM", why, MENU_ACCENT); return false; }
	if (r == 0) {
		/* RomM 4: a pairing code made in its web page, typed here */
		char code[64] = "";
		kb_result kr;

		pick_say(a, "RomM", "In RomM's web page: your profile,\nClient API Tokens, Pair Device.\nThen type its code here",
		         MENU_ACCENT);
		kr = kb_prompt(a->r, &a->in, "RomM Pairing Code", code, (int)sizeof code, MENU_ACCENT,
		               wifi_backdrop, idle_due_ctx, a);
		if (kr == KB_POWER) { power_off(a); return false; }
		if (kr != KB_ACCEPT || !code[0]) return false;
		romm_busy(a, "Checking the code");
		if (!romm_exchange(&g_romm, code, why, sizeof why)) { pick_say(a, "RomM", why, MENU_ACCENT); return false; }
		romm_signed_in(a);
		return true;
	}
	interval = p.interval;
	t0 = plat_now_ms();
	next = t0 + (unsigned)interval * 1000u;
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (a->running) {
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; return false; }
		if (a->in.pressed[IN_POWER]) { power_off(a); return false; }
		if (menu_leaving(a)) return false;
		if (plat_now_ms() - t0 > (unsigned)p.expires * 1000u) {
			pick_say(a, "RomM", "The code ran out: try again", MENU_ACCENT);
			return false;
		}
		if ((int)(plat_now_ms() - next) >= 0) {
			r = romm_pair_poll(&g_romm, &p, why, sizeof why);
			if (r == ROMM_PAIR_OK) { romm_signed_in(a); return true; }
			if (r == ROMM_PAIR_DENIED || r == ROMM_PAIR_EXPIRED) {
				pick_say(a, "RomM", why, MENU_ACCENT);
				return false;
			}
			if (r == ROMM_PAIR_SLOW) interval += 5;
			next = plat_now_ms() + (unsigned)interval * 1000u;
		}
		snprintf(msg, sizeof msg, "Your code: %s\n\nOn a phone or computer, open\n%s\nand approve it",
		         p.user_code, p.verify);
		pick_frame(a, "Connect to RomM", NULL, 0, 0, NULL, 0, 0, msg, MENU_ACCENT);
		SDL_Delay(30);
	}
	return false;
}

/* ---- downloads -------------------------------------------------------------- */

/* A RomM download with the panel up: pm_fetch's way, with the token */
static bool romm_fetch(app *a, const char *title, const char *what, const char *url,
                       const char *path, long size, char *why, size_t wn)
{
	char part[LIB_PATH * 2 + 8];
	net_req r;
	pid_t pid;
	int rc;

	snprintf(part, sizeof part, "%s.part", path);
	unlink(part);
	memset(&r, 0, sizeof r);
	r.url = url;
	r.bearer = g_romm.token;
	r.timeout_s = 3600;
	pid = net_request_spawn(&r, part);
	if (pid <= 0) { snprintf(why, wn, "Couldn't start the download"); return false; }
	rc = pm_wait(a, pid, title, what, part, size, MENU_ACCENT, true);
	if (rc != 0) {
		unlink(part);
		snprintf(why, wn, rc < 0 ? "Stopped" : "Couldn't download it");
		return false;
	}
	if (rename(part, path) != 0) { unlink(part); snprintf(why, wn, "Couldn't save it to the card"); return false; }
	return true;
}

/* The first file in `dir` with one of `exts` (a disc's .m3u or .cue) */
static bool romm_first_of(const char *dir, const char *const *exts, char *out, size_t n)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	bool found = false;
	int i;

	if (!d) return false;
	for (i = 0; exts[i] && !found; i++) {
		rewinddir(d);
		while ((e = readdir(d))) {
			const char *dot = strrchr(e->d_name, '.');

			if (dot && !strcasecmp(dot + 1, exts[i])) {
				snprintf(out, n, "%s", e->d_name);
				found = true;
				break;
			}
		}
	}
	closedir(d);
	return found;
}

/* A game onto its shelf: the file, or a multi-file game's zip unpacked to a
 * folder of its own; its cover as its box art; and noted as RomM's */
static bool romm_download_game(app *a, const romm_rom *g, const char *folder, const char *cover)
{
	char url[1200], dir[LIB_PATH], dest[LIB_PATH * 2], why[160], file[LIB_PATH * 2], rdir[LIB_PATH];
	char stem[LIB_NAME];

	if (!net_online()) { pick_say(a, g->name, "Connect to Wi-Fi first", MENU_ACCENT); return false; }
	sync_abandon();
	snprintf(dir, sizeof dir, "%s/%s", P_ROMS, folder);
	mkdir(dir, 0755);
	romm_rom_url(&g_romm, g, url, sizeof url);
	if (!g->multi) {
		snprintf(dest, sizeof dest, "%s/%s", dir, g->file_name);
		if (!romm_fetch(a, g->name, "Downloading", url, dest, g->size, why, sizeof why)) {
			if (strcmp(why, "Stopped")) pick_say(a, g->name, why, MENU_ACCENT);
			return false;
		}
		snprintf(file, sizeof file, "%s", g->file_name);
		snprintf(stem, sizeof stem, "%s", g->file_name);
		if (strrchr(stem, '.')) *strrchr(stem, '.') = '\0';
	} else {
		static const char *const firsts[] = { "m3u", "cue", "ccd", "chd", "toc", NULL };
		char zip[LIB_PATH * 2], to[LIB_PATH * 2], disc[LIB_NAME];

		snprintf(zip, sizeof zip, "%s/.romm-%d.zip", dir, g->id);
		if (!romm_fetch(a, g->name, "Downloading", url, zip, g->size, why, sizeof why)) {
			if (strcmp(why, "Stopped")) pick_say(a, g->name, why, MENU_ACCENT);
			return false;
		}
		snprintf(to, sizeof to, "%s/%s", dir, g->fs_name);
		mkdir(to, 0755);
		{
			const char *argv[] = { pm_unzip_bin(), "-oq", zip, "-d", to, NULL };
			int rc = pm_run(a, g->name, "Unpacking", MENU_ACCENT, argv, NULL);

			unlink(zip);
			if (rc != 0) { pick_say(a, g->name, "Couldn't unpack it", MENU_ACCENT); return false; }
		}
		if (!romm_first_of(to, firsts, disc, sizeof disc)) disc[0] = '\0';
		snprintf(file, sizeof file, "%s%s%s", g->fs_name, disc[0] ? "/" : "", disc);
		snprintf(stem, sizeof stem, "%s", g->fs_name);
	}
	/* its cover, as the shelf's box art */
	if (cover && cover[0] && access(cover, R_OK) == 0) {
		char media[LIB_PATH + 16];

		snprintf(media, sizeof media, "%s/.media", dir);
		mkdir(media, 0755);
		cover_save(a, cover, folder, stem);
	}
	romm_dir(rdir, sizeof rdir);
	romm_map_add(rdir, g->id, folder, file);
	sync();
	fprintf(stderr, "romm: %s downloaded into %s\n", g->name, folder);
	rescan_all(a);
	return true;
}

/* ---- a system's games ------------------------------------------------------- */

typedef struct {
	romm_rom *g;
	int n;
	const char *folder;
	char (*have)[1];              /* on the card already */
} romm_list;

/* The cover under the cursor, fetched with the token, kept in .userdata */
typedef struct {
	int want, shown;
	pid_t pid;
	int fetching;
	unsigned since;
	SDL_Texture *tex;
	int tw, th;
	bool failed;
} romm_pic;

static void romm_cover_path(int id, char *out, size_t n)
{
	snprintf(out, n, "%s/romm/covers/%d.img", P_USERDATA, id);
}

static void romm_pic_step(app *a, romm_pic *pp, const romm_rom *list)
{
	char path[LIB_PATH], url[1200];

	if (pp->pid > 0) {
		int st;
		pid_t r = waitpid(pp->pid, &st, WNOHANG);

		if (r == pp->pid || r < 0) {
			char part[LIB_PATH + 8];

			net_spawn_done(pp->pid);
			romm_cover_path(list[pp->fetching].id, path, sizeof path);
			snprintf(part, sizeof part, "%s.part", path);
			if (r == pp->pid && WIFEXITED(st) && WEXITSTATUS(st) == 0) rename(part, path);
			else { unlink(part); if (pp->fetching == pp->want) pp->failed = true; }
			pp->pid = 0;
		}
	}
	if (pp->want < 0 || pp->shown == pp->want) return;
	romm_cover_path(list[pp->want].id, path, sizeof path);
	if (access(path, R_OK) == 0) {
		SDL_Surface *s = IMG_Load(path);

		if (pp->tex) SDL_DestroyTexture(pp->tex);
		pp->tex = NULL;
		if (s) {
			pp->tex = SDL_CreateTextureFromSurface(a->r, s);
			pp->tw = s->w; pp->th = s->h;
			SDL_FreeSurface(s);
			if (pp->tex) SDL_SetTextureScaleMode(pp->tex, SDL_ScaleModeLinear);
		} else {
			pp->failed = true;
		}
		pp->shown = pp->want;
		return;
	}
	if (pp->pid <= 0 && !pp->failed && plat_now_ms() - pp->since > 180 && net_online()) {
		char dir[LIB_PATH], part[LIB_PATH + 8];
		net_req r;
		bool own;

		romm_cover_url(&g_romm, &list[pp->want], url, sizeof url);
		if (!url[0]) { pp->failed = true; return; }
		snprintf(dir, sizeof dir, "%s/romm/covers", P_USERDATA);
		mkdir(dir, 0755);
		snprintf(part, sizeof part, "%s.part", path);
		own = !strncmp(url, g_romm.url, strlen(g_romm.url));
		memset(&r, 0, sizeof r);
		r.url = url;
		r.bearer = own ? g_romm.token : NULL;   /* never to anyone else's server */
		r.timeout_s = 20;
		pp->pid = net_request_spawn(&r, part);
		pp->fetching = pp->want;
		if (pp->pid <= 0) { pp->pid = 0; pp->failed = true; }
	}
}

static void romm_pic_free(romm_pic *pp)
{
	if (pp->pid > 0) { kill(pp->pid, SIGTERM); waitpid(pp->pid, NULL, 0); net_spawn_done(pp->pid); }
	if (pp->tex) SDL_DestroyTexture(pp->tex);
	memset(pp, 0, sizeof *pp);
	pp->want = pp->shown = -1;
}

static void romm_list_draw(app *a, const char *heading, const romm_list *l, int sel, int first,
                           int vis, romm_pic *pp)
{
	SDL_Rect panel = PM_PANEL;
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	int rh = menu_row_h(), lm = ui_font_line(UI_F_MENU), ls = ui_font_line(UI_F_META);
	int lx = panel.x + 20, lw = 452, y0 = panel.y + 100, i;
	SDL_Rect box = { lx + lw + 24, y0, panel.x + panel.w - 36 - (lx + lw + 24), 0 };
	char meta[48], fit[256], line[200];

	snprintf(meta, sizeof meta, "%d game%s", l->n, l->n == 1 ? "" : "s");
	pm_frame_begin(a, MENU_ACCENT, heading, meta);
	if (l->n == 0) {
		ui_text(a->r, fm, "No games here", panel.x + panel.w / 2, panel.y + panel.h / 2 - lm / 2, 0,
		        UI_TEXT_DIM);
		pm_footer(a, "B: back");
		pm_frame_end(a);
		return;
	}
	for (i = first; i < l->n && i < first + vis; i++) {
		int y = y0 + (i - first) * rh, cy = y + rh / 2, tw = 0;

		if (i == sel)
			ui_round_rect(a->r, &(SDL_Rect){ lx, y, lw, rh }, rh / 4, (SDL_Color){ 255, 255, 255, 34 });
		if (l->have[i][0])
			tw = pm_tag(a, "On Brick", lx + lw - 12, cy, (SDL_Color){ 0, 30, 12, 255 }, PM_GREEN) + 8;
		ui_fit_text(fm, l->g[i].name, fit, sizeof fit, lw - 28 - tw);
		ui_text(a->r, fm, fit, lx + 16, cy - lm / 2 - TTF_FontDescent(fm) / 2, -1, UI_TEXT);
	}
	if (l->n > vis) {
		int track = vis * rh, th = track * vis / l->n, ty = y0 + (track - th) * first / (l->n - vis);

		SDL_SetRenderDrawColor(a->r, 0x3D, 0xD6, 0xFF, 160);
		SDL_RenderFillRect(a->r, &(SDL_Rect){ lx - 10, ty, 3, th > 12 ? th : 12 });
	}
	{
		const romm_rom *g = &l->g[sel];
		int y;

		/* as tall as leaves room for the name, its size and the keys */
		box.h = panel.y + panel.h - 24 - ls - 20 - (lm + ls + 30) - y0;
		if (box.h > box.w) box.h = box.w;
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		ui_round_rect(a->r, &box, 10, (SDL_Color){ 0, 0, 0, 90 });
		if (pp->tex && pp->shown == pp->want && pp->tw > 0 && pp->th > 0) {
			float k = (float)box.w / pp->tw < (float)box.h / pp->th ? (float)box.w / pp->tw
			                                                       : (float)box.h / pp->th;
			SDL_Rect d = { 0, 0, (int)(pp->tw * k), (int)(pp->th * k) };

			d.x = box.x + (box.w - d.w) / 2;
			d.y = box.y + (box.h - d.h) / 2;
			SDL_RenderCopy(a->r, pp->tex, NULL, &d);
		} else {
			ui_text(a->r, fs, pp->failed ? "No cover" : "Loading...", box.x + box.w / 2,
			        box.y + box.h / 2 - ls / 2, 0, UI_TEXT_DIM);
		}
		y = box.y + box.h + 18;
		ui_fit_text(fm, g->name, fit, sizeof fit, box.w);
		ui_text(a->r, fm, fit, box.x, y, -1, UI_TEXT);
		y += lm + 4;
		snprintf(line, sizeof line, "%.1f MB%s%s", g->size / 1048576.0, g->multi ? "  \xC2\xB7  several files" : "",
		         l->have[sel][0] ? "  \xC2\xB7  on this Brick" : "");
		ui_text(a->r, fs, line, box.x, y, -1, UI_TEXT_DIM);
		(void)y;
	}
	pm_footer(a, "A: download   \xE2\x80\xB9 \xE2\x80\xBA: page   L2 R2: letter   B: back");
	draw_letter_popup(a);
	pm_frame_end(a);
}

static bool romm_is_here(const char *dir, const romm_rom *g, const char *folder)
{
	char p[LIB_PATH * 2];

	snprintf(p, sizeof p, "%s/%s/%s", P_ROMS, folder, g->multi ? g->fs_name : g->file_name);
	(void)dir;
	return access(p, F_OK) == 0;
}

static int romm_letter_jump(const romm_list *l, int cur, int dir)
{
	char c0;
	int i;

	if (l->n <= 1) return cur;
	c0 = shelf_initial(l->g[cur].name);
	if (dir > 0) {
		for (i = cur + 1; i < l->n; i++) if (shelf_initial(l->g[i].name) != c0) return i;
		return 0;
	}
	i = cur;
	while (i > 0 && shelf_initial(l->g[i - 1].name) == c0) i--;
	if (i != cur) return i;
	i = cur == 0 ? l->n - 1 : cur - 1;
	c0 = shelf_initial(l->g[i].name);
	while (i > 0 && shelf_initial(l->g[i - 1].name) == c0) i--;
	return i;
}

static void romm_games_screen(app *a, const romm_platform *p, const char *folder)
{
	SDL_Rect panel = PM_PANEL;
	romm_list l;
	romm_pic pp;
	char why[160], now[96], rdir[LIB_PATH];
	int total = 0, got, cap = 0, sel = 0, first = 0, i;
	int vis = (panel.h - 100 - 24 - ui_font_line(UI_F_META) - 16) / menu_row_h();
	Uint32 held = 0;

	memset(&l, 0, sizeof l);
	l.folder = folder;
	romm_dir(rdir, sizeof rdir);
	/* the whole list, a page at a time */
	for (;;) {
		romm_rom *more;

		if (l.n + 100 > cap) {
			cap = cap ? cap * 2 : 256;
			more = realloc(l.g, (size_t)cap * sizeof *l.g);
			if (!more) break;
			l.g = more;
		}
		snprintf(now, sizeof now, "Getting %s's games\n%d of %d", p->name, l.n, total ? total : p->roms);
		romm_busy(a, now);
		got = romm_roms(&g_romm, p->id, l.n, 100, l.g + l.n, 100, &total, why, sizeof why);
		if (got < 0) { pick_say(a, "RomM", why, MENU_ACCENT); free(l.g); return; }
		l.n += got;
		if (got == 0 || l.n >= total) break;
		plat_input_poll(&a->in);
		if (menu_leaving(a)) break;
	}
	l.have = calloc((size_t)(l.n ? l.n : 1), 1);
	for (i = 0; i < l.n && l.have; i++) l.have[i][0] = romm_is_here(rdir, &l.g[i], folder);
	if (!l.have) { free(l.g); return; }
	memset(&pp, 0, sizeof pp);
	pp.want = pp.shown = -1;
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	while (a->running) {
		if (l.n > 0 && pp.want != sel) { pp.want = sel; pp.since = plat_now_ms(); pp.failed = false; }
		romm_pic_step(a, &pp, l.g);
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); break; }
		if (menu_leaving(a)) break;
		if (in_repeat(&a->in, IN_VOLUP)) plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN)) plat_volume_nudge(-1);
		if (l.n > 0) {
			if (in_repeat(&a->in, IN_DOWN)) sel = sel + 1 < l.n ? sel + 1 : 0;
			if (in_repeat(&a->in, IN_UP)) sel = sel > 0 ? sel - 1 : l.n - 1;
			if (in_repeat(&a->in, IN_RIGHT) || in_repeat(&a->in, IN_R1)) sel = sel + vis < l.n ? sel + vis : l.n - 1;
			if (in_repeat(&a->in, IN_LEFT) || in_repeat(&a->in, IN_L1)) sel = sel - vis > 0 ? sel - vis : 0;
			if (in_repeat(&a->in, IN_R2)) { sel = romm_letter_jump(&l, sel, +1); letter_show(shelf_initial(l.g[sel].name)); }
			if (in_repeat(&a->in, IN_L2)) { sel = romm_letter_jump(&l, sel, -1); letter_show(shelf_initial(l.g[sel].name)); }
			if (a->in.down[IN_UP] || a->in.down[IN_DOWN]) {
				if (!held) held = plat_now_ms();
				if (plat_now_ms() - held >= 2000) letter_show(shelf_initial(l.g[sel].name));
			} else {
				held = 0;
			}
			if (a->in.pressed[IN_ACCEPT]) {
				char cover[LIB_PATH];
				bool go = true;

				if (l.have[sel][0])
					go = confirm_panel(a, "Download", "It's on this Brick already.\nDownload it again?",
					                   "Download");
				if (go) {
					romm_cover_path(l.g[sel].id, cover, sizeof cover);
					if (romm_download_game(a, &l.g[sel], folder, cover)) l.have[sel][0] = 1;
				}
				plat_input_flush();
				memset(&a->in, 0, sizeof a->in);
				continue;
			}
		}
		if (sel < first) first = sel;
		if (sel >= first + vis) first = sel - vis + 1;
		romm_list_draw(a, p->name, &l, sel, first, vis, &pp);
		SDL_Delay(8);
	}
	romm_pic_free(&pp);
	free(l.have);
	free(l.g);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* ---- systems -------------------------------------------------------------- */

typedef struct {
	romm_platform p[64];
	char folder[64][CFG_STR];
	char counts[64][16];
	int n;
} romm_sys_ctx;

static int romm_sys_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	romm_sys_ctx *c = ctx;
	int i, n = 0;

	*heading = "RomM Games";
	for (i = 0; i < c->n && n < max - 2; i++)
		rows[n++] = (menu_row){ c->p[i].name, c->counts[i], c->p[i].roms > 0, true };
	if (c->n == 0) rows[n++] = MENU_NOTE("None of RomM's systems are ChaOS's");
	return n;
}

static menu_result romm_sys_key(app *a, void *ctx, in_button key, int sel)
{
	romm_sys_ctx *c = ctx;

	if (key != IN_ACCEPT || sel < 0 || sel >= c->n) return MENU_STAY;
	romm_games_screen(a, &c->p[sel], c->folder[sel]);
	return g_menu_closing ? MENU_DONE : MENU_STAY;
}

static void romm_systems_screen(app *a)
{
	romm_sys_ctx *c = calloc(1, sizeof *c);
	romm_platform all[64];
	menu_style st = { 0 };
	char why[160];
	int n, i;

	if (!c) return;
	romm_busy(a, "Getting your systems");
	n = romm_platforms(&g_romm, all, 64, why, sizeof why);
	if (n < 0) { pick_say(a, "RomM", why, MENU_ACCENT); free(c); return; }
	for (i = 0; i < n; i++) {
		if (!all[i].tag || !romm_folder_for(all[i].tag, c->folder[c->n], CFG_STR)) continue;
		c->p[c->n] = all[i];
		snprintf(c->counts[c->n], 16, "%d", all[i].roms);
		c->n++;
	}
	st.accent = MENU_ACCENT;
	st.fixed_w = menu_std_width(a);
	menu_run(a, &st, romm_sys_build, romm_sys_key, c);
	free(c);
}

/* ---- saves -------------------------------------------------------------------- */

typedef struct {
	romm_local_save *s;
	int n, cap;
	struct { int rom_id; char stem[LIB_NAME]; } *games;
	int ng, capg;
} romm_scan;

static bool romm_scan_one(int rom_id, const char *folder, const char *file, void *ctx)
{
	romm_scan *sc = ctx;
	const char *base = strrchr(file, '/');
	char stem[LIB_NAME], path[LIB_PATH * 2];
	struct stat st;

	(void)folder;
	snprintf(stem, sizeof stem, "%s", base ? base + 1 : file);
	if (strrchr(stem, '.')) *strrchr(stem, '.') = '\0';
	if (sc->ng == sc->capg) {
		void *m = realloc(sc->games, (size_t)(sc->capg = sc->capg ? sc->capg * 2 : 64) * sizeof *sc->games);

		if (!m) return false;
		sc->games = m;
	}
	sc->games[sc->ng].rom_id = rom_id;
	snprintf(sc->games[sc->ng].stem, LIB_NAME, "%s", stem);
	sc->ng++;
	snprintf(path, sizeof path, "%s/Saves/%s.srm", P_CARD, stem);
	if (stat(path, &st) != 0 || st.st_size <= 0) return true;
	if (sc->n == sc->cap) {
		void *m = realloc(sc->s, (size_t)(sc->cap = sc->cap ? sc->cap * 2 : 64) * sizeof *sc->s);

		if (!m) return false;
		sc->s = m;
	}
	memset(&sc->s[sc->n], 0, sizeof sc->s[0]);
	sc->s[sc->n].rom_id = rom_id;
	snprintf(sc->s[sc->n].file_name, ROMM_STR, "%s.srm", stem);
	if (!ra_md5_file(path, sc->s[sc->n].md5)) return true;
	sc->s[sc->n].mtime = st.st_mtime;
	sc->s[sc->n].size = (long)st.st_size;
	sc->n++;
	return true;
}

static const char *romm_stem_of(const romm_scan *sc, int rom_id)
{
	int i;

	for (i = 0; i < sc->ng; i++) if (sc->games[i].rom_id == rom_id) return sc->games[i].stem;
	return NULL;
}

/* Sync: what the server's plan says, for the games that came from RomM. A
 * save replaced is kept first, in Saves/.romm-backup; a conflict is sent up
 * as a new save beside the server's, so both are kept. */
static void romm_sync_screen(app *a)
{
	romm_scan sc;
	romm_sync_op ops[256];
	char rdir[LIB_PATH], why[160], msg[200];
	int n, session = 0, i, up = 0, down = 0, failed = 0;

	if (!net_online()) { pick_say(a, "Sync Saves", "Connect to Wi-Fi first", MENU_ACCENT); return; }
	memset(&sc, 0, sizeof sc);
	romm_dir(rdir, sizeof rdir);
	romm_busy(a, "Looking at your saves");
	romm_map_each(rdir, romm_scan_one, &sc);
	if (sc.ng == 0) {
		pick_say(a, "Sync Saves", "Download games from RomM first:\ntheir saves are what syncs", MENU_ACCENT);
		free(sc.s); free(sc.games);
		return;
	}
	if (!g_romm.device_id[0]) {
		char name[96];

		romm_device_name(name, sizeof name);
		if (!romm_register(&g_romm, name, why, sizeof why)) {
			pick_say(a, "Sync Saves", why, MENU_ACCENT);
			free(sc.s); free(sc.games);
			return;
		}
		romm_keep();
	}
	romm_busy(a, "Asking RomM what to sync");
	n = romm_negotiate(&g_romm, sc.s, sc.n, &session, ops, 256, why, sizeof why);
	if (n < 0) { pick_say(a, "Sync Saves", why, MENU_ACCENT); free(sc.s); free(sc.games); return; }
	for (i = 0; i < n; i++) {
		const char *stem = romm_stem_of(&sc, ops[i].rom_id);
		char path[LIB_PATH * 2];

		if (!stem) continue;                      /* a game not on this Brick */
		snprintf(path, sizeof path, "%s/Saves/%s.srm", P_CARD, stem);
		if (!strcmp(ops[i].action, "upload") || !strcmp(ops[i].action, "conflict")) {
			snprintf(msg, sizeof msg, "Sending %s's save", stem);
			romm_busy(a, msg);
			if (access(path, R_OK) == 0 && romm_save_upload(&g_romm, ops[i].rom_id, path, why, sizeof why)) up++;
			else failed++;
		} else if (!strcmp(ops[i].action, "download") && ops[i].save_id) {
			char tmp[LIB_PATH * 2 + 16];

			snprintf(msg, sizeof msg, "Getting %s's save", stem);
			romm_busy(a, msg);
			snprintf(tmp, sizeof tmp, "%s.romm", path);
			if (!romm_save_download(&g_romm, ops[i].save_id, tmp, why, sizeof why)) { failed++; continue; }
			if (access(path, F_OK) == 0) {
				/* the one here, kept */
				char bdir[LIB_PATH], bk[LIB_PATH * 2 + 40];

				snprintf(bdir, sizeof bdir, "%s/Saves/.romm-backup", P_CARD);
				mkdir(bdir, 0755);
				snprintf(bk, sizeof bk, "%s/%s.%ld.srm", bdir, stem, (long)time(NULL));
				rename(path, bk);
			}
			if (rename(tmp, path) == 0) down++; else failed++;
		}
	}
	romm_sync_complete(&g_romm, session, up + down, failed);
	sync();
	fprintf(stderr, "romm: sync sent %d, got %d, %d failed\n", up, down, failed);
	if (up + down + failed == 0) snprintf(msg, sizeof msg, "Everything is in sync");
	else snprintf(msg, sizeof msg, "Sent %d save%s, got %d%s", up, up == 1 ? "" : "s", down,
	              failed ? "\nSome couldn't be synced" : "");
	pick_say(a, "Sync Saves", msg, MENU_ACCENT);
	{
		char p[LIB_PATH];
		FILE *f;

		snprintf(p, sizeof p, "%s/last-sync", rdir);
		if ((f = fopen(p, "w"))) { fprintf(f, "%ld\n", (long)time(NULL)); fclose(f); }
	}
	free(sc.s);
	free(sc.games);
}

/* ---- BIOS --------------------------------------------------------------------- */

static void romm_bios_screen(app *a)
{
	romm_platform all[64];
	romm_firmware fw[32];
	char why[160], msg[200], bios[LIB_PATH];
	int n, i, k, nf, got = 0, had = 0, failed = 0;

	if (!net_online()) { pick_say(a, "BIOS", "Connect to Wi-Fi first", MENU_ACCENT); return; }
	romm_busy(a, "Asking RomM for BIOS files");
	n = romm_platforms(&g_romm, all, 64, why, sizeof why);
	if (n < 0) { pick_say(a, "BIOS", why, MENU_ACCENT); return; }
	snprintf(bios, sizeof bios, "%s/Bios", P_CARD);
	mkdir(bios, 0755);
	for (i = 0; i < n; i++) {
		char folder[CFG_STR];

		if (!all[i].tag || !all[i].firmware || !romm_folder_for(all[i].tag, folder, sizeof folder)) continue;
		nf = romm_firmware_list(&g_romm, all[i].id, fw, 32, why, sizeof why);
		for (k = 0; k < nf; k++) {
			char path[LIB_PATH * 2], url[1200];
			struct stat st;

			snprintf(path, sizeof path, "%s/%s", bios, fw[k].file_name);
			if (stat(path, &st) == 0 && (fw[k].size <= 0 || st.st_size == fw[k].size)) { had++; continue; }
			romm_firmware_url(&g_romm, &fw[k], url, sizeof url);
			if (romm_fetch(a, "BIOS", fw[k].file_name, url, path, fw[k].size, why, sizeof why)) got++;
			else if (!strcmp(why, "Stopped")) { i = n; break; }
			else failed++;
		}
	}
	fprintf(stderr, "romm: BIOS got %d, had %d, %d failed\n", got, had, failed);
	if (got + had + failed == 0) snprintf(msg, sizeof msg, "RomM has no BIOS files\nfor ChaOS's systems");
	else snprintf(msg, sizeof msg, "Got %d BIOS file%s%s%s", got, got == 1 ? "" : "s",
	              had ? ", the rest were here" : "", failed ? "\nSome couldn't be fetched" : "");
	pick_say(a, "BIOS", msg, MENU_ACCENT);
}

/* ---- RomM's page ------------------------------------------------------------ */

enum { RM_GAMES, RM_SYNC, RM_BIOS, RM_SERVER, RM_SIGNOUT };

typedef struct {
	char when[48], note[160];
} romm_home_ctx;

static void romm_home_refresh(romm_home_ctx *c)
{
	char p[LIB_PATH], d[LIB_PATH];
	FILE *f;
	long t = 0;

	romm_dir(d, sizeof d);
	snprintf(p, sizeof p, "%s/last-sync", d);
	if ((f = fopen(p, "r"))) { if (fscanf(f, "%ld", &t) != 1) t = 0; fclose(f); }
	if (!t) snprintf(c->when, sizeof c->when, "never");
	else {
		long age = (long)time(NULL) - t;

		if (age < 3600) snprintf(c->when, sizeof c->when, "just now");
		else if (age < 86400) snprintf(c->when, sizeof c->when, "%ld hr ago", age / 3600);
		else snprintf(c->when, sizeof c->when, "%ld day%s ago", age / 86400, age < 172800 ? "" : "s");
	}
	if (g_romm.token[0])
		snprintf(c->note, sizeof c->note, "Signed in%s%s", g_romm.user[0] ? " as " : "", g_romm.user);
	else
		snprintf(c->note, sizeof c->note, "Not signed in");
}

static int romm_home_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	romm_home_ctx *c = ctx;
	bool in = g_romm.token[0] != 0, net = net_online();
	int n = 0;
	const char *server = g_romm.url[0] ? g_romm.url + (strstr(g_romm.url, "://") ? strstr(g_romm.url, "://") - g_romm.url + 3 : 0)
	                                   : "set it";

	(void)max;
	*heading = "RomM";
	if (in) {
		rows[n] = (menu_row){ "Games", NULL, net, true };
		rows[n++].hint = "Download games from your library";
		rows[n] = (menu_row){ "Sync Saves", c->when, net, true };
		rows[n++].hint = "Saves of the games from RomM, both ways";
		rows[n] = (menu_row){ "BIOS", NULL, net, true };
		rows[n++].hint = "Get the BIOS files RomM has";
	}
	rows[n] = (menu_row){ "Server", server, true, true };
	rows[n++].hint = "Your RomM's address, like nas.local:8080";
	if (in) {
		rows[n] = (menu_row){ "Sign Out", g_romm.user[0] ? g_romm.user : NULL, true, true };
		rows[n++].hint = "Your games and saves stay";
	} else {
		rows[n] = (menu_row){ "Sign In", NULL, net && g_romm.url[0], true };
		rows[n++].hint = "Pair this Brick with your RomM";
	}
	(void)c;
	return n;
}

static menu_result romm_home_key(app *a, void *ctx, in_button key, int sel)
{
	romm_home_ctx *c = ctx;
	bool in = g_romm.token[0] != 0;
	int row = sel;

	if (key != IN_ACCEPT) return MENU_STAY;
	if (!in) row = sel == 0 ? RM_SERVER : RM_SIGNOUT;   /* Server, Sign In */
	switch (row) {
	case RM_GAMES: romm_systems_screen(a); break;
	case RM_SYNC:  romm_sync_screen(a); break;
	case RM_BIOS:  romm_bios_screen(a); break;
	case RM_SERVER: {
		char typed[ROMM_STR];
		kb_result kr;
		const char *u = g_romm.url;

		snprintf(typed, sizeof typed, "%s", strstr(u, "://") ? strstr(u, "://") + 3 : u);
		kr = kb_prompt(a->r, &a->in, "RomM Server", typed, (int)sizeof typed, MENU_ACCENT,
		               wifi_backdrop, idle_due_ctx, a);
		if (kr == KB_POWER) { power_off(a); return MENU_DONE; }
		if (kr == KB_ACCEPT) {
			if (!romm_set_url(&g_romm, typed)) pick_say(a, "RomM", "That isn't a server's address", MENU_ACCENT);
			else romm_keep();
		}
		break;
	}
	case RM_SIGNOUT:
		if (in) {
			if (confirm_panel(a, "Sign Out", "Forget this sign-in?\nYour games and saves stay.", "Sign Out")) {
				char d[LIB_PATH];

				romm_dir(d, sizeof d);
				romm_forget(&g_romm, d);
			}
		} else if (g_romm.url[0]) {
			romm_pair_screen(a);
		}
		break;
	default: break;
	}
	romm_home_refresh(c);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return g_menu_closing ? MENU_DONE : MENU_STAY;
}

static void romm_screen(app *a)
{
	romm_home_ctx c;
	menu_style st = { 0 };

	romm_get();
	memset(&c, 0, sizeof c);
	romm_home_refresh(&c);
	st.accent = MENU_ACCENT;
	st.fixed_w = menu_std_width(a);
	menu_run(a, &st, romm_home_build, romm_home_key, &c);
}

static void launch_game(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	/* The game's system, not the shelf's. Launching a favorite off the
	 * Favorites shelf otherwise loads whatever core the shelf claims, which
	 * is none, and looks for the ROM under a folder that does not exist. */
	int o = shelf_owner(a, a->sys_cursor, v->cursor);
	const system_cfg *s = &a->sys.systems[o];
	char core[CFG_STR * 2], elf[CFG_STR * 2], rom[LIB_PATH * 2];
	char st[LIB_PATH * 2], pv[LIB_PATH * 2], apv[LIB_PATH * 2];
	char save[CFG_STR * 2], bios[CFG_STR * 2];
	char active[LIB_PATH * 2] = "";
	char set[LIB_PATH * 2];
	int  console = 0;
	bool first_play = false;
	/* 18 fixed entries plus NULL, then two per core option. Sized off the
	 * loader's own cap so the two cannot drift apart: the previous 20 was
	 * already 18 full, and a silent bound check would have dropped every
	 * option rather than failing loudly. */
	char *argv[20 + 2 * 32];
	bool resident = false, want_menu;
	unsigned waited;
	int n = 0;

	if (v->list.count == 0) return;
	/* ChaOS: a port, not a game for Diatom */
	if (!strcmp(s->core, PORTS_CORE)) {
		/* ChaOS: its first card is PortMaster's catalog, not a port */
		if (pm_is_entry(v->list.items[v->cursor].file)) { pm_screen(a, s->accent); return; }
		port_launch(a, o, v->list.items[v->cursor].file, v->list.items[v->cursor].title);
		return;
	}

	snprintf(core, sizeof core, "%s/cores/%s_libretro.so", P_ROOT, s->core);
	snprintf(elf, sizeof elf, "%s/diatom", P_ROOT);
	/* Captured once, beside the path it goes into: play time is keyed on the
	 * launch path the same way favorites are, and reading the cursor again
	 * further down would be reading it at a different moment. */
	const char *romfile = v->list.items[v->cursor].file;

	snprintf(rom, sizeof rom, "%s/%s/%s", P_ROMS, s->folder, romfile);
	snprintf(save, sizeof save, "%s/Saves", P_CARD);
	snprintf(bios, sizeof bios, "%s/Bios", P_CARD);

	/* A disc that needs firmware, before anything tries to run it.
	 *
	 * Diatom checks this too and answers bios_missing, but that answer has
	 * never been reachable: it acts on a `firmware=` key the launcher has
	 * never sent. So the core was asked to load a CD with no System Card,
	 * refused it the way it refuses a corrupt ROM, and the shelf came back
	 * with nothing said - which is a long way from "needs Bios/syscard3.pce".
	 *
	 * Disc images only. A HuCard needs no System Card, and refusing every
	 * cartridge on the shelf because a CD would have needed one is a worse
	 * failure than the one being fixed. */
	if (s->disc_bios[0] && lib_is_disc(v->list.items[v->cursor].file)) {
		char fw[CFG_STR * 3];

		snprintf(fw, sizeof fw, "%s/%s", bios, s->disc_bios);
		if (access(fw, R_OK) != 0) {
			char msg[CFG_STR + 32];

			snprintf(msg, sizeof msg, "needs Bios/%s", s->disc_bios);
			wait_panel(a, v->list.items[v->cursor].title, msg);
			SDL_Delay(2200);
			plat_input_flush();
			memset(&a->in, 0, sizeof a->in);
			return;
		}
	}

	/* The autosave story, by path rather than by convention: the state and the
	 * preview live beside each other, the launch hands both over, and a game
	 * always comes up where it was left. */
	state_path(a, o, &v->list.items[v->cursor], st, sizeof st);
	/* The scratch, not the Auto card - see pause_preview_path. The card is
	 * written from this only when the game actually ends, below. */
	pause_preview_path(pv, sizeof pv);
	persist_dir_ensure(a, o);

	/* Achievements, if this game has any. Most of a library does not, and that
	 * is not a failure: chv_load says so by returning false and everything
	 * below carries on with console 0 and no set, which is what Diatom reads
	 * as "this game has none". */
	{
		chv_path(P_ROMS, s->folder, v->list.items[v->cursor].name,
		         set, sizeof set);
		/* Every launch, not just a first play: this is what a late fetch is
		 * checked against, and it has to name the game on screen now rather
		 * than the last one that went looking for a set. */
		snprintf(g_game_set, sizeof g_game_set, "%s", set);

		/* The async slot, cleared of an account answer from the last game that
		 * never landed, before this launch asks anything of its own. And no
		 * watch list to trim until this game has handed one over. */
		sync_abandon();
		g_watch_ms = 0;

		/* Fetch it if this game has never been played here. Only then: a
		 * cached set costs nothing and this is the launch path, so the delay
		 * is paid once per game rather than every time. Failing is ordinary -
		 * offline, not signed in, or a game RetroAchievements has never seen -
		 * and the launch carries on without. */
		/* Never played here, so there is no set to hand over. The game starts
		 * anyway and the set is found behind it - see ra_fetch_begin. Nothing
		 * in front of the launch. */
		first_play = ra_signed_in() && access(set, R_OK) != 0 && net_online();

		if (chv_load(set)) {
			/* Reconcile with the account BEFORE deciding what to watch.
			 * Without this the launcher filters against what this device
			 * happens to have seen, which is not the same question and does
			 * not look different: measured 2026-08-29 as 3 of 40 for Contra
			 * against the site's own 13. */
			/* The account's answer, without the launch waiting for it - read
			 * while the game runs, see sync_poll. On a first play there is
			 * nothing to ask about yet; that question is asked once the set
			 * arrives. */
			if (!first_play) sync_begin(chv_game(), set);


			/* No "startsession" request, deliberately. It drives the "currently
			 * playing" indicator on the website and nothing on the device, and
			 * it is another 320ms request - which is the exact trade this whole
			 * path exists to refuse. */

			chv_active_path(active, sizeof active);
			if (chv_write_active(active)) {
				console = chv_console();
			} else {
				/* Everything in the set is already earned. Nothing to watch,
				 * and sending an empty file would have Diatom log a set with
				 * no achievements in it every launch. */
				active[0] = '\0';
			}
		}
	}

	remember_place(a);
	/* Before the launch, not after: a device that loses power during the load
	 * was still playing this game. */
	playing_set(a);

	/* Read and cleared together. Leaving it set would have the NEXT game
	 * launched in this session open with its menu up too, which is the sort
	 * of thing that looks like a haunting rather than a bug. */
	want_menu = a->resume_menu;
	a->resume_menu = false;

	/* Whether this game should be heard - quiet while Muse plays, Diatom's
	 * QUIET. Stated before the RUN goes out, which sends it. */
	plat_resident_quiet(musec_playing());
	/* And whether the battery is low, so the disc is over its first frame. */
	plat_resident_low_battery(battery_level());

	/* A game resumed at boot waited 71 and 47 ms for the resident on the
	 * Pixel 2 (2026-10-05, a DAC plugged in), where it had been falling back
	 * unseen. A second is ten times that, and only ever spent on a Diatom
	 * that is running but stuck; one that isn't running falls back at once. */
	if (plat_resident_await(1000, &waited)) {
		if (waited)
			fprintf(stderr, "resident emulator answered after %u ms\n", waited);
		/* The emulator is already up, holding its context and every core,
		 * so this is ~200ms rather than ~1100. Nothing here is torn
		 * down -- this process keeps its own context through the whole game,
		 * which is why coming back is a frame rather than a second and a
		 * half. The two do overlap for the length of this animation, while
		 * the game loads behind it; after that only the emulator draws,
		 * because this process is blocked - except when the player opens
		 * the in-game menu, which is drawn HERE now, over the frame the
		 * emulator hands us on the way into its pause. */
		aout_before_launch();
		/* ChaOS: the console's shader and overlay go with the RUN. */
		{
			const char *lcfg, *ldtov;

			look_files(a, o, &lcfg, &ldtov);
			plat_resident_set_look(lcfg, ldtov);
			plat_resident_set_display(DMODES[a->view[o].dmode].name);
			{
				char tz[64];

				/* The zone the Date & Time menu chose, sent with each game */
				db_get_str(db_lib(), "timezone", tz, sizeof tz, "America/New_York");
				plat_resident_set_zone(tz);
			}
		}
		/* ChaOS: where its screenshots go (MENU+X, and the in-game menu) */
		shots_dir(P_CARD, a->sys.systems[o].folder, v->list.items[v->cursor].file,
		          g_shots_dir, sizeof g_shots_dir);
		/* ChaOS: its buttons, and rewind, both by the console it belongs to */
		{
			char spec[512];
			const char *otag = a->sys.systems[o].tag;

			plat_resident_set_play(remap_spec(otag, v->list.items[v->cursor].file,
			                                  spec, sizeof spec),
			                       rewind_load(otag));
		}
		if (plat_resident_send(s->tag, core, rom, st, st, pv,
		                       console, active[0] ? active : NULL)) {
			int r;

			/* The list Diatom was just handed, for an early answer to trim. */
			if (active[0]) {
				g_watch_ms = plat_now_ms();
				snprintf(g_watch_active, sizeof g_watch_active, "%s", active);
			}

			/* Play time starts here, and costs a clock read. Nothing is
			 * opened or written - the launch path does no I/O for this, on
			 * purpose, because a millisecond bought here is the first of
			 * several. See stats.h. */
			stats_begin(s->tag, romfile, plat_now_ms());


			/* Auto Off. Out here rather than inside the chv_load branch
			 * above, which is where the first version put it and is the same
			 * mistake the paragraph below describes: that branch is the one a
			 * game with no achievements never takes, so Auto Off only ever
			 * worked for games that happened to have a cached set.
			 *
			 * Unless on the charger - the feature exists to stop a game
			 * running unattended on battery, and plugged in there is no
			 * battery reason and no cost to leaving it. on_game_tick keeps
			 * this true if the cable changes mid-game. */
			g_idle_secs = a->auto_off;
			g_idle_charging = on_charger();
			plat_resident_line("SETIDLE\tms=%d",
			                   (g_idle_secs > 0 && !g_idle_charging)
			                       ? g_idle_secs * 1000 : 0);

			/* The first play of this game: nothing was cached, so there is a
			 * set to go and find. Out here and not inside the chv_load branch
			 * above, which is the mistake the first version made - that
			 * branch is precisely the one a first play does not take.
			 *
			 * Hashing is local work and happens after RUN, so it overlaps the
			 * game's own startup rather than delaying it. The two requests
			 * then run behind the game and on_game_tick hands the set over
			 * when they land. */
			if (first_play) {
				char h[33];

				if (ra_hash_rom(rom, s->tag, h)) {
					chv_active_path(g_pending_active, sizeof g_pending_active);
					snprintf(g_pending_set, sizeof g_pending_set, "%s", set);
					ra_fetch_begin(h, set);
				}
			}
			/* Coming back from a shutdown: the game loads and the menu is
			 * already up, so nothing is handed control of a game the player
			 * may not have meant to resume. Ordered on the same socket, so it
			 * lands before the first frame the player could act on. */
			if (want_menu) plat_resident_line("PAUSE");
			/* No launch animation, and it is a display-safety rule, not a
			 * taste call: Diatom presents through fbdev, this process
			 * through GL, and the handoff spike's one invariant is that
			 * they never present concurrently - a 190ms overlap that is
			 * harmless GL-on-GL is the exact case that wedges the display
			 * engine when one side is fbdev. A warm launch is 26 to 44 ms from
			 * RUN to RUNNING, so there is nothing to animate over anyway; the
			 * shelf simply holds until the game's first frame replaces it. */
			for (;;) {
				r = plat_resident_wait();
				if (r == RES_PAUSED) { game_menu(a); continue; }
				break;
			}
			resident = (r == RES_EXIT);

			/* The Auto card's picture, written ONLY here - at the same moment
			 * Diatom wrote the state it belongs to, so the two cannot drift.
			 * Between exits the card keeps the previous session's frame, which
			 * is exactly what resuming would give you. */
			if (resident) {
				char ap[LIB_PATH * 2];

				preview_path(a, o, &v->list.items[v->cursor],
				             ap, sizeof ap);
				copy_file(plat_resident_last_preview(), ap);
			}

			/* The game is over, so this is the one write that is synced -
			 * on the path where the player is waiting for the shelf rather
			 * than for a game to start. */
			stats_end(NULL, plat_now_ms());

			/* Whatever was earned is sent now, behind the shelf rather than in
			 * front of it - see ra_flush_start. It used to be sent right here,
			 * blocking, on the reasoning that the wait loop above is the power
			 * button's watchdog and so the first safe moment was after it. True
			 * of that loop, and not the only place the watchdog matters: this
			 * spot is between that loop and the shelf's, so the button was
			 * read by neither while it waited. Whatever will not send stays
			 * queued. */
			/* After the account's answer, so anything it already held is
			 * settled before deciding what is owed - otherwise the flush would
			 * cheerfully submit a dozen duplicates. The answer has usually been
			 * in since half a second into the game. If it is still on its way,
			 * the sending waits for it rather than the shelf - see sync_poll.
			 *
			 * The game is passed rather than read later: by the time a waiting
			 * send starts, the info screen may have loaded another set. */
			g_watch_ms = 0;
			sync_poll();
			if (g_sync_game) {
				g_sync_flush = true;
				snprintf(g_sync_rom, sizeof g_sync_rom, "%s", rom);
				snprintf(g_sync_tag, sizeof g_sync_tag, "%s", s->tag);
				g_sync_cur = chv_game();
				fprintf(stderr, "ra: the account's answer is still on its way; "
				                "sending waits for it\n");
			} else {
				ra_flush_start(rom, s->tag, chv_game());
			}

			/* The game's play time and last-played moment are in now, so the
			 * Recently Played shelf is rebuilt before the player sees it. The
			 * last use of s, v and o in this branch: refreshing moves them. */
			refresh_virtual_shelves(a);

			/* RES_DEAD means it stopped answering -- it died, or the game
			 * never started. Say so by falling through to the path that
			 * runs it the slow way, rather than fading the shelf back up
			 * as though it had run. */
			if (!resident)
				fprintf(stderr, "resident emulator stopped answering, falling back\n");
		} else {
			fprintf(stderr, "resident emulator did not answer, falling back\n");
		}
	} else {
		/* Silent until 2026-10-05, which is how a resumed game running in
		 * the fallback went unnoticed. */
		fprintf(stderr, "no resident emulator to connect to (waited %u ms), falling back\n",
		        waited);
	}

	if (!resident) {
		/* One game per process, the old way: the fallback for a resident that
		 * is missing or has died. Diatom standalone IS the one-shot mode -
		 * same binary, no socket - so the fallback stopped being a different
		 * emulator and became the same one held differently. It has to take
		 * the display, so this side tears its own down first. */
		anim_launch(a, 190);
		free_all_textures(a);
		texload_fonts(false);   /* the workers' fonts go before TTF_Quit */
		ui_quit();
		plat_input_quit();
		plat_video_quit();

		argv[n++] = elf;
		argv[n++] = (char *)"--core";            argv[n++] = core;
		argv[n++] = (char *)"--rom";             argv[n++] = rom;
		argv[n++] = (char *)"--save";            argv[n++] = save;
		argv[n++] = (char *)"--system";          argv[n++] = bios;
		argv[n++] = (char *)"--display";
		argv[n++] = (char *)DMODES[a->view[o].dmode].name;   /* ChaOS: the console's */
		/* The channels too: this path is rare, but a player who hears with
		 * one ear should not lose half a game to it. It plays on the codec,
		 * so the cable decides between headphones and the speaker. */
		{
			aout_state cs = aout_now();

			argv[n++] = (char *)"--channels";
			argv[n++] = (char *)aout_channels_wire(
				aout_channels_for(g_channels, aout_dest_of(&cs, "")));
		}
		argv[n++] = (char *)"--load-state";      argv[n++] = st;
		argv[n++] = (char *)"--state-on-exit";   argv[n++] = st;
		/* The Auto card's own path here, not the scratch the resident mode
		 * uses. Standalone has no in-game menu - the launcher is torn down -
		 * so nothing can overwrite it mid-session, and it is written once at
		 * exit beside the state. The resident path needs the indirection
		 * because a pause writes the preview too; this one does not. */
		preview_path(a, o, &v->list.items[v->cursor], apv, sizeof apv);
		argv[n++] = (char *)"--preview-on-exit"; argv[n++] = apv;
		/* Same opinions as the resident path gets over SETOPT, so a game plays
		 * the same whether the resident was up or the fallback ran it. */
		{
			int ci;
			for (ci = 0; ci < plat_coreopt_count(s->tag) && n < (int)(sizeof argv / sizeof argv[0]) - 3; ci++) {
				argv[n++] = (char *)"--core-option";
				argv[n++] = (char *)plat_coreopt(s->tag, ci);
			}
		}
		argv[n] = NULL;
		fprintf(stderr, "diatom exited %d\n", plat_run(argv, NULL, P_ROOT));

		if (!plat_video_init() || !plat_input_init()) { a->running = false; return; }
		a->r = plat_renderer();
		ui_init(a->r, P_FONT);
		texload_fonts(true);
		a->menu_w = 0;   /* fonts reopened: remeasure the panel */
		prime_sys_window(a);
		prime_window(a, a->sys_cursor);
		/* The display is ours again and no game is running: the safe moment
		 * to put a resident emulator back, so the NEXT launch is fast. */
		respawn_resident(a);
	}

	t_back0 = plat_now_ms();
	present_black(a);

	/* The game has just written a fresh autosave preview. Throw the card's
	 * texture away so it picks that up - but only if the preview is what the
	 * card is drawn FROM.
	 *
	 * Box art comes first in the order a card finds its picture, so for a game
	 * that has it this used to rebuild the card from a file that had not
	 * changed, into a pixel-identical texture. That was always wasted work, and
	 * it was invisible while decoding blocked the frame: the card was back
	 * before anything drew. Since decoding moved to a worker, the card is
	 * genuinely absent for a few frames, and the one you just quit popped in
	 * while every card beside it was already there. The question was never
	 * "did the preview change", it is "is this card showing the preview". */
	{
		game_entry *g = &v->list.items[v->cursor];
		int o = shelf_owner(a, a->sys_cursor, v->cursor);

		if (v->tex[v->cursor] && !has_box_art(a->sys.systems[o].folder, g->name)) {
			SDL_DestroyTexture(v->tex[v->cursor]);
			v->tex[v->cursor] = NULL;
		}
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	/* The idle clock is not restarted here, though it has been stopped for
	 * the whole session. idle_due notices the gap on its own, which is what
	 * stops this from being a list of places to remember. */

	if (access(TORTOS_POWEROFF_FLAG, F_OK) == 0) {
		a->running = false;
		return;
	}
	/* Power was pressed during the game. The emulator no longer handles that
	 * key itself, so the press arrived here. */
	if (plat_run_power_pressed()) { power_off(a); return; }

	/* Past both power checks, so this is a real return to the shelf rather
	 * than a shutdown. Everything above leaves the marker standing, which is
	 * what makes the next boot able to tell them apart. */
	playing_clear();

	/* Straight to the shelf, no fade. The card decode inside this render is
	 * the only real cost left on the way back, and a fade laid over the top of
	 * it is time spent easing in a picture the player has already been looking
	 * at all the way up to the moment they quit. render() is right here: it
	 * draws and presents exactly once. */
	render(a);
	fprintf(stderr, "exit: back in %u ms\n", plat_now_ms() - t_back0);
}

/* ---------- a card from the systems row ----------------------------------- */

/* Ten presses, the last in the lowest four bits, each an in_button. */
#define SEQ_KEYS 0x2233010154ull
#define SEQ_LEN  10

/* The texts, base64: a header the build generates (mk/notes.sh). */
#include "notes.h"
#define SEQ_COUNT ((int)(sizeof SEQ_TEXT / sizeof SEQ_TEXT[0]))

/* The last presses on the systems row, newest in the lowest four bits. 0xF
 * is no button, so a fresh start matches nothing. */
static unsigned long long g_seq_hist = ~0ull;

/* Whether the newest k presses are the sequence's first k. */
static bool seq_prefix(unsigned long long hist, int k)
{
	unsigned long long mask = (1ull << (4 * k)) - 1;

	return (hist & mask) == ((SEQ_KEYS >> (4 * (SEQ_LEN - k))) & mask);
}

/* Feeds this frame's press. The sequence's last two, B and A, are taken from
 * the shelf when the presses before them match, so A doesn't open a system;
 * true when A completes it. */
static bool seq_step(in_state *in)
{
	in_button got = IN_NONE;
	bool done = false;
	int b, n = 0;

	for (b = IN_LEFT; b <= IN_MENU; b++)
		if (in->pressed[b]) { got = (in_button)b; n++; }
	if (n == 0) return false;
	if (n > 1) { g_seq_hist = ~0ull; return false; }

	if (got == IN_BACK && seq_prefix(g_seq_hist, SEQ_LEN - 2))
		in->pressed[IN_BACK] = false;
	if (got == IN_ACCEPT && seq_prefix(g_seq_hist, SEQ_LEN - 1)) {
		in->pressed[IN_ACCEPT] = false;
		done = true;
	}
	g_seq_hist = done ? ~0ull : (g_seq_hist << 4) | (unsigned)got;
	return done;
}

/* Standard base64, no line breaks. */
static void seq_decode(const char *src, char *out, size_t outn)
{
	static const char abc[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	unsigned acc = 0;
	int bits = 0;
	size_t n = 0;

	for (; *src && *src != '='; src++) {
		const char *p = strchr(abc, *src);

		if (!p) continue;
		acc = (acc << 6) | (unsigned)(p - abc);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			if (n + 1 < outn) out[n++] = (char)((acc >> bits) & 0xFF);
		}
	}
	out[n] = '\0';
}

/* The next text, in a shuffled order that goes through them all before any
 * comes round again, and never the same one twice running. Fresh each boot. */
static const char *seq_next(void)
{
	static int order[SEQ_COUNT], at = SEQ_COUNT, last = -1;
	int i;

	if (at == SEQ_COUNT) {
		srand((unsigned)time(NULL) ^ plat_now_ms());
		for (i = 0; i < SEQ_COUNT; i++) order[i] = i;
		for (i = SEQ_COUNT - 1; i > 0; i--) {
			int j = rand() % (i + 1), t = order[i];

			order[i] = order[j];
			order[j] = t;
		}
		if (order[0] == last) {
			int t = order[0];

			order[0] = order[SEQ_COUNT - 1];
			order[SEQ_COUNT - 1] = t;
		}
		at = 0;
	}
	last = order[at++];
	return SEQ_TEXT[last];
}

/* The card: the text over the mark, faint, on the dimmed shelf. A or B
 * closes it; MENU, power and Auto Off do what they do on the shelf. */
static void seq_card(app *a)
{
	char text[256], lines[CHV_WRAP_LINES][CHV_WRAP_COLS];
	TTF_Font *f = ui_font(UI_F_MENU);
	int line = ui_font_line(UI_F_MENU), pad = menu_pad();
	int w = menu_std_width(a), n, i, done = 0;
	SDL_Rect panel;

	seq_decode(seq_next(), text, sizeof text);
	n = wrap_text(f, text, w, lines, CHV_WRAP_LINES);

	panel.w = w + pad * 2;
	panel.h = n * line + pad * 4;
	if (panel.h < TORTOS_SCREEN_H / 3) panel.h = TORTOS_SCREEN_H / 3;
	panel.x = (TORTOS_SCREEN_W - panel.w) / 2;
	panel.y = (TORTOS_SCREEN_H - panel.h) / 2;

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);

	while (!done && !want_quit) {
		plat_input_poll(&a->in);

		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_ACCEPT]) done = 1;
		if (a->in.pressed[IN_MENU]) done = 1;
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); break; }

		chv_backdrop(a, NULL, true);
		ui_glow(a->r, &panel, MENU_ACCENT, 60, 1.5f);
		ui_panel(a->r, &panel, MENU_RADIUS, MENU_ACCENT);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		/* ChaOS's logo, faint behind the text, the panel's width */
		draw_chaos_mark(a->r, 0.0f, 30, panel.w * 0.8f / (49.0f * CHAOS_CELL),
		                panel.x + panel.w / 2 - TORTOS_SCREEN_W / 2.0f,
		                panel.y + panel.h / 2 - TORTOS_SCREEN_H / 2.0f);
		for (i = 0; i < n; i++)
			ui_text(a->r, f, lines[i], panel.x + panel.w / 2,
			        panel.y + (panel.h - n * line) / 2 + i * line, 0, UI_TEXT);
		plat_present();
		SDL_Delay(8);
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
}

/* ---------- input --------------------------------------------------------- */

static void enter_system(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	/* Muse too: its cards are albums, and opening one is the games shelf's
	 * own A - see update_games. */
	if (v->list.count == 0) return;
	a->screen = SCREEN_GAMES;
	cf_reset(&v->cf, v->cursor);
	prime_window(a, a->sys_cursor);
}

/* Both: two axes, no entering and no going back.
 *
 * Up and down turn to another system, left and right move through its games.
 * The game cursor is per system already - sysview keeps one each - so turning
 * away and back returns you to where you were rather than to the top.
 *
 * MENU is the firmware's menu and B is this system's. Everywhere else those
 * are the same button meaning different things depending on which shelf you
 * are on; here there is one shelf, so they cannot be told apart that way and
 * each gets its own button. B is free precisely because there is nothing to
 * go back to. */
static void update_both(app *a)
{
	sysview *v;
	int ns = a->sys.count, ng, sdir = 0, gdir = 0;

	if (ns <= 0) return;

	if (a->in.pressed[IN_MENU]) { tortos_menu_for(a, SCREEN_SYSTEMS); return; }
	if (a->in.pressed[IN_BACK]) { tortos_menu_for(a, SCREEN_GAMES); return; }

	/* Up advances: the shelf runs A at the bottom to Z at the top. Tried the
	 * other way on 2026-09-11 and put back the same day - a stack is read from
	 * the bottom, and every part of this agreeing did not make it look right. */
	if (in_repeat(&a->in, IN_DOWN)) { a->sys_cursor = (a->sys_cursor - 1 + ns) % ns; sdir = -1; }
	if (in_repeat(&a->in, IN_UP))   { a->sys_cursor = (a->sys_cursor + 1) % ns; sdir = +1; }
	if (sdir) cf_set_cursor_dir(&a->cf_sys, a->sys_cursor, ns, sdir);

	/* Re-read after a system turn: v, its list and its cursor all moved. */
	v = &a->view[a->sys_cursor];
	ng = v->list.count;
	if (ng <= 0) return;
	if (!v->cf.primed) cf_reset(&v->cf, v->cursor);

	if (in_repeat(&a->in, IN_LEFT))  { v->cursor = (v->cursor - 1 + ng) % ng; gdir = -1; }
	if (in_repeat(&a->in, IN_RIGHT)) { v->cursor = (v->cursor + 1) % ng; gdir = +1; }
	if (in_repeat(&a->in, IN_L1))    { v->cursor = ((v->cursor - CF_WINDOW) % ng + ng) % ng; gdir = -1; }
	if (in_repeat(&a->in, IN_R1))    { v->cursor = (v->cursor + CF_WINDOW) % ng; gdir = +1; }
	/* ChaOS: L2 and R2 to the first game of a letter, each press, with the
	 * letter shown; and the letter passing by once left or right is held */
	if (in_repeat(&a->in, IN_R2)) { v->cursor = shelf_letter_jump(v, +1); gdir = +1; letter_note(v); }
	if (in_repeat(&a->in, IN_L2)) { v->cursor = shelf_letter_jump(v, -1); gdir = -1; letter_note(v); }
	letter_hold(v, a->in.down[IN_LEFT] || a->in.down[IN_RIGHT]);
	if (gdir) cf_set_cursor_dir(&v->cf, v->cursor, ng, gdir);

	/* Muse's row: its cards are albums, and A opens one onto its tracks, as on
	 * Muse's own shelf (muse_shelf_screen). X and Y are free - an album is not
	 * a favorite, and the play mode is set on Now Playing. SELECT and B from
	 * the tracks come back to this row. */
	if (is_muse(&a->sys.systems[a->sys_cursor])) {
		if (a->in.pressed[IN_ACCEPT]) muse_album(a, v, v->cursor);
		return;
	}

	if (a->in.pressed[IN_X]) { game_info_screen(a); return; }
	if (a->in.pressed[IN_Y]) {
		fav_toggle(a->sys.systems[shelf_owner(a, a->sys_cursor, v->cursor)].tag,
		           v->list.items[v->cursor].file);
		fav_save();
		refresh_virtual_shelves(a);
		return;
	}
	if (a->in.pressed[IN_ACCEPT]) { launch(a); return; }
}

/* Start on a system's cards when the systems row moves onto it, not when A is
 * pressed.
 *
 * Entering used to be the first time anything asked for a system's covers, so
 * the games shelf drew with empty slots until they arrived. Measured
 * 2026-09-14, cold after a reboot, across six systems: the focused card 70-160ms
 * after A, the whole row 95-177ms. Short, and visible. Leaving a system keeps
 * the cards around its cursor, which is why only the first entry showed it.
 *
 * Starting here gives the decode however long it takes to reach the system and
 * press A, and that is enough: re-measured the same way, four systems entered
 * after a reboot all had every card already loaded on arrival.
 *
 * Whatever was queued for the system just passed is dropped first, so a
 * held direction does not decode a row for every system it crosses, and the one
 * the row stops on is not left waiting behind them.
 *
 * The visible row only; entering asks for two more either side. They stay after
 * the row moves on, which is less than already stays: a system entered and left
 * keeps nine. */
static void load_ahead(app *a)
{
	texload_forget();
	prime_toward(a, a->sys_cursor, a->view[a->sys_cursor].cursor);
}

static void update_systems(app *a)
{
	int n = a->sys.count;

	/* No systems means the modulo below divides by zero. Reachable the moment
	 * empty systems started being hidden: a card with no ROMs on it, or ROMs
	 * one directory too deep, now leaves nothing on the shelf and the first
	 * press of left or right took the launcher down with SIGFPE. */
	if (n <= 0) return;

	/* The direction is carried through, not inferred from the cursor: on a
	 * shelf of two, moving from either card to the other is one step in BOTH
	 * directions, and only the press says which. */
	/* The d-pad axis follows the shelf, and vertically the list runs UPWARD:
	 * the first system at the bottom, the last at the top, so up advances.
	 * Down-is-next is the convention for a list on a page; a stack is read
	 * from the bottom, and this shelf is a stack. */
	in_button back = CARD_DIRS[g_dir].vertical ? IN_DOWN : IN_LEFT;
	in_button fwd  = CARD_DIRS[g_dir].vertical ? IN_UP : IN_RIGHT;
	int dir = 0;

	if (in_repeat(&a->in, back)) { a->sys_cursor = (a->sys_cursor - 1 + n) % n; dir = -1; }
	if (in_repeat(&a->in, fwd))  { a->sys_cursor = (a->sys_cursor + 1) % n; dir = +1; }
	if (dir) load_ahead(a);
	/* Muse's card opens Muse, the one SELECT opens - see update_games. */
	if (a->in.pressed[IN_ACCEPT]) {
		if (is_muse(&a->sys.systems[a->sys_cursor])) muse_open(a, NULL, NULL);
		else enter_system(a);
	}
	cf_set_cursor_dir(&a->cf_sys, a->sys_cursor, n, dir);
}

/* The first alphanumeric character of a name, upper-cased, or '\0' for a name
 * with none at all - which groups those few together rather than giving each
 * one a group of its own. Leading articles and punctuation are deliberately
 * NOT skipped: lib_scan sorts the shelf with strcasecmp on this same string,
 * so grouping that disagreed with the order the cards are drawn in would make
 * the jump land somewhere that looks arbitrary. "The Legend of Zelda" files
 * under T here because it sits under T on the shelf. */
static char shelf_initial(const char *s)
{
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c >= 'a' && c <= 'z') return (char)(c - 32);
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (char)c;
	}
	return '\0';
}

/* The first game sharing idx's initial, walking backwards from it. */
/* What the letter jump reads: the title, or on Muse's shelf the artist. */
static const char *shelf_key(const sysview *v, int i)
{
	return v->jump_by_name ? v->list.items[i].name : v->list.items[i].title;
}

/* ---- ChaOS: the letter popup -------------------------------------------------
 *
 * A square in the menus' style with the letter the cursor is on, big, in the
 * middle of the screen: while L2 and R2 scrub a shelf from one letter to the
 * next, and once moving along it has been held for two seconds. It fades in,
 * and out again a moment after the scrubbing or the holding stops. Every
 * theme, both Game List Views, and Cubic's games. */
static char   g_lp_letter;
static Uint32 g_lp_last;           /* the last time it was asked for */
static Uint32 g_lp_since;          /* when it came up from nothing */
static Uint32 g_lp_held;           /* when the current hold began, or 0 */

static void letter_note(sysview *v)
{
	Uint32 now = plat_now_ms();
	char c;

	if (v->list.count <= 0) return;
	c = shelf_initial(v->jump_by_name ? v->list.items[v->cursor].name
	                                  : v->list.items[v->cursor].title);
	g_lp_letter = c ? c : '#';
	if (!g_lp_last || now - g_lp_last > 1100) g_lp_since = now;
	g_lp_last = now;
}

/* The same for a list that isn't a shelf (PortMaster's) */
static void letter_show(char c)
{
	Uint32 now = plat_now_ms();

	g_lp_letter = c ? c : '#';
	if (!g_lp_last || now - g_lp_last > 1100) g_lp_since = now;
	g_lp_last = now;
}

/* Called each pass with whether moving is held: two seconds of it shows the
 * letter, for as long as it goes on */
static void letter_hold(sysview *v, bool held)
{
	Uint32 now = plat_now_ms();
	if (!held) { g_lp_held = 0; return; }
	if (!g_lp_held) g_lp_held = now;
	if (now - g_lp_held >= 2000) letter_note(v);
}

static void draw_letter_popup(app *a)
{
	static SDL_Texture *layer;
	static TTF_Font *big;
	static char drawn;
	const int S = 220;
	Uint32 now = plat_now_ms(), el;
	float al;

	if (!g_lp_last) return;
	el = now - g_lp_last;
	/* up while it is being asked for and 0.7 s after, then out over 0.4 s;
	 * in over 0.12 s from nothing */
	if (el >= 1100) { g_lp_last = 0; return; }
	al = el < 700 ? 1.0f : 1.0f - (float)(el - 700) / 400.0f;
	if (now - g_lp_since < 120) {
		float in = (float)(now - g_lp_since) / 120.0f;
		if (in < al) al = in;
	}
	/* A renderer made again since (after a port or a game the launcher
	 * let go of the screen for) has none of the old one's textures, and
	 * TTF_Quit took the font's library with it: both made anew, the old
	 * ones left alone, as nothing can safely free them now */
	{
		static SDL_Renderer *made_for;

		if (made_for != a->r) { layer = NULL; big = NULL; drawn = 0; made_for = a->r; }
	}
	if (!big) big = TTF_OpenFont(P_FONT, 150);
	if (!layer) {
		layer = SDL_CreateTexture(a->r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, S, S);
		if (!layer) return;
		SDL_SetTextureBlendMode(layer, SDL_BLENDMODE_BLEND);
		drawn = 0;
	}
	if (drawn != g_lp_letter) {
		SDL_Texture *was = SDL_GetRenderTarget(a->r);
		SDL_Rect q = { 0, 0, S, S };
		char t[2] = { g_lp_letter, 0 };

		if (SDL_SetRenderTarget(a->r, layer) == 0) {
			SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_NONE);
			SDL_SetRenderDrawColor(a->r, 0, 0, 0, 0);
			SDL_RenderClear(a->r);
			SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
			ui_panel(a->r, &q, 28, menu_accent());
			if (big) {
				SDL_Surface *ts = TTF_RenderUTF8_Blended(big, t, UI_TEXT);
				if (ts) {
					SDL_Texture *tt = SDL_CreateTextureFromSurface(a->r, ts);
					/* centred on the ink: a capital's top to its baseline */
					int mnx, mxx, mny, mxy, adv, asc = TTF_FontAscent(big);
					int cap = TTF_GlyphMetrics(big, (Uint16)g_lp_letter, &mnx, &mxx, &mny,
					                           &mxy, &adv) == 0 ? mxy : asc;
					SDL_Rect d = { (S - ts->w) / 2, (S - cap) / 2 - (asc - cap), ts->w, ts->h };
					if (tt) { SDL_RenderCopy(a->r, tt, NULL, &d); SDL_DestroyTexture(tt); }
					SDL_FreeSurface(ts);
				}
			}
			drawn = g_lp_letter;
		}
		SDL_SetRenderTarget(a->r, was);
	}
	{
		SDL_Rect d = { (TORTOS_SCREEN_W - S) / 2, (TORTOS_SCREEN_H - S) / 2, S, S };
		SDL_SetTextureAlphaMod(layer, (Uint8)(al * 255));
		SDL_RenderCopy(a->r, layer, NULL, &d);
	}
	redraw_now();
}

static int shelf_group_start(sysview *v, int idx)
{
	int n = v->list.count, i = idx;
	char c = shelf_initial(shelf_key(v, idx));

	for (;;) {
		int p = (i - 1 + n) % n;
		if (p == idx) return idx;             /* one initial, the whole shelf */
		if (shelf_initial(shelf_key(v, p)) != c) break;
		i = p;
	}
	return i;
}

/* Up and down cross the shelf one initial at a time, which on a long library
 * is the difference between forty presses and two. Down lands on the first
 * game of the next initial. Up lands on the first game of THIS one, and moves
 * to the previous initial only when the cursor is already there - so from any
 * group's first game, up and down are exact inverses. */
static int shelf_letter_jump(sysview *v, int dir)
{
	int n = v->list.count, i, cur = v->cursor;
	char c0 = shelf_initial(shelf_key(v, cur));

	if (n <= 1) return cur;
	if (dir > 0) {
		for (i = 1; i < n; i++) {
			int k = (cur + i) % n;
			if (shelf_initial(shelf_key(v, k)) != c0) return k;
		}
		return cur;                           /* every name starts alike */
	}
	i = shelf_group_start(v, cur);
	if (i != cur) return i;
	/* Already at the top of the group. If what precedes it shares the initial
	 * then the shelf is one group and there is nowhere to go - the same answer
	 * down gives, rather than shuffling back by one. */
	i = (cur - 1 + n) % n;
	if (shelf_initial(shelf_key(v, i)) == c0) return cur;
	return shelf_group_start(v, i);
}

static void update_games(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	int n = v->list.count;
	if (n <= 0) { a->screen = SCREEN_SYSTEMS; return; }
	/* MUSE HAS ONE DOOR. Its card used to open its shelf HERE, in the main
	 * loop, as if it were a console: a second Muse beside the one SELECT
	 * opens, so SELECT on it opened that one on top, and the two looked
	 * alike. Eric's call, 2026-09-23: the card opens the same Muse SELECT
	 * does. Whatever lands the main loop on Muse's shelf - A on its card, a
	 * jump to it, a screen put back at boot - hands over to muse_open, which
	 * comes back to the card on the systems row. */
	if (is_muse(&a->sys.systems[a->sys_cursor])) {
		a->screen = SCREEN_SYSTEMS;
		muse_open(a, NULL, NULL);
		return;
	}
	/* The two axes trade roles with the shelf rather than one of them being
	 * reassigned onto the other: stepping always runs along the row, and the
	 * letter jump always runs across it. Turning the shelf turns the d-pad
	 * with it and nothing is left doing two jobs. */
	bool vert = CARD_DIRS[g_dir].vertical;
	in_button back = vert ? IN_DOWN : IN_LEFT, fwd = vert ? IN_UP : IN_RIGHT;
	in_button jup = vert ? IN_LEFT : IN_UP, jdn = vert ? IN_RIGHT : IN_DOWN;
	int dir = 0;

	/* ChaOS: a List reads top to bottom, as every menu does: down is the
	 * next game, and left and right the letters */
	if (LIST_VIEW_ON()) { back = IN_UP; fwd = IN_DOWN; jup = IN_LEFT; jdn = IN_RIGHT; }

	if (in_repeat(&a->in, back)) { v->cursor = (v->cursor - 1 + n) % n; dir = -1; }
	if (in_repeat(&a->in, fwd))  { v->cursor = (v->cursor + 1) % n; dir = +1; }
	if (in_repeat(&a->in, jdn))  v->cursor = shelf_letter_jump(v, +1);
	if (in_repeat(&a->in, jup))  v->cursor = shelf_letter_jump(v, -1);
	/* ChaOS: L2 and R2 scrub by letter, with the letter shown; holding
	 * the way along for two seconds shows it too */
	if (in_repeat(&a->in, IN_R2)) { v->cursor = shelf_letter_jump(v, +1); letter_note(v); }
	if (in_repeat(&a->in, IN_L2)) { v->cursor = shelf_letter_jump(v, -1); letter_note(v); }
	letter_hold(v, a->in.down[back] || a->in.down[fwd]);
	/* L1/R1 jump a screenful, so a long shelf is crossable. Wrapped the long
	 * way round on purpose: C's % truncates toward zero, so on a shelf of
	 * three games the obvious `(cursor - CF_WINDOW + n*2) % n` lands on -1 and
	 * the draw walks off the front of the list. */
	if (in_repeat(&a->in, IN_L1))    v->cursor = ((v->cursor - CF_WINDOW) % n + n) % n;
	if (in_repeat(&a->in, IN_R1))    v->cursor = (v->cursor + CF_WINDOW) % n;
	/* HERE, AND NOT AT THE END OF THE PASS, because four of the handlers below
	 * return without reaching the end.
	 *
	 * A repeat fires from the clock every REPEAT_RATE_MS while a direction is
	 * held, and the loop passes at about 60Hz while any button is down, so
	 * roughly one pass in five moves the cursor. Press A on one of those passes
	 * and the shelf was never told: `target` stays a card behind `cursor`, the
	 * game runs, the render on the way back snaps the cards to that stale
	 * target, and the NEXT pass through here finally notices and animates the
	 * difference. Which is exactly the report - the shelf comes back showing a
	 * game off to the side and then slides across to the right one - and the
	 * one-in-five is why it only happened sometimes.
	 *
	 * X and Y are the same shape: the info screen and a favorite toggle both
	 * return from here too. B is the only one that got away with it, because
	 * enter_system resets this coverflow on the way back in.
	 *
	 * The call is unconditional and `dir` may be 0: a letter jump and L1/R1
	 * move the cursor without a direction, and cf_set_cursor_dir works out the
	 * rest itself. It returns immediately when the cursor has not moved. */
	cf_set_cursor_dir(&v->cf, v->cursor, n, dir);
	if (a->in.pressed[IN_BACK]) {
		a->screen = SCREEN_SYSTEMS;
		evict_far(v, TEX_KEEP_FAR);
		return;
	}
	/* X opens the game. Y marks it. The pair sits together because they are
	 * the two things you do to a card without launching it. */
	if (a->in.pressed[IN_X] && n > 0) { game_info_screen(a); return; }
	/* Y favorites what is under the cursor. Written through immediately:
	 * there is no confirm step to hang the save off, and the alternative is
	 * losing the choice to a flat battery. */
	if (a->in.pressed[IN_Y] && n > 0) {
		const char *ftag = a->sys.systems[shelf_owner(a, a->sys_cursor, v->cursor)].tag;
		bool was_fav = fav_is(ftag, v->list.items[v->cursor].file);
		bool now_fav = fav_toggle(ftag, v->list.items[v->cursor].file);

		fav_save();
		/* ChaOS: say so - a dot on the card was all Y ever answered. Neither
		 * before nor after is a full list refusing one more. */
		shelf_say(now_fav ? "Added to Favorites"
		          : was_fav ? "Removed from Favorites" : "Favorites is full", 1500);
		/* The shelf follows immediately. Everything below this line may have
		 * moved - the system indices, sys_cursor, and v itself - so nothing
		 * from before it can be reused. */
		refresh_virtual_shelves(a);
		return;
	}
	if (a->in.pressed[IN_ACCEPT]) { launch(a); return; }
}

/* ---------- main ---------------------------------------------------------- */

/* Drop systems with nothing in them.
 *
 * A card is a promise that opening it leads somewhere, and nine cards where
 * three have games is eight swipes to find the one you wanted. systems.cfg
 * stays the full list of what TortOS knows how to run; this is only what is
 * worth showing today.
 *
 * Compacted in place rather than filtered at draw time because sys_cursor
 * indexes sys.systems[] directly in a dozen places - the tint, the card art,
 * the launch, the resume - and an index layer over all of them would be a lot
 * of surface for a cosmetic rule. Both arrays move together or the shelf
 * shows one system's card over another's games.
 *
 * Textures are not freed here: this runs before prime_sys_window, so there
 * are none yet. */
static void hide_empty_systems(app *a)
{
	int i, n = 0;

	for (i = 0; i < a->sys.count; i++) {
		if (a->view[i].list.count <= 0) continue;
		if (n != i) {
			a->sys.systems[n] = a->sys.systems[i];
			a->view[n] = a->view[i];
			memset(&a->view[i], 0, sizeof a->view[i]);
		}
		n++;
	}
	if (n != a->sys.count)
		fprintf(stderr, "scan: %d of %d systems have games\n", n, a->sys.count);
	a->sys.count = n;
	if (a->sys_cursor >= n) a->sys_cursor = n ? n - 1 : 0;
}

/* ---- Virtual shelves: Favorites and Recently Played ------------------------
 *
 * Two shelves whose games come from every other system. Neither is a system:
 * no core, no folder, no extensions. Nothing may read those from a->sys.systems[]
 * for a game on one of them - shelf_owner() is how every one of them is found.
 *
 * They sit at the front of the systems row, Favorites first, and each is there
 * only while it has something to show: Favorites while a game is favorited,
 * Recently Played while a game has been played and its switch is on.
 *
 * Every change drops both and builds both again. The rebuild moves every
 * index, so the callers find their place by TAG afterwards, never by index. */
#define VSHELF_FAV "FAV"
#define VSHELF_REC "REC"
#define RECENT_MAX 12             /* the games Recently Played lists, newest first */


static int vshelf_index(app *a, const char *tag)
{
	int i;

	for (i = 0; i < a->sys.count; i++)
		if (!strcmp(a->sys.systems[i].tag, tag)) return i;
	return -1;
}

/* Take a virtual shelf off, wherever it is. Its game list and owner map are
 * this file's own allocations, and its game_entry values are copies - the
 * originals belong to the shelves they came from and are not touched. */
static void drop_virtual_shelf(app *a, const char *tag)
{
	int i = vshelf_index(a, tag), k;

	if (i < 0) return;
	for (k = 0; k < a->view[i].list.count; k++)
		if (a->view[i].tex && a->view[i].tex[k]) SDL_DestroyTexture(a->view[i].tex[k]);
	free(a->view[i].tex);
	free(a->view[i].tw);
	free(a->view[i].th);
	free(a->view[i].cb);
	free(a->view[i].list.items);
	free(a->view[i].owner);
	if (a->sys_tex[i]) SDL_DestroyTexture(a->sys_tex[i]);

	for (k = i; k + 1 < a->sys.count; k++) {
		a->sys.systems[k] = a->sys.systems[k + 1];
		a->view[k] = a->view[k + 1];
		a->sys_tex[k] = a->sys_tex[k + 1];
		a->sys_w[k] = a->sys_w[k + 1];
		a->sys_cb[k] = a->sys_cb[k + 1];
		a->sys_h[k] = a->sys_h[k + 1];
	}
	a->sys.count--;
	memset(&a->view[a->sys.count], 0, sizeof a->view[0]);
	a->sys_tex[a->sys.count] = NULL;
}

/* Put a shelf of `n` games at the front of the systems row. The caller owns
 * items and owner until this returns; on failure they are freed here. Returns
 * whether the shelf went in. */
static bool vshelf_insert(app *a, const char *name, const char *tag, const char *card,
                          game_entry *items, int *owner, int n, int sort)
{
	int i;

	if (a->sys.count >= CFG_MAX_SYSTEMS) {
		fprintf(stderr, "scan: no room for %s\n", name);
		free(items); free(owner);
		return false;
	}
	for (i = a->sys.count; i > 0; i--) {
		a->sys.systems[i] = a->sys.systems[i - 1];
		a->view[i] = a->view[i - 1];
		a->sys_tex[i] = a->sys_tex[i - 1];
		a->sys_w[i] = a->sys_w[i - 1];
		a->sys_cb[i] = a->sys_cb[i - 1];
		a->sys_h[i] = a->sys_h[i - 1];
	}
	a->sys_tex[0] = NULL;
	for (i = 0; i < n; i++) owner[i]++;          /* everything moved up one */

	memset(&a->sys.systems[0], 0, sizeof a->sys.systems[0]);
	snprintf(a->sys.systems[0].name, CFG_STR, "%s", name);
	snprintf(a->sys.systems[0].tag, sizeof a->sys.systems[0].tag, "%s", tag);
	snprintf(a->sys.systems[0].card, CFG_STR, "%s", card);
	a->sys.systems[0].accent = MENU_ACCENT;      /* TortOS's, not a console's */

	memset(&a->view[0], 0, sizeof a->view[0]);
	a->view[0].list.items = items;
	a->view[0].list.count = n;
	a->view[0].list.scanned = true;
	a->view[0].owner = owner;
	a->view[0].sort = sort;
	a->view[0].tex = calloc((size_t)n, sizeof *a->view[0].tex);
	a->view[0].tw  = calloc((size_t)n, sizeof *a->view[0].tw);
	a->view[0].th  = calloc((size_t)n, sizeof *a->view[0].th);
	/* cb with the rest. The scan allocates these four together, and a shelf
	 * missing one would crash on the first card it draws. */
	a->view[0].cb  = calloc((size_t)n, sizeof *a->view[0].cb);
	if (!a->view[0].tex || !a->view[0].tw || !a->view[0].th || !a->view[0].cb) {
		free(a->view[0].tex); free(a->view[0].tw); free(a->view[0].th);
		free(a->view[0].cb);
		free(items); free(owner);
		memset(&a->view[0], 0, sizeof a->view[0]);
		for (i = 0; i < a->sys.count; i++) {
			a->sys.systems[i] = a->sys.systems[i + 1];
			a->view[i] = a->view[i + 1];
			a->sys_tex[i] = a->sys_tex[i + 1];
			a->sys_w[i] = a->sys_w[i + 1];
			a->sys_cb[i] = a->sys_cb[i + 1];
			a->sys_h[i] = a->sys_h[i + 1];
		}
		return false;
	}
	a->sys.count++;
	fprintf(stderr, "scan: %-16s %d game%s\n", name, n, n == 1 ? "" : "s");
	return true;
}

/* Build the Favorites shelf: the favorited games, resolved against the shelves
 * that actually scanned, so a favorite whose ROM is off the card simply does
 * not appear - which is the same rule as hiding an empty system. Sorted by name
 * like every other shelf rather than by the order someone pressed Y, unless the
 * shelf's own Sort By says otherwise. */
static void build_favorites_shelf(app *a)
{
	game_entry *items;
	int *owner;
	int n = 0, i, k, si, real;

	if (!g_fav_shelf || fav_count() <= 0 || a->sys.count <= 0) return;

	items = calloc(FAV_MAX, sizeof *items);
	owner = calloc(FAV_MAX, sizeof *owner);
	if (!items || !owner) { free(items); free(owner); return; }

	real = a->sys.count;
	for (i = 0; i < fav_count(); i++) {
		const char *tag, *file;
		bool found = false;
		if (!fav_at(i, &tag, &file)) continue;
		for (si = 0; si < real; si++) {
			if (strcmp(a->sys.systems[si].tag, tag)) continue;
			for (k = 0; k < a->view[si].list.count; k++) {
				if (strcmp(a->view[si].list.items[k].file, file)) continue;
				items[n] = a->view[si].list.items[k];
				owner[n] = si;
				n++;
				found = true;
				break;
			}
			break;
		}
		/* Said out loud. A favorite that does not resolve is either a ROM
		 * that has left the card or a key that never matched, and silently
		 * showing one fewer game than the file lists is the kind of thing
		 * that gets noticed months later. */
		if (!found) fprintf(stderr, "fav: unresolved %s\t%s\n", tag, file);
		if (n >= FAV_MAX) break;
	}
	if (n == 0) { free(items); free(owner); return; }

	/* Insertion sort on the shown title, carrying the owner with it. n is at
	 * most FAV_MAX and realistically a dozen. */
	for (i = 1; i < n; i++) {
		game_entry t = items[i];
		int to = owner[i], j = i - 1;
		while (j >= 0 && strcasecmp(items[j].name, t.name) > 0) {
			items[j + 1] = items[j];
			owner[j + 1] = owner[j];
			j--;
		}
		items[j + 1] = t;
		owner[j + 1] = to;
	}

	vshelf_insert(a, "Favorites", VSHELF_FAV, "FAVORITES.png", items, owner, n, 0);
}

/* Build the Recently Played shelf: the games played most recently, newest first,
 * across every system, at most RECENT_MAX. Only while the switch is on and a game
 * has a last-played time. Muse is skipped - its cards are albums, not games. */
typedef struct { int sys, file; long at; } recent_pick;

static int recent_newest_first(const void *x, const void *y)
{
	long a = ((const recent_pick *)x)->at, b = ((const recent_pick *)y)->at;

	return a < b ? 1 : a > b ? -1 : 0;
}

static void build_recent_shelf(app *a)
{
	recent_pick *picks;
	game_entry *items;
	int *owner;
	int si, k, total = 0, m = 0, n, i;

	if (!g_recent_shelf || a->sys.count <= 0) return;
	for (si = 0; si < a->sys.count; si++)
		if (!is_muse(&a->sys.systems[si])) total += a->view[si].list.count;
	if (total <= 0) return;

	picks = calloc((size_t)total, sizeof *picks);
	if (!picks) return;
	for (si = 0; si < a->sys.count; si++) {
		if (is_muse(&a->sys.systems[si])) continue;
		for (k = 0; k < a->view[si].list.count; k++) {
			long secs = 0, at = 0;

			stats_lookup(a->sys.systems[si].tag, a->view[si].list.items[k].file,
			             &secs, &at);
			if (at <= 0 || m >= total) continue;
			picks[m].sys = si;
			picks[m].file = k;
			picks[m].at = at;
			m++;
		}
	}
	if (m == 0) { free(picks); return; }
	qsort(picks, (size_t)m, sizeof *picks, recent_newest_first);

	n = m < RECENT_MAX ? m : RECENT_MAX;
	items = calloc((size_t)n, sizeof *items);
	owner = calloc((size_t)n, sizeof *owner);
	if (!items || !owner) { free(items); free(owner); free(picks); return; }
	for (i = 0; i < n; i++) {
		items[i] = a->view[picks[i].sys].list.items[picks[i].file];
		owner[i] = picks[i].sys;
	}
	free(picks);
	vshelf_insert(a, "Recently Played", VSHELF_REC, "RECENT.png", items, owner, n,
	              sort_index("recent"));
}

/* Both virtual shelves, Recently Played first so that Favorites lands in front
 * of it: each insert goes to the front. */
static void build_virtual_shelves(app *a)
{
	/* stats_lookup reads whatever stats_summarize last folded, and nothing folds
	 * at boot: without this, Recently Played found no play at all, and the Play
	 * Time and Last Played orders found none either, until the stats screen had
	 * been opened. Folded here, before any shelf asks. */
	stats_summarize(STATS_ALL, false, (long)time(NULL));
	build_recent_shelf(a);
	build_favorites_shelf(a);
}

/* Rebuild both virtual shelves in place, for use the moment a favorite changes,
 * a game is played, or the Recently Played switch is flipped. Restarting the
 * launcher to see a star take effect is not an answer.
 *
 * Drop and rebuild rather than patch: each shelf is a sorted projection of a set
 * over every other shelf, and the cases - it appears, grows, shrinks, goes away -
 * are one line each this way. The order each one had is kept by tag, so a Sort
 * By chosen on a shelf survives its rebuild. */
static void refresh_virtual_shelves(app *a)
{
	static const char *const tags[] = { VSHELF_FAV, VSHELF_REC };
	char tag[sizeof a->sys.systems[0].tag];
	int cur, i, k, sorts[2] = { 0, 0 }, had[2] = { 0, 0 };

	texload_bump();
	if (a->sys.count <= 0) return;
	snprintf(tag, sizeof tag, "%s", a->sys.systems[a->sys_cursor].tag);
	cur = a->view[a->sys_cursor].cursor;
	for (k = 0; k < 2; k++) {
		i = vshelf_index(a, tags[k]);
		if (i >= 0) { sorts[k] = a->view[i].sort; had[k] = 1; }
	}

	drop_virtual_shelf(a, VSHELF_FAV);
	drop_virtual_shelf(a, VSHELF_REC);
	build_virtual_shelves(a);
	for (k = 0; k < 2; k++) {
		i = vshelf_index(a, tags[k]);
		if (i < 0) continue;
		if (had[k]) a->view[i].sort = sorts[k];
		sort_shelf(a, i);
	}

	i = vshelf_index(a, tag);
	if (i >= 0) {
		a->sys_cursor = i;
		/* The list can shrink under the cursor - un-favoriting the game you are
		 * looking at is the ordinary way to get here. */
		if (cur >= a->view[i].list.count)
			cur = a->view[i].list.count ? a->view[i].list.count - 1 : 0;
		a->view[i].cursor = cur;
		cf_reset(&a->view[i].cf, cur);
	} else {
		/* The shelf being stood on no longer exists: un-favoriting the last
		 * favorite while inside Favorites, or switching Recently Played off
		 * while on it. There is no list to stay in, so go back out to the
		 * shelves. */
		a->sys_cursor = 0;
		a->screen = SCREEN_SYSTEMS;
	}
	cf_reset(&a->cf_sys, a->sys_cursor);
}

/* Muse's shelf: a card per album, and which album each card is. Built in the
 * folder's own order, by artist; the order chosen for it is put on it by
 * sort_all once the setting has been read, the way every shelf's is. */
static void muse_fill_view(sysview *v)
{
	int n = g_muse.nalbums;

	if (n <= 0) return;
	v->list.items = calloc((size_t)n, sizeof *v->list.items);
	v->tex = calloc((size_t)n, sizeof *v->tex);
	v->tw = calloc((size_t)n, sizeof *v->tw);
	v->th = calloc((size_t)n, sizeof *v->th);
	v->cb = calloc((size_t)n, sizeof *v->cb);
	v->album = calloc((size_t)n, sizeof *v->album);
	if (!v->list.items || !v->tex || !v->tw || !v->th || !v->cb || !v->album) {
		free(v->list.items); free(v->tex); free(v->tw); free(v->th); free(v->cb);
		free(v->album);
		memset(v, 0, sizeof *v);
		return;
	}
	v->list.count = n;
	v->list.scanned = true;
	muse_order_view(v);
}

/* Muse's shelf in its order, v->sort: which album each card is, and each
 * card's entry from its album - the artist in `name`, the album in `title`,
 * which the card is called, and its folder in `file`, which is how a change
 * of order finds the album that was selected. Filled afresh rather than
 * sorted in place: an entry is only ever a copy of the library's names. The
 * letter jump goes by whatever the shelf is in order of. */
static void muse_order_view(sysview *v)
{
	int k;

	/* The arrays hold every album; the shelf is the kind it shows. */
	if (!v->album) return;
	v->list.count = ml_shelf_order(&g_muse, (ml_order)v->sort, muse_books_shown(),
	                               v->album);
	for (k = 0; k < v->list.count; k++) {
		game_entry *e = &v->list.items[k];
		int al = v->album[k];

		snprintf(e->name, sizeof e->name, "%s", g_muse.artists[muse_artist_of(al)].name);
		snprintf(e->title, sizeof e->title, "%s", g_muse.albums[al].name);
		muse_album_dir(al, e->file, sizeof e->file);
	}
	v->jump_by_name = v->sort == ML_BY_ARTIST;
}

/* A free name for `name` in `dir`: itself, or with " (2)", " (3)" ... before
 * its extension. False when none fits. */
static bool free_name(const char *dir, const char *name, char *out, size_t n)
{
	const char *dot = strrchr(name, '.');
	int stem = dot ? (int)(dot - name) : (int)strlen(name);
	int k;

	if (snprintf(out, n, "%s/%s", dir, name) >= (int)n) return false;
	for (k = 2; access(out, F_OK) == 0; k++) {
		if (k > 99) return false;
		if (snprintf(out, n, "%s/%.*s (%d)%s", dir, stem, name, k, dot ? dot : "")
		    >= (int)n) return false;
	}
	return true;
}

/* Songs loose at the top of Music/, in no folder, moved into Music/Singles,
 * where Muse reads them as an album (ml_album's `singles`): loose, they were
 * on no shelf. Eric's call, 2026-10-05. A name already taken there gets " (2)"
 * and on, so nothing is overwritten. And Singles gets its cover, shipped with
 * TortOS (tools/gensingles.py), at Music/.media/Singles.png, where every
 * album's is kept, unless it already has one. A rename on the same card, so
 * it costs a folder read when there is nothing to move. */
static void muse_tidy_singles(void)
{
	char music[LIB_PATH], singles[LIB_PATH + 16];
	char from[LIB_PATH * 2], to[LIB_PATH * 2 + 16];
	char (*loose)[256] = NULL;
	int nloose = 0, cap = 0, moved = 0, i;
	struct dirent *e;
	struct stat st;
	DIR *d;

	snprintf(music, sizeof music, "%s/Music", P_CARD);
	snprintf(singles, sizeof singles, "%s/%s", music, ML_SINGLES);
	if (!(d = opendir(music))) return;
	/* Listed first, moved after: renaming out of a folder being read is
	 * allowed to show or skip entries. */
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || !ml_is_audio(e->d_name)) continue;
		if (snprintf(from, sizeof from, "%s/%s", music, e->d_name) >= (int)sizeof from ||
		    stat(from, &st) != 0 || !S_ISREG(st.st_mode) ||
		    strlen(e->d_name) >= sizeof loose[0])
			continue;
		if (nloose == cap) {
			char (*more)[256] = realloc(loose, (size_t)(cap ? cap * 2 : 16) * sizeof *loose);

			if (!more) break;
			loose = more;
			cap = cap ? cap * 2 : 16;
		}
		snprintf(loose[nloose++], sizeof loose[0], "%s", e->d_name);
	}
	closedir(d);
	for (i = 0; i < nloose; i++) {
		mkdir(singles, 0755);
		snprintf(from, sizeof from, "%s/%s", music, loose[i]);
		if (free_name(singles, loose[i], to, sizeof to) && rename(from, to) == 0) moved++;
		else fprintf(stderr, "muse: could not move %s into %s\n", loose[i], ML_SINGLES);
	}
	free(loose);
	if (moved)
		fprintf(stderr, "muse: moved %d loose song%s into Music/%s\n",
		        moved, moved == 1 ? "" : "s", ML_SINGLES);
	singles_cover();
}

/* Singles' cover, the one shipped with TortOS, at Music/.media/Singles.png,
 * once there is a Singles and unless it has a cover already. At every scan,
 * and when the transfer screen opens, since a cover taken off the card there
 * was otherwise back only at the next restart (2026-10-05). */
static void singles_cover(void)
{
	char music[LIB_PATH], singles[LIB_PATH + 16], cover[LIB_PATH * 2], src[LIB_PATH * 2];
	struct stat st;

	snprintf(music, sizeof music, "%s/Music", P_CARD);
	snprintf(singles, sizeof singles, "%s/%s", music, ML_SINGLES);
	if (stat(singles, &st) != 0 || !S_ISDIR(st.st_mode)) return;
	snprintf(cover, sizeof cover, "%s/.media/%s.jpg", music, ML_SINGLES);
	if (file_nonempty(cover)) return;
	snprintf(cover, sizeof cover, "%s/.media/%s.png", music, ML_SINGLES);
	if (file_nonempty(cover)) return;
	snprintf(src, sizeof src, "%s/.media", music);
	mkdir(src, 0755);
	snprintf(src, sizeof src, "%s/singles.png", P_ROOT);
	copy_file(src, cover);
}

/* ---- Muse: albums filed under their artists -----------------------------
 *
 * An album folder straight in Music/ is its own artist on the shelf, so
 * Trompe Le Monde sorted under T rather than with the Pixies. Its tags say who
 * it is by, and Muse's player reads them (src/muse/tags.h): with a clear
 * answer it moves to Music/<artist>/<album>, its cover and Album Art's memory
 * of it with it, and the shelf is read again once. Eric's call, 2026-10-05.
 *
 * Left where it is when the tags give no clear answer (a podcast, a mix), when
 * the answer is its own name, when Music/<artist>/<album> is already there,
 * and for now when Muse is playing from it. Never Singles, never a book. The
 * player is not up at the first scan of a boot, so this happens a moment
 * after, one album at a time, from the main loop. */
typedef struct { char dir[LIB_PATH]; unsigned asked; bool done; } flat_album;
static flat_album *g_flat;
static int         g_nflat;
static bool        g_muse_moved;            /* something moved: read it again */

/* The albums straight in Music/, after a scan. */
static void flat_albums_find(void)
{
	int i;

	free(g_flat);
	g_nflat = 0;
	g_flat = calloc((size_t)(g_muse.nalbums > 0 ? g_muse.nalbums : 1), sizeof *g_flat);
	if (!g_flat) return;
	for (i = 0; i < g_muse.nalbums; i++) {
		const ml_album *al = &g_muse.albums[i];
		const char *p, *slash;

		if (al->book || al->singles || al->n <= 0) continue;
		p = g_muse.tracks[al->first].path;            /* Music/<album>/<track> */
		if (strncmp(p, "Music/", 6) || !(slash = strchr(p + 6, '/')) ||
		    strchr(slash + 1, '/'))
			continue;
		snprintf(g_flat[g_nflat++].dir, sizeof g_flat[0].dir, "%.*s", (int)(slash - p), p);
	}
}

/* An artist's name as a folder: what the card cannot store in one becomes -,
 * and no leading or trailing space or trailing dot. */
static void folder_name(const char *in, char *out, size_t n)
{
	size_t o = 0, len;

	while (*in == ' ') in++;
	for (; *in && o + 1 < n; in++)
		out[o++] = strchr("/\\:*?\"<>|", *in) || (unsigned char)*in < 0x20 ? '-' : *in;
	out[o] = '\0';
	for (len = o; len && (out[len - 1] == ' ' || out[len - 1] == '.'); len--) out[len - 1] = '\0';
}

/* Move Music/<album> under its artist. 1 moved, 0 left where it is, -1 not
 * now (Muse is playing from it). */
static int file_album(const char *rel, const char *artist)
{
	const char *album = strrchr(rel, '/') + 1;
	char who[256], from[LIB_PATH * 2], dir[LIB_PATH * 2], to[LIB_PATH * 2 + 256];
	char cfrom[LIB_PATH * 2 + 300], cto[LIB_PATH * 2 + 600], key[LIB_PATH + 300], val[96];
	static const char *const EXT[] = { "jpg", "png" };
	size_t rl = strlen(rel);
	const char *t;
	int k;

	folder_name(artist, who, sizeof who);
	if (!who[0] || !strcmp(who, album) || !strcmp(who, ML_SINGLES)) return 0;
	for (k = 0; (t = musec_track(k)); k++)
		if (!strncmp(t, rel, rl) && t[rl] == '/') return -1;

	snprintf(from, sizeof from, "%s/%s", P_CARD, rel);
	snprintf(dir, sizeof dir, "%s/Music/%s", P_CARD, who);
	snprintf(to, sizeof to, "%s/%s", dir, album);
	if (access(to, F_OK) == 0) {
		fprintf(stderr, "muse: Music/%s stays: Music/%s/%s is there already\n",
		        album, who, album);
		return 0;
	}
	mkdir(dir, 0755);
	if (rename(from, to) != 0) {
		fprintf(stderr, "muse: could not move Music/%s under Music/%s\n", album, who);
		return 0;
	}
	/* Its cover, from Music/.media to the artist's. */
	for (k = 0; k < 2; k++) {
		snprintf(cfrom, sizeof cfrom, "%s/Music/.media/%s.%s", P_CARD, album, EXT[k]);
		if (access(cfrom, F_OK) != 0) continue;
		snprintf(cto, sizeof cto, "%s/.media", dir);
		mkdir(cto, 0755);
		snprintf(cto, sizeof cto, "%s/.media/%s.%s", dir, album, EXT[k]);
		rename(cfrom, cto);
	}
	/* And what Album Art remembers of it, so it is not fetched again. */
	snprintf(key, sizeof key, "museart.%s", rel);
	if (db_get_str(db_lib(), key, val, sizeof val, "") && val[0]) {
		db_del(db_lib(), key);
		snprintf(key, sizeof key, "museart.Music/%s/%s", who, album);
		db_set_str(db_lib(), key, val);
	}
	fprintf(stderr, "muse: filed Music/%s under Music/%s\n", album, who);
	return 1;
}

/* Whether any album is still waiting for its answer: the shelf is read again
 * once they all have one, not once per album moved. */
static bool flat_albums_waiting(void)
{
	int i;

	for (i = 0; i < g_nflat; i++)
		if (!g_flat[i].done) return true;
	return false;
}

/* One step: take what the player has answered, and ask about the next album.
 * From the main loop and the transfer screen's. */
static void muse_file_albums(void)
{
	char dir[LIB_PATH * 2], name[256];
	unsigned now = plat_now_ms();
	int i;

	while (musec_artist_take(dir, sizeof dir, name, sizeof name))
		for (i = 0; i < g_nflat; i++) {
			int r;

			if (g_flat[i].done || strcmp(g_flat[i].dir, dir)) continue;
			r = name[0] ? file_album(dir, name) : 0;
			if (r < 0) { g_flat[i].asked = 0; break; }   /* playing: ask later */
			g_flat[i].done = true;
			if (r > 0) g_muse_moved = true;
			break;
		}
	/* One at a time: the next once this one is answered, or after five
	 * seconds for an answer lost with the player. */
	for (i = 0; i < g_nflat; i++) {
		if (g_flat[i].done) continue;
		if (g_flat[i].asked && now - g_flat[i].asked < 5000) return;
		if (musec_artist_ask(g_flat[i].dir)) g_flat[i].asked = now ? now : 1;
		return;
	}
}

/* Muse's card, at the end of the shelf, when there is music to play.
 *
 * After Favorites and after the empty systems are hidden, so it is neither
 * compacted away for having no games nor shifted by Favorites being inserted
 * in front: it is appended, and every later insertion moves it along with
 * everything else. Its view holds the albums - see muse_fill_view - so A on
 * the card opens a shelf of their covers, the way a console's opens its
 * games.
 *
 * Hidden when the folder is empty or missing, the same rule an empty system
 * and an empty Favorites shelf follow: a card should lead somewhere. */
static void build_muse_shelf(app *a)
{
	int i;

	/* Everything indexed by album goes with the albums: a rescan can put a
	 * different album at every index. */
	g_muse_gen++;
	g_nflat = 0;
	ml_free(&g_muse);
	free(g_cov);
	g_cov = NULL;
	free(g_book_done);
	g_book_done = NULL;
	np_forget();
	snprintf(g_muse_root, sizeof g_muse_root, "%s", P_CARD);
	muse_tidy_singles();
	if (!ml_scan_card(g_muse_root, &g_muse) || g_muse.ntracks == 0) {
		fprintf(stderr, "scan: %-16s no music or books in %s\n", "Muse", g_muse_root);
		return;
	}
	g_cov = calloc((size_t)g_muse.nalbums, sizeof *g_cov);
	g_book_done = calloc((size_t)g_muse.nalbums, sizeof *g_book_done);
	if (!g_book_done) { ml_free(&g_muse); return; }
	flat_albums_find();
	for (i = 0; i < g_muse.nalbums; i++) {
		char key[LIB_PATH + 8], val[16];

		if (!g_muse.albums[i].book) continue;
		book_key(i, key, sizeof key);
		db_get_str(db_dev(), key, val, sizeof val, "");
		g_book_done[i] = !strcmp(val, "finished");
	}
	{
		char show[16];

		db_get_str(db_dev(), "muse.show", show, sizeof show, "music");
		g_muse_books = !strcmp(show, "books");
	}
	fprintf(stderr, "scan: %-16s %d artists, %d albums, %d books, %d tracks\n", "Muse",
	        g_muse.nartists, ml_count(&g_muse, false), ml_count(&g_muse, true),
	        g_muse.ntracks);
	/* ChaOS: the library is read either way, for SELECT; the card goes on
	 * the row only with Appearance's Show Muse on, or while SELECT has it */
	if (g_muse_shelf || g_muse_attach) muse_shelf_attach(a);
}

/* Muse's card on the end of the row, its view its albums */
static void muse_shelf_attach(app *a)
{
	system_cfg *s;
	int i;

	if (g_muse.ntracks == 0 || a->sys.count >= CFG_MAX_SYSTEMS) return;
	for (i = 0; i < a->sys.count; i++)
		if (is_muse(&a->sys.systems[i])) return;
	i = a->sys.count;
	s = &a->sys.systems[i];
	memset(s, 0, sizeof *s);
	snprintf(s->name, CFG_STR, "%s", "Muse");
	snprintf(s->tag, sizeof s->tag, "%s", "MUSE");
	snprintf(s->card, CFG_STR, "%s", "MUSE.png");
	s->accent = MUSE_ACCENT;
	memset(&a->view[i], 0, sizeof a->view[i]);
	muse_fill_view(&a->view[i]);
	a->sys_tex[i] = NULL;
	a->sys_w[i] = a->sys_h[i] = 0;
	a->sys_cb[i] = 0;
	a->sys.count++;
	/* Its order, as sort_load would read it, for an attach after boot */
	{
		char key[CFG_STR + 16], name[CFG_STR];

		snprintf(key, sizeof key, "sort.%s", s->tag);
		if (db_get_str(db_dev(), key, name, sizeof name, NULL))
			a->view[i].sort = (int)ml_order_index(name);
	}
	look_load_one(a, i);
	sort_shelf(a, i);
}

/* ChaOS: and off again. It is always last, so nothing else moves. */
static void muse_shelf_detach(app *a)
{
	int i = a->sys.count - 1, k;
	sysview *v;

	if (i < 0 || !is_muse(&a->sys.systems[i])) return;
	v = &a->view[i];
	for (k = 0; v->tex && k < v->list.count; k++)
		if (v->tex[k]) SDL_DestroyTexture(v->tex[k]);
	free(v->tex); free(v->tw); free(v->th); free(v->cb); free(v->album);
	free(v->list.items);
	if (a->sys_tex[i]) SDL_DestroyTexture(a->sys_tex[i]);
	memset(v, 0, sizeof *v);
	a->sys_tex[i] = NULL;
	a->sys.count--;
	if (a->sys_cursor >= a->sys.count) {
		a->sys_cursor = a->sys.count > 0 ? a->sys.count - 1 : 0;
		a->screen = SCREEN_SYSTEMS;
	}
	cf_reset(&a->cf_sys, a->sys_cursor);
}

/* ChaOS: Show Muse flipped: the card on or off the row, at once */
static void muse_shelf_set(app *a)
{
	texload_bump();
	if (g_muse_shelf) muse_shelf_attach(a);
	else muse_shelf_detach(a);
	cf_reset(&a->cf_sys, a->sys_cursor);
}

/* ChaOS: the Ports shelf. Only with PortMaster installed - it is only
 * there if someone wants it (Get PortMaster) - and then PortMaster's own
 * card first, which opens the browser (pm_screen) */
static void pm_shelf_entry(game_list *l)
{
	game_entry *items;

	if (!ports_pm_ready(P_ROMS)) { lib_free(l); return; }
	items = realloc(l->items, (size_t)(l->count + 1) * sizeof *items);
	if (!items) return;
	memmove(items + 1, items, (size_t)l->count * sizeof *items);
	memset(&items[0], 0, sizeof items[0]);
	snprintf(items[0].name, sizeof items[0].name, "%s", PM_ENTRY_NAME);
	snprintf(items[0].title, sizeof items[0].title, "%s", PM_ENTRY_NAME);
	snprintf(items[0].file, sizeof items[0].file, "%s", PM_ENTRY_FILE);
	l->items = items;
	l->count++;
	l->scanned = true;
}

static void scan_all(app *a)
{
	for (int i = 0; i < a->sys.count; i++) {
		sysview *v = &a->view[i];
		lib_scan(P_ROMS, a->sys.systems[i].folder, a->sys.systems[i].exts, &v->list);
		if (!strcmp(a->sys.systems[i].core, PORTS_CORE)) pm_shelf_entry(&v->list);
		if (v->list.count > 0) {
			v->tex = calloc((size_t)v->list.count, sizeof *v->tex);
			v->tw = calloc((size_t)v->list.count, sizeof *v->tw);
			v->th = calloc((size_t)v->list.count, sizeof *v->th);
			v->cb = calloc((size_t)v->list.count, sizeof *v->cb);
			/* A count with no array behind it would be dereferenced on the
			 * next frame. An empty shelf is the honest answer. */
			if (!v->tex || !v->tw || !v->th || !v->cb) {
				free(v->tex); free(v->tw); free(v->th); free(v->cb);
				v->tex = NULL; v->tw = NULL; v->th = NULL; v->cb = NULL;
				lib_free(&v->list);
			}
		}
		fprintf(stderr, "scan: %-16s %d game%s\n", a->sys.systems[i].folder,
		        v->list.count, v->list.count == 1 ? "" : "s");
		/* What was left out, and what the folder takes, so a game that is
		 * on the card and not on the shelf has its reason in the log. */
		if (v->list.skipped) {
			char names[LIB_SKIPS_SHOWN * (LIB_NAME + 4)] = "";
			int k, shown = v->list.skipped < LIB_SKIPS_SHOWN ? v->list.skipped
			                                                 : LIB_SKIPS_SHOWN;

			for (k = 0; k < shown; k++) {
				size_t at = strlen(names);

				snprintf(names + at, sizeof names - at, "%s\"%s\"", k ? ", " : "",
				         v->list.skipped_eg[k]);
			}
			fprintf(stderr, "scan: %-16s left out %d (it takes %s): %s%s\n",
			        a->sys.systems[i].folder, v->list.skipped,
			        a->sys.systems[i].exts[0] ? a->sys.systems[i].exts : "anything",
			        names, v->list.skipped > shown ? ", and more" : "");
		}
	}
	hide_empty_systems(a);
	build_virtual_shelves(a);
	build_muse_shelf(a);
}

/* Read the card again and rebuild every shelf, for when something outside the
 * launcher has changed what is on it - which today means Over The Hare.
 *
 * Drop and rebuild rather than patch, on the same reasoning drop_favorites
 * gives: the cases are "a game appeared", "a game went", "a system that was
 * empty now has games", "a system emptied", and "the shelf you are standing on
 * is one of those". Patching five cases correctly is harder than doing the
 * whole thing again, and the whole thing is three directory reads.
 *
 * The config is re-read because hide_empty_systems does not hide, it COMPACTS:
 * a system with no games is removed from systems[] and its entry overwritten.
 * Uploading the first ROM for a system is precisely what this feature is for,
 * so without the reload that system could never come back. */
static void rescan_all(app *a)
{
	char tag[sizeof a->sys.systems[0].tag];
	char path[CFG_STR * 2];
	int i;

	/* Indices move when a system appears or empties, so the tag is the only
	 * handle on "where the player was" that survives the rebuild. */
	snprintf(tag, sizeof tag, "%s", a->sys.systems[a->sys_cursor].tag);

	/* First, and on its own: their lists and owner maps are this file's
	 * allocations rather than lib_scan's, and their textures are their own. */
	drop_virtual_shelf(a, VSHELF_FAV);
	drop_virtual_shelf(a, VSHELF_REC);

	free_all_textures(a);
	for (i = 0; i < a->sys.count; i++) {
		free(a->view[i].tex); free(a->view[i].tw); free(a->view[i].th);
		free(a->view[i].cb);
		free(a->view[i].album);                         /* Muse's, else NULL */
		lib_free(&a->view[i].list);
		memset(&a->view[i], 0, sizeof a->view[i]);
	}
	/* Every slot, not the first sys.count of them.
	 *
	 * hide_empty_systems moves systems[] and view[] and leaves sys_tex where
	 * it is - harmless at startup because they are all NULL then, and the
	 * comment in build_favorites_shelf already says so. Running the scan a
	 * second time is the moment that stops being true, and the failure is a
	 * system wearing the next one's card. Clearing the whole array puts it
	 * back to the state the omission is safe in. */
	memset(a->sys_tex, 0, sizeof a->sys_tex);
	memset(a->sys_w, 0, sizeof a->sys_w);
	memset(a->sys_h, 0, sizeof a->sys_h);

	/* menu_shelf_width measures every row of every system's menu once and
	 * caches it. The set of systems is exactly what it measured across, so a
	 * rescan invalidates it - and the case that shows is a card that booted
	 * empty: with no systems there are no game-menu rows to measure, the
	 * panel is sized for the TortOS menu alone, and the first system to
	 * arrive gets "Integer tall" cut off inside a frame measured before it
	 * existed. */
	a->menu_w = 0;

	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	if (!cfg_load_systems(path, &a->sys)) {
		/* The card was readable a moment ago, so this is a card that has gone
		 * away underneath us. An empty shelf is the honest picture. */
		fprintf(stderr, "rescan: no usable %s\n", path);
		a->sys.count = 0;
		a->sys_cursor = 0;
		a->screen = SCREEN_SYSTEMS;
		return;
	}

	scan_all(a);
	look_load(a);        /* indexes by tag, so it is safe to run again */
	sort_load(a);
	sort_all(a);

	a->sys_cursor = 0;
	for (i = 0; i < a->sys.count; i++)
		if (strcmp(a->sys.systems[i].tag, tag) == 0) { a->sys_cursor = i; break; }

	/* Whether the caller can stay where it was.
	 *
	 * Rescanning from inside a system's game list should leave you in that
	 * list looking at the new games, so the screen is not forced here. But the
	 * shelf being stood on can have emptied and gone - deleting the last ROM
	 * for a system over the network is the ordinary way - and then there is no
	 * list to stay in. Same rule refresh_virtual_shelves follows when the last
	 * favorite goes. */
	if (i >= a->sys.count || !a->sys.count) a->screen = SCREEN_SYSTEMS;

	if (a->sys.count) {
		a->tint = a->sys.systems[a->sys_cursor].accent;
		/* The rebuilt view starts at zero and its coverflow has to agree, or
		 * the shelf animates from wherever the old one happened to be. */
		a->view[a->sys_cursor].cursor = 0;
		cf_reset(&a->view[a->sys_cursor].cf, 0);
	}
	cf_reset(&a->cf_sys, a->sys_cursor);
}

/* --shot <file.png> [--screen games|systems] draws one frame, writes it out
 * and exits. This is how the shelf gets looked at without a device in hand:
 * the host build renders exactly what the handheld renders. */
static const char *shot_path;
static int shot_screen = -1;
static int shot_menu, shot_menu_sel;
static int shot_gmenu = -1;           /* ChaOS: --gmenu ROW, the in-game menu */
/* ChaOS: --pm home ROW | list SECTION ROW | detail ZIP: PortMaster's browser,
 * from the catalog in .userdata/ports */
static int shot_port_power = -1;   /* ChaOS: --port-power ROW, power during a port */
/* ChaOS: --romm home ROW | games PLATFORM ROW, against .userdata/romm's server */
static const char *shot_romm;
static int shot_romm_a, shot_romm_b;
static const char *shot_pm;
static const char *shot_pm_arg;
static int shot_pm_n;
static bool shot_clock;                    /* ChaOS: --datetime [row] */
static const char *shot_pick;              /* ChaOS: --pick IMAGE, the cover picker */
static const char *shot_pick_note;         /* ChaOS: --pick-note TEXT, its message alone */
/* --shader [group [row]]: the shader's page, -1, or a group's (ChaOS). */
static int shot_shader = -2, shot_shader_sel;
static const char *shot_shots;      /* --shots DIR [asking]: the viewer */
static int shot_shots_ask;
static bool shot_ss;
static int shot_kb, shot_kb_layer;
static const char *shot_notice;
static const char *shot_notice_head = "Unlocked  -  5 points";
static int shot_cheevos;
static int shot_cheevos_sel;
/* How far between two systems the shelf is caught, 0 at rest. The shot is one
 * settled frame, so anything that only happens DURING a move - the label
 * crossfade, a card mid-slide - cannot be looked at without this. */
static float shot_sysmove;
/* The same for the games shelf, which is the one with a long list under it.
 * Eleven systems make a rail whose segments tile the track; three hundred games
 * make an 18px segment moving two and a half pixels a card, and the two do not
 * look alike at any fraction of a move. */
static float shot_gamemove;
static const char *shot_cheevos_game = "Hagane: The Final Conflict";
static int shot_syn;       /* --synopsis: the card a scraped game opens */
static int shot_info;
/* --controls [page] draws MENU > Controls, page 0-3, over whichever screen
 * --screen asked for: the page follows UI Direction, so this is the only way
 * to look at all three without setting the device three times. */
static int shot_controls = -1;
static int shot_updates = -1;   /* ChaOS: --updates <upd_state> */
static int shot_upd_pill = -1;  /* ChaOS: --update-pill <upd_state>, the shelf's notice */
static const char *shot_remap;  /* ChaOS: --remap <TAG>: its Button Mapping */
static int shot_cc_page = -1;  /* ChaOS: --cc <page>, with --remap: its Controls */
static int shot_art;
static const char *shot_art_now = "Legend of Zelda, The - A Link to the Past (USA)";
static int shot_hare;
static const char *shot_hare_addr = "192.168.1.42";
static const char *shot_hare_who  = "1 connected";
static const char *shot_hare_head = "receiving Contra (USA).zip";
static const char *shot_wait, *shot_wait_msg = "Scanning...";
static const char *shot_kb_text = "correct horse";
/* The title was hardcoded to "Wi-Fi password", so the one other thing that
 * uses this keyboard - signing in to RetroAchievements - could not be
 * rendered at all. A harness narrower than the thing it checks. */
static const char *shot_kb_title = "Wi-Fi password";
static int shot_slots, shot_slot_sel;
static int shot_jump;               /* letter-jumps to apply before drawing */
static int shot_sys = -1;           /* ChaOS: --sys N */
/* --nowplaying ALBUM [TRACK [SECONDS]], and --paused with it. */
static int   shot_np = -1, shot_np_track, shot_np_paused;
static float shot_np_at = 83.0f;
/* --mode NAME plays whatever the shot shows in that play mode: the mark beside
 * the track count is drawn from it. */
static const char *shot_mode;
static float shot_slot_aspect = 4.0f / 3.0f;

/* A stand-in for a paused game frame, at whatever shape was asked for: the
 * slot carousel frames the picture to its own aspect, so looking at that
 * without a device means being able to hand it one. */
static SDL_Texture *fake_frame(SDL_Renderer *r, float aspect)
{
	int h = 224, w = (int)(224 * aspect + 0.5f);
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
	                                                SDL_PIXELFORMAT_ARGB8888);
	SDL_Texture *t;
	int x, y;

	if (!s) return NULL;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			Uint32 *px = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);
			int checker = ((x / 16) + (y / 16)) & 1;
			px[x] = SDL_MapRGBA(s->format,
			                    (Uint8)(30 + 180 * x / w),
			                    (Uint8)(40 + 150 * y / h),
			                    (Uint8)(checker ? 150 : 90), 255);
		}
	t = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	return t;
}

static void shot_draw_slots(app *a)
{
	slot_view sv = { .aspect = shot_slot_aspect, .saving = 0 };
	int i;

	for (i = 0; i <= GM_SLOTS; i++) {
		/* Slot 5 left empty, so the empty state is in the picture too. */
		if (i == 5) continue;
		sv.have[i] = 1;
		snprintf(sv.when[i], sizeof sv.when[i], "Aug %d %d:%02d:%02d PM",
		         20 + i, 1 + i, i * 7, i * 9);
		sv.thumb[i] = fake_frame(a->r, shot_slot_aspect);
	}
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 185);
	SDL_RenderFillRect(a->r, NULL);
	slot_draw(a, &sv, shot_slot_sel);
	for (i = 0; i <= GM_SLOTS; i++)
		if (sv.thumb[i]) SDL_DestroyTexture(sv.thumb[i]);
}

/* Now Playing without a daemon, for --shot. A cover not yet on the card is
 * drawn as missing rather than asked for: there is nobody to ask. */
static void np_shot(app *a)
{
	mu_now mn = { 0 };
	const ml_album *al;
	char next[128] = "";
	int ar, t;

	if (shot_np < 0 || shot_np >= g_muse.nalbums) return;
	al = &g_muse.albums[shot_np];
	t = shot_np_track < al->n ? shot_np_track : al->n - 1;
	for (ar = 0; ar < g_muse.nartists - 1; ar++)
		if (shot_np < g_muse.artists[ar].first + g_muse.artists[ar].n) break;
	mn.state = shot_np_paused ? MU_PAUSED : MU_PLAYING;
	snprintf(mn.title, sizeof mn.title, "%s", g_muse.tracks[al->first + t].name);
	snprintf(mn.artist, sizeof mn.artist, "%s", g_muse.artists[ar].name);
	snprintf(mn.album, sizeof mn.album, "%s", al->name);
	mn.at = shot_np_at;
	mn.len = 236.0;
	mn.index = t;
	mn.count = al->n;
	if (t + 1 < al->n)
		snprintf(next, sizeof next, "%s", g_muse.tracks[al->first + t + 1].name);

	{
		char base[LIB_PATH * 2], path[LIB_PATH * 2 + 8];

		ml_cover_base(g_muse_root, &g_muse, shot_np, base, sizeof base);
		snprintf(path, sizeof path, "%s.jpg", base);
		if (!file_nonempty(path)) {
			snprintf(path, sizeof path, "%s.png", base);
			if (!file_nonempty(path) && g_cov)
				g_cov[shot_np].st = ml_folder_image(g_muse_root, &g_muse, shot_np,
				                                    path, sizeof path)
				                  ? COV_FOLDER : COV_NONE;
		}
	}
	np_cover(a, shot_np);
	np_draw(a, &mn, next);
}

/* For a shot: every cover not on the card is a cover the music does not
 * carry, since there is no daemon to ask - so those albums draw their
 * generated card rather than an empty slot. */
static void muse_covers_settle(void)
{
	char base[LIB_PATH * 2], p[LIB_PATH * 2 + 8];
	int i;

	for (i = 0; g_cov && i < g_muse.nalbums; i++) {
		ml_cover_base(g_muse_root, &g_muse, i, base, sizeof base);
		snprintf(p, sizeof p, "%s.jpg", base);
		if (file_nonempty(p)) continue;
		snprintf(p, sizeof p, "%s.png", base);
		if (!file_nonempty(p))
			g_cov[i].st = ml_folder_image(g_muse_root, &g_muse, i, p, sizeof p)
			            ? COV_FOLDER : COV_NONE;
	}
}

/* --updater-art DIR: what the updater shows while it writes a release
 * (tools/tortos-update/screen.c), drawn by this launcher's own panel code so
 * the two match, since the updater has no font library of its own. The
 * three screens on the plain background without the parts that change, the
 * glyphs for those parts (the version in the heading, the percentage, the
 * bar's fill), and where they go. tools/genupdater-art.py makes the
 * updater's art.h from them; its comment says how to run both. */
static const char *shot_updater_art;

static void art_save(const char *dir, const char *name, const void *data, size_t n)
{
	char path[1024];
	FILE *f;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	if (!(f = fopen(path, "wb")) || fwrite(data, 1, n, f) != n) {
		fprintf(stderr, "updater art: can't write %s\n", path);
		exit(1);
	}
	fclose(f);
}

static void art_frame(app *a, const char *dir, const char *name, const char *heading,
                      const menu_row *rows, int n)
{
	size_t bytes = (size_t)TORTOS_SCREEN_W * TORTOS_SCREEN_H * 4;
	void *px = malloc(bytes);

	SDL_SetRenderTarget(a->r, NULL);
	draw_background(a);
	menu_bar_fill(0.0f);
	menu_draw(a, heading, rows, n, -1, menu_std_width(a), MENU_ACCENT);
	SDL_RenderReadPixels(a->r, NULL, SDL_PIXELFORMAT_RGBA32, px, TORTOS_SCREEN_W * 4);
	plat_present();
	art_save(dir, name, px, bytes);
	free(px);
}

/* One piece drawn on nothing, as it would be on the panel: its color times
 * its coverage, and the coverage, which is what the updater blends with. */
static void art_piece(app *a, FILE *index, FILE *bin, const char *key, int w, int h,
                      void (*draw)(app *, const void *), const void *what)
{
	SDL_Texture *t = SDL_CreateTexture(a->r, SDL_PIXELFORMAT_RGBA32,
	                                   SDL_TEXTUREACCESS_TARGET, w, h);
	void *px = malloc((size_t)w * h * 4);

	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_SetRenderTarget(a->r, t);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(a->r, 0, 0, 0, 0);
	SDL_RenderClear(a->r);
	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	draw(a, what);
	SDL_RenderReadPixels(a->r, NULL, SDL_PIXELFORMAT_RGBA32, px, w * 4);
	SDL_SetRenderTarget(a->r, NULL);
	SDL_DestroyTexture(t);
	fprintf(index, "%s %d %d %ld\n", key, w, h, ftell(bin));
	fwrite(px, 1, (size_t)w * h * 4, bin);
	free(px);
}

typedef struct { TTF_Font *f; const char *s; SDL_Color col; } art_text;
static void art_draw_text(app *a, const void *what)
{
	const art_text *t = what;

	ui_text(a->r, t->f, t->s, 0, 0, -1, t->col);
}
static void art_draw_bar(app *a, const void *what)
{
	const SDL_Rect *r = what;
	SDL_Rect at = { 0, 0, r->w, r->h };

	ui_round_rect(a->r, &at, r->h / 2, (SDL_Color){ (Uint8)(MENU_ACCENT >> 16),
	              (Uint8)(MENU_ACCENT >> 8), (Uint8)MENU_ACCENT, 255 });
}

static void updater_art(app *a, const char *dir)
{
	TTF_Font *fh = ui_font(UI_F_LABEL), *fm = ui_font(UI_F_MENU);
	const menu_row writing[] = {
		{ "Writing", "", false }, MENU_BAR, MENU_RULE, MENU_NOTE("Don't turn the device off"),
	};
	const menu_row checking[] = {
		{ "Checking", "", false }, MENU_BAR, MENU_RULE, MENU_NOTE("Don't turn the device off"),
	};
	const menu_row failed[] = {
		MENU_NOTE("It stopped while writing."),
		MENU_NOTE("Use ChaOS Installer;"),
		MENU_NOTE("games and saves are safe."),
		MENU_RULE,
		MENU_NOTE("Power button: turn off"),
	};
	char path[1024], one[2] = "";
	FILE *index, *bin, *layout;
	SDL_Rect bar;
	const char *c;

	g_menu_bare = true;
	/* A space for a heading: the band stays, and the version goes in it */
	art_frame(a, dir, "writing.rgba", " ", writing, 4);
	art_frame(a, dir, "checking.rgba", " ", checking, 4);
	bar = (SDL_Rect){ g_menu_seen.bar_x, g_menu_seen.bar_y, g_menu_seen.bar_w, g_menu_seen.bar_h };
	snprintf(path, sizeof path, "%s/layout.txt", dir);
	if (!(layout = fopen(path, "w"))) exit(1);
	fprintf(layout, "screen %d %d\nhead %d %d\nvalue %d %d\nbar %d %d %d %d\n",
	        TORTOS_SCREEN_W, TORTOS_SCREEN_H, g_menu_seen.head_x, g_menu_seen.head_y,
	        g_menu_seen.value_x, g_menu_seen.value_y, bar.x, bar.y, bar.w, bar.h);
	fclose(layout);
	art_frame(a, dir, "failed.rgba", "The update didn't finish", failed, 5);

	snprintf(path, sizeof path, "%s/pieces.txt", dir);
	index = fopen(path, "w");
	snprintf(path, sizeof path, "%s/pieces.bin", dir);
	bin = fopen(path, "wb");
	if (!index || !bin) exit(1);
	{
		art_text t = { fh, "Update to ", UI_TEXT_SOFT };

		art_piece(a, index, bin, "head-prefix", ui_text_width(fh, t.s), TTF_FontHeight(fh),
		          art_draw_text, &t);
	}
	for (c = "0123456789."; *c; c++) {
		char key[16];
		art_text t = { fh, one, UI_TEXT_SOFT };

		one[0] = *c;
		snprintf(key, sizeof key, "head-%c", *c);
		art_piece(a, index, bin, key, ui_text_width(fh, one), TTF_FontHeight(fh),
		          art_draw_text, &t);
	}
	for (c = "0123456789%"; *c; c++) {
		char key[16];
		art_text t = { fm, one, UI_TEXT_DIM };

		one[0] = *c;
		snprintf(key, sizeof key, "value-%c", *c);
		art_piece(a, index, bin, key, ui_text_width(fm, one), TTF_FontHeight(fm),
		          art_draw_text, &t);
	}
	art_piece(a, index, bin, "bar-fill", bar.w, bar.h, art_draw_bar, &bar);
	fclose(index);
	fclose(bin);
	fprintf(stderr, "updater art: wrote %s\n", dir);
}

/* One frame of RomM's screens for --romm */
static void romm_shot(app *a)
{
	g_pm_shot = true;
	romm_get();
	if (!strcmp(shot_romm, "home")) {
		romm_home_ctx c;
		menu_row rows[16];
		const char *heading;
		int n;

		memset(&c, 0, sizeof c);
		romm_home_refresh(&c);
		n = romm_home_build(&c, rows, 16, &heading);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw(a, heading, rows, n, shot_romm_a, menu_std_width(a), MENU_ACCENT);
	} else if (!strcmp(shot_romm, "games")) {
		romm_list l;
		romm_pic pp;
		romm_rom g[64];
		char why[160], have[64][1];
		int total, i, vis = (PM_PANEL.h - 100 - 24 - ui_font_line(UI_F_META) - 16) / menu_row_h();

		memset(&l, 0, sizeof l);
		memset(have, 0, sizeof have);
		l.n = romm_roms(&g_romm, shot_romm_a, 0, 64, g, 64, &total, why, sizeof why);
		if (l.n < 0) { fprintf(stderr, "--romm games: %s\n", why); return; }
		l.g = g;
		l.have = have;
		if (l.n > 1) have[1][0] = 1;
		memset(&pp, 0, sizeof pp);
		pp.want = pp.shown = -1;
		if (l.n) { pp.want = shot_romm_b < l.n ? shot_romm_b : 0; pp.failed = true; }
		for (i = 0; i < 1; i++) ;
		romm_list_draw(a, "Game Boy", &l, pp.want < 0 ? 0 : pp.want, 0, vis, &pp);
	}
}

/* One frame of PortMaster's browser for --pm */
static void pm_shot(app *a)
{
	unsigned accent = 0xA0AEC0;
	int k;

	for (k = 0; k < a->sys.count; k++)
		if (!strcmp(a->sys.systems[k].core, PORTS_CORE)) accent = a->sys.systems[k].accent;
	g_pm_shot = true;
	if (!pm_reload()) { fprintf(stderr, "--pm: no catalog in .userdata/ports\n"); return; }
	g_pm_rtr_only = false;
	if (!strcmp(shot_pm, "home")) {
		pm_home_ctx c;
		menu_row rows[16];
		const char *heading;
		int n;

		memset(&c, 0, sizeof c);
		pm_home_counts(&c);
		n = pm_home_build(&c, rows, 16, &heading);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw(a, heading, rows, n, shot_pm_arg ? atoi(shot_pm_arg) : 0, menu_std_width(a), accent);
	} else if (!strcmp(shot_pm, "list")) {
		static const pm_section sec[] = { PMS_MAY, PMS_ALL, PMS_INSTALLED, PMS_UPDATES };
		static const char *const heads[] = { "May Run on ChaOS", "All Ports", "Installed", "Updates" };
		int which = shot_pm_arg ? atoi(shot_pm_arg) : 0, *idx = NULL, n, sel = shot_pm_n, first;
		int vis = (PM_PANEL.h - 100 - 24 - ui_font_line(UI_F_META) - 16) / menu_row_h();
		pm_pic pp;

		if (which < 0 || which > 3) which = 0;
		n = pm_list_build(sec[which], NULL, &idx);
		if (sel >= n) sel = n ? n - 1 : 0;
		first = sel >= vis ? sel - vis + 1 : 0;
		pm_pic_init(&pp);
		if (n) { pm_pic_want(&pp, idx[sel]); pm_pic_step(a, &pp); }
		/* --pm list SECTION ROW letter: with the scrub's letter up */
		if (n && getenv("CHAOS_SHOT_LETTER")) letter_show(shelf_initial(g_pmc.p[idx[sel]].title));
		pm_list_draw(a, heads[which], idx, n, sel, first, vis, &pp, accent);
		pm_pic_free(&pp);
		free(idx);
	} else if (!strcmp(shot_pm, "detail") && shot_pm_arg) {
		int at = pmcat_find(&g_pmc, shot_pm_arg), nd;
		static char desc[80][CHV_WRAP_COLS];
		char text[8192];
		pm_pic pp;
		const pm_port *p;

		if (at < 0) { fprintf(stderr, "--pm detail: no %s\n", shot_pm_arg); return; }
		p = &g_pmc.p[at];
		pmcat_text(p, "desc", text, sizeof text);
		nd = wrap_text(ui_font(UI_F_MENU), text, PM_PANEL.w - 72, desc, 80);
		pm_pic_init(&pp);
		pm_pic_want(&pp, at);
		pm_pic_step(a, &pp);
		pm_detail_draw(a, at, &pp, (const char (*)[CHV_WRAP_COLS])desc, nd, 0, accent,
		               p->flags & PMF_UPDATE ? "A: update   X: remove   Y: read all   B: back"
		               : p->flags & PMF_INSTALLED ? "X: remove   Y: read all   B: back"
		               : "A: install   Y: read all   B: back");
		pm_pic_free(&pp);
	}
}

static void take_shot(app *a)
{
	SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, TORTOS_SCREEN_W,
	                                                  TORTOS_SCREEN_H, 32,
	                                                  SDL_PIXELFORMAT_RGBA32);
	muse_covers_settle();
	draw_shelf(a);
	/* An empty card's welcome, as render draws it, on its own: the device
	 * shows it only on the shelf, never under another screen */
	if (a->sys.count <= 0 && !shot_menu && !shot_slots && !shot_kb && !shot_wait &&
	    !shot_info && shot_controls < 0 && shot_np < 0 && !shot_art && !shot_hare &&
	    !shot_syn && !shot_cheevos)
		welcome_draw(a);
	if (shot_menu) tortos_menu_draw(a, shot_menu_sel);
	if (shot_pm) pm_shot(a);
	if (shot_port_power >= 0) port_power_draw(a, shot_port_power);
	if (shot_romm) romm_shot(a);
	if (shot_gmenu >= 0) {
		static menu_row rows[GM_SHOWN + 4];
		gm_bufs gb;
		int gn;

		memset(&gb, 0, sizeof gb);
		gn = gm_build(a, rows, &gb, 2, NULL);
		menu_draw(a, NULL, rows, gn, shot_gmenu < gn ? shot_gmenu : 0, 0, MENU_ACCENT);
	}
	if (shot_clock) {
		clock_ui cu;
		menu_row rows[MENU_MAX_ROWS];
		const char *head;
		int n;

		memset(&cu, 0, sizeof cu);
		n = clock_build(&cu, rows, MENU_MAX_ROWS, &head);
		menu_draw(a, head, rows, n, shot_menu_sel, menu_std_width(a), MENU_ACCENT);
	}
	if (shot_pick_note) {
		char note[192];

		snprintf(note, sizeof note, "%s", shot_pick_note);
		for (char *q = note; *q; q++) if (*q == '|') *q = '\n';
		pick_draw(a, "Super Tilt Bro", NULL, 0, 0, NULL, 0, 0, note, MENU_ACCENT);
	}
	if (shot_upd_pill >= 0) {
		upd_demo((upd_state)shot_upd_pill, "1.2.0", NULL);
		draw_upd_notice(a);
	}
	if (shot_pick) {
		pick_item it[3];
		SDL_Surface *sf = IMG_Load(shot_pick);
		SDL_Texture *t = sf ? SDL_CreateTextureFromSurface(a->r, sf) : NULL;

		memset(it, 0, sizeof it);
		snprintf(it[0].label, sizeof it[0].label, "itch.io: Super Tilt Bro. for NES");
		snprintf(it[1].label, sizeof it[1].label, "itch.io: Super Tilt Bro. (Demo)");
		it[0].state = 2;
		pick_draw(a, "Super Tilt Bro", it, 2, 0, t, sf ? sf->w : 0, sf ? sf->h : 0,
		          NULL, MENU_ACCENT);
		if (t) SDL_DestroyTexture(t);
		if (sf) SDL_FreeSurface(sf);
	}
	if (shot_shader > -2) shader_shot(a, shot_shader, shot_shader_sel);
	if (shot_ss) ss_shot(a);
	if (shot_shots) {
		static char names[SHOTS_MAX][SHOTS_NAME];
		int n = shots_list(shot_shots, names, SHOTS_MAX), tw = 0, th = 0;
		char p[LIB_PATH * 2 + SHOTS_NAME];
		SDL_Surface *sf;
		SDL_Texture *t = NULL;

		if (n > 1) {
			snprintf(p, sizeof p, "%s/%s", shot_shots, names[1]);
			if ((sf = IMG_Load(p))) { t = SDL_CreateTextureFromSurface(a->r, sf); SDL_FreeSurface(sf); }
			if (t) SDL_QueryTexture(t, NULL, NULL, &tw, &th);
			shots_draw(a, t, tw, th, names[1], 1, n, shot_shots_ask ? 1 : 0, 1.0f);
			if (t) SDL_DestroyTexture(t);
		}
	}
	if (shot_slots) shot_draw_slots(a);
	if (shot_kb) kb_preview(a->r, shot_kb_title, shot_kb_text,
	                        shot_kb_layer, 1, 0, MENU_ACCENT);
	/* The in-game notice is not drawn by this process - Diatom composites it
	 * over the game - so it cannot be screenshotted like the rest. Rendered
	 * to its wire format and read straight back, which is the same pixels
	 * Diatom will show and therefore the thing worth looking at. */
	/* The achievements list, with sample data. It is only reachable from the
	 * in-game menu, so without this it can only be judged on a device with a
	 * game running - and it took two rounds of that to notice its heading was
	 * clipped at both ends. */
	/* The one-line panel behind "Scanning...", "Connecting...", "Signing
	 * in..." and "Looking up this game...". Five flows use it and none of them
	 * could be rendered. */
	if (shot_wait) {
		menu_row row = { shot_wait_msg, NULL, false };

		menu_draw(a, shot_wait, &row, 1, -1, 0, MENU_ACCENT);
	}
	if (shot_info) info_preview(a, true);
	if (shot_controls >= 0) {
		menu_row rows[CTL_MAX_ROWS];
		char head[80];
		ctl_page p = (ctl_page)(shot_controls % CTL_PAGES);
		int n;

		ctl_set_fn_volume(plat_brightness_on_volume());
		ctl_set_battery_lights(plat_battery_lights());
		n = ctl_rows(p, controls_dir(), rows);

		snprintf(head, sizeof head, "Main Menu Controls");
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw(a, head, rows, n, -1, menu_std_width(a), MENU_ACCENT);
	}
	if (shot_remap) {
		rm_ctx c;
		menu_row rows[24];
		const char *head;
		int n, i;

		memset(&c, 0, sizeof c);
		c.a = a;
		for (i = 0; i < a->sys.count; i++)
			if (!strcmp(a->sys.systems[i].tag, shot_remap)) c.sys = i;
		c.file = "game.bin";
		rm_load(&c);
		n = rm_build(&c, rows, 24, &head);
		if (shot_cc_page >= 0) {
			cc_ctx cc;
			memset(&cc, 0, sizeof cc);
			cc.a = a; cc.sys = c.sys; cc.file = "game.bin"; cc.page = shot_cc_page;
			n = cc_build(&cc, rows, 24, &head);
			SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
			SDL_RenderFillRect(a->r, NULL);
			menu_draw(a, head, rows, n, 0, menu_std_width(a), MENU_ACCENT);
			goto shot_done;
		}
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw(a, head, rows, n, 2, menu_shelf_width(a), MENU_ACCENT);
	}
shot_done:
	if (shot_updates >= 0) {
		upd_ctx c;
		menu_row rows[16];
		const char *head;
		int n;

		memset(&c, 0, sizeof c);
		c.a = a;
		c.next_look = ~0u;              /* as found, not asked */
		c.online = shot_updates < 100;
		c.wifi = c.online ? WIFI_CONNECTED : WIFI_OFF;
		upd_demo((upd_state)(shot_updates % 100), "1.1.0",
		         shot_updates % 100 == UPD_FAILED ? "Wi-Fi is off" : NULL);
		n = upd_build(&c, rows, 16, &head);
		SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
		SDL_RenderFillRect(a->r, NULL);
		menu_draw(a, head, rows, n, n - 1, 640, MENU_ACCENT);
		if (shot_updates % 100 == UPD_AVAILABLE) {
			snprintf(g_upd_msg, sizeof g_upd_msg, "ChaOS 1.1.0 is available: MENU > Updates");
			g_upd_msg_until = plat_now_ms() + 5000;
			draw_upd_notice(a);
		}
	}
	if (shot_np >= 0) np_shot(a);
	if (shot_art)
		art_preview(a, shot_art_now, "4 of 10 systems",
		            "37 found, 2 missing, 61 already", true);
	if (shot_hare)
		hare_preview(a, shot_hare_addr, "4071", shot_hare_who,
		             "12 KB / 41 MB", shot_hare_head);
	/* The synopsis card, from the card's own games table - so a shot of it is a
	 * picture of real scraped text rather than a fixture. */
	if (shot_syn) {
		sysview *v = &a->view[a->sys_cursor];
		const system_cfg *s = &a->sys.systems[shelf_owner(a, a->sys_cursor, v->cursor)];
		game_meta m;

		if (v->list.count > 0 &&
		    db_game_get(db_lib(), s->folder, v->list.items[v->cursor].file, &m) &&
		    m.synopsis[0]) {
			char lines[SYN_WRAP_LINES][CHV_WRAP_COLS];
			menu_row rows[SYN_ROWS];
			int fixed = menu_std_width(a);
			int loop_at = 0;
			int nl = syn_layout(m.synopsis, fixed, lines, rows, &loop_at);

			SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(a->r, 0, 0, 0, 120);
			SDL_RenderFillRect(a->r, NULL);
			menu_draw_ex(a, v->list.items[v->cursor].title, rows, nl, -1, fixed,
			             s->accent, NULL, false, loop_at);
		}
	}
	if (shot_cheevos) {
		/* Real titles, and the long ones are the point: a set's names run past
		 * the panel often, and this shot drew eight short ones - so the screen
		 * could be looked at without a device and still not show the thing
		 * worth looking at. Two of the three below are real RetroAchievements
		 * titles from sets on this card. */
		static const struct { const char *t; int p; bool got; } sample[] = {
			{ "The Path to Disaster", 5, true },
			{ "Hold On To Your Potatoes, We're In For A Bumpy Ride", 10, true },
			{ "Violated Heavens", 10, true },
			{ "I Am Not Left Handed Either, But I Can Still Beat You", 25, false },
			{ "Koma Faction's Fall", 25, false },
			{ "Not So Disaster", 10, false },
			{ "Storming The Fortress", 10, false },
			{ "Blazing through the Skies", 10, false },
		};
		int cn = (int)(sizeof sample / sizeof sample[0]), ci;
		menu_row rows[8];
		unsigned vcols[8] = { 0 };
		char vals[8][16], head[192];

		for (ci = 0; ci < cn; ci++) {
			snprintf(vals[ci], sizeof vals[ci], "%d", sample[ci].p);
			rows[ci] = (menu_row){ sample[ci].t, vals[ci], sample[ci].got };
			vcols[ci] = sample[ci].got ? UI_EARNED_RGB : 0u;
		}
		snprintf(head, sizeof head, "%s\n%d/36 cheevos   %d/415 points",
		         shot_cheevos_game, 3, 25);
		/* Drawn the way cheevos_screen draws it - menu_draw_ex, the earned
		 * colors, the shared width - because a shot that composes the panel
		 * differently from the screen is a picture of something that does not
		 * exist. It used to call menu_draw with a width of 0. */
		menu_draw_ex(a, head, rows, cn, shot_cheevos_sel, menu_std_width(a),
		             a->tint, vcols, true, 0);
	}
	if (shot_notice) {
		char dt[512];
		FILE *nf;

		snprintf(dt, sizeof dt, "%s.dtov", shot_path);
		if (notice_render(shot_notice_head, shot_notice, dt) &&
		    (nf = fopen(dt, "rb"))) {
			unsigned char hd[8];
			if (fread(hd, 1, 8, nf) == 8 && !memcmp(hd, "DTOV", 4)) {
				int nw = hd[4] | (hd[5] << 8), nh = hd[6] | (hd[7] << 8);
				SDL_Surface *ns = SDL_CreateRGBSurfaceWithFormat(
					0, nw, nh, 32, SDL_PIXELFORMAT_ARGB8888);
				if (ns && fread(ns->pixels, 4, (size_t)nw * nh, nf)
				          == (size_t)nw * nh) {
					SDL_Rect at = { (TORTOS_SCREEN_W - nw) / 2,
					                TORTOS_SCREEN_H - nh - TORTOS_SCREEN_H / 24,
					                nw, nh };
					SDL_Texture *nt = SDL_CreateTextureFromSurface(a->r, ns);
					if (nt) {
						SDL_SetTextureBlendMode(nt, SDL_BLENDMODE_BLEND);
						SDL_RenderCopy(a->r, nt, NULL, &at);
						SDL_DestroyTexture(nt);
					}
				}
				if (ns) SDL_FreeSurface(ns);
			}
			fclose(nf);
			remove(dt);
		}
	}
	if (out) {
		/* Read BEFORE presenting: the backbuffer is invalid afterwards. */
		SDL_RenderReadPixels(a->r, NULL, SDL_PIXELFORMAT_RGBA32,
		                     out->pixels, out->pitch);
		plat_present();
		IMG_SavePNG(out, shot_path);
		SDL_FreeSurface(out);
		/* Say what was drawn, not just that something was: a tool whose whole
		 * job is rendering one state is far more useful when the state it
		 * chose is in the output beside the filename. */
		{
			sysview *v = &a->view[a->sys_cursor];
			const char *what = a->screen == SCREEN_GAMES && v->list.count > 0
			                 ? v->list.items[v->cursor].title
			                 : a->sys.systems[a->sys_cursor].name;
			/* Checked rather than assumed. This line once named a focused
			 * game on a shot with no cards in it. A card drawn on the frame
			 * always ends up with a texture - box art, a preview, or the
			 * generated card - so one without means the picture is not what
			 * the name beside it claims, and that is worth saying loudly. */
			const char *note =
				a->screen == SCREEN_GAMES && v->list.count > 0 &&
				v->tex && !v->tex[v->cursor]
				? "  (NO ART: the focused card had no texture on this frame)" : "";
			fprintf(stderr, "wrote %s  [%s] %s%s\n", shot_path,
			        a->screen == SCREEN_GAMES ? "games" : "systems", what, note);
		}
	}
}

int main(int argc, char *argv[])
{
	app a = { 0 };
	char path[CFG_STR * 2];
	char startup[CFG_STR];
	bool loader;                  /* texload's threads are running */

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot_path = argv[++i];
		else if (!strcmp(argv[i], "--updater-art") && i + 1 < argc) shot_updater_art = argv[++i];
		else if (!strcmp(argv[i], "--screen") && i + 1 < argc)
			shot_screen = strcmp(argv[++i], "games") == 0 ? SCREEN_GAMES
			                                              : SCREEN_SYSTEMS;
		/* --menu [row] draws the TortOS menu over whichever screen --screen
		 * asked for, so the panel can be looked at without a device. */
		else if (!strcmp(argv[i], "--gmenu") && i + 1 < argc) shot_gmenu = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--port-power") && i + 1 < argc) shot_port_power = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--romm") && i + 1 < argc) {
			shot_romm = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') shot_romm_a = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') shot_romm_b = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--pm") && i + 1 < argc) {
			shot_pm = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_pm_arg = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_pm_n = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--menu")) {
			shot_menu = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_menu_sel = atoi(argv[++i]);
		}
		/* --slots <sel> [aspect] draws one frame of the save/load carousel
		 * over synthetic frames, which is the only way to look at it without
		 * a game running on a device. */
		/* --jump N applies N letter-jumps before the shot (negative for up),
		 * so the d-pad's behavior on a real library can be checked without a
		 * device or a hand on it. */
		/* ChaOS: --appearance [row], the Appearance page as --menu draws the
		 * main menu */
		else if (!strcmp(argv[i], "--appearance")) {
			shot_menu = 1;
			g_appearance_page = true;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_menu_sel = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--pick") && i + 1 < argc) shot_pick = argv[++i];
		else if (!strcmp(argv[i], "--pick-note") && i + 1 < argc) shot_pick_note = argv[++i];
		/* ChaOS: --datetime [row], the Date & Time screen */
		else if (!strcmp(argv[i], "--datetime")) {
			shot_clock = true;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_menu_sel = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--sys") && i + 1 < argc) shot_sys = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--jump") && i + 1 < argc) {
			shot_jump = atoi(argv[++i]);
		}
		/* --keyboard [layer] [text] draws one frame of the text-entry panel,
		 * for the same reason --menu and --slots exist: it is dense, and
		 * laying it out against a screenshot beats a round trip to a device. */
		else if (!strcmp(argv[i], "--cheevos-screen")) {
			shot_cheevos = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_cheevos_sel = atoi(argv[++i]);
			/* A game name, so the heading can be looked at with one of the
			 * long ones rather than only the short one that fit. */
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_cheevos_game = argv[++i];
		}
		else if (!strcmp(argv[i], "--synopsis")) shot_syn = 1;
		/* --nowplaying ALBUM [TRACK [SECONDS]] draws Now Playing for an album
		 * of the card's Music folder, by its number in the scan, without a
		 * daemon: the cover has to be on the card already, or the album is
		 * drawn as one with none. --paused draws it paused. */
		else if (!strcmp(argv[i], "--nowplaying") && i + 1 < argc) {
			shot_np = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_np_track = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_np_at = (float)atof(argv[++i]);
		}
		else if (!strcmp(argv[i], "--paused")) shot_np_paused = 1;
		else if (!strcmp(argv[i], "--shader")) {
			shot_shader = -1;
			if (i + 1 < argc && (argv[i + 1][0] == '-' ? argv[i + 1][1] >= '0' && argv[i + 1][1] <= '9'
			                                          : argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9'))
				shot_shader = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_shader_sel = atoi(argv[++i]);
		}
		/* --speed SHADE BUSY FRAME MEASURED: the speed check's rows, as if
		 * Diatom had said so (BUSY -1 for not known) */
		else if (!strcmp(argv[i], "--speed") && i + 4 < argc) {
			plat_shade_speed sp;
			sp.shade_ms = atof(argv[++i]);
			sp.busy_ms  = atof(argv[++i]);
			sp.frame_ms = atof(argv[++i]);
			sp.measured = atoi(argv[++i]) != 0;
			plat_resident_demo_speed(&sp);
			shot_speed = true;
		}
		else if (!strcmp(argv[i], "--ss")) shot_ss = true;
		/* --letter C: the letter popup, as if L2 had just been pressed */
		else if (!strcmp(argv[i], "--letter") && i + 1 < argc) {
			g_lp_letter = argv[++i][0];
			g_lp_last = plat_now_ms() ? plat_now_ms() : 1;
			g_lp_since = g_lp_last - 500;
		}
		/* --pixnames T: the ChaOS pixel themes with the focused card at T (0..1),
		 * the one leaving at 1-T; -1 for both at their ends */
		else if (!strcmp(argv[i], "--pixnames") && i + 1 < argc) pn_fix((float)atof(argv[++i]));
		else if (!strcmp(argv[i], "--shots") && i + 1 < argc) {
			shot_shots = argv[++i];
			if (i + 1 < argc && !strcmp(argv[i + 1], "ask")) { shot_shots_ask = 1; i++; }
		}
		else if (!strcmp(argv[i], "--mode") && i + 1 < argc) shot_mode = argv[++i];
		else if (!strcmp(argv[i], "--sysmove") && i + 1 < argc)
			shot_sysmove = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--gamemove") && i + 1 < argc)
			shot_gamemove = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--info")) shot_info = 1;
		else if (!strcmp(argv[i], "--cc") && i + 1 < argc)
			shot_cc_page = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--remap") && i + 1 < argc)
			shot_remap = argv[++i];
		else if (!strcmp(argv[i], "--update-pill") && i + 1 < argc)
			shot_upd_pill = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--updates") && i + 1 < argc)
			shot_updates = atoi(argv[++i]);   /* an upd_state, +100 offline */
		else if (!strcmp(argv[i], "--controls")) {
			shot_controls = 0;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_controls = atoi(argv[++i]);
		}
		else if (!strcmp(argv[i], "--phase") && i + 1 < argc)
			shot_phase = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--art")) {
			shot_art = 1;
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_art_now = argv[++i];
		}
		else if (!strcmp(argv[i], "--hare")) {
			shot_hare = 1;
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hare_addr = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hare_who  = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_hare_head = argv[++i];
		}
		else if (!strcmp(argv[i], "--wait") && i + 1 < argc) {
			shot_wait = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_wait_msg = argv[++i];
		}
		else if (!strcmp(argv[i], "--notice") && i + 1 < argc) {
			shot_notice = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_notice_head = argv[++i];
		}
		else if (!strcmp(argv[i], "--keyboard")) {
			shot_kb = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '2')
				shot_kb_layer = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_kb_text = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-') shot_kb_title = argv[++i];
		}
		else if (!strcmp(argv[i], "--slots")) {
			shot_slots = 1;
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_slot_sel = atoi(argv[++i]);
			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shot_slot_aspect = (float)atof(argv[++i]);
		}
	}
	/* --wifi prints what the radio can see and exits. The wifi module talks
	 * to firmware that only exists on the device, so it cannot be exercised
	 * from the host at all, and a flag that reports its view is the cheapest
	 * way to tell "the machinery is wrong" from "the network is". */
	if (argc > 1 && !strcmp(argv[1], "--wifi")) {
		wifi_net nets[WIFI_MAX_NETS];
		char ssid[WIFI_SSID_MAX], ip[64];
		int n, i;
		wifi_state st;

		printf("bringing the supplicant up ...\n");
		printf("  wifi_up: %s\n", wifi_up() ? "ok" : "FAILED");
		st = wifi_status(ssid, sizeof ssid, ip, sizeof ip);
		printf("  status: %s  ssid=[%s] ip=[%s]\n",
		       st == WIFI_CONNECTED  ? "connected"  :
		       st == WIFI_CONNECTING ? "connecting" :
		       st == WIFI_IDLE       ? "idle"       : "off", ssid, ip);
		n = wifi_scan(nets, WIFI_MAX_NETS);
		printf("  scan: %d network(s)\n", n);
		for (i = 0; i < n; i++)
			printf("    %-32s %4d dBm  %-9s %s\n", nets[i].ssid,
			       nets[i].signal, nets[i].secured ? "secured" : "open",
			       nets[i].known ? "known" : "");
		return n < 0 ? 1 : 0;
	}

	signal(SIGTERM, on_sigterm);
	signal(SIGINT, on_sigterm);

	/* Nothing on the device can read a database - there is no sqlite3 binary -
	 * so losing the ability to SEE a setting would be a real loss where losing
	 * the ability to edit one is the point. Runs before video: it is a
	 * question asked over adb, not a screen. */
	/* Repair, not configuration. A wrong shipped default is permanent on a
	 * device that already seeded it - seeding fills absent keys only, and it
	 * cannot tell "the player chose this" from "we seeded it by mistake" - so
	 * there has to be a way to correct one in place. There is no sqlite3 on
	 * the device to do it with.
	 *
	 * It refuses a key that does not already exist, in either scope. That is
	 * what keeps it a repair tool: a typo cannot invent a setting that looks
	 * real, is written faithfully, and is read by nothing. */
	if (argc > 2 && !strcmp(argv[1], "--set")) {
		char dev[CFG_STR * 2], lib[CFG_STR * 2], env[CFG_STR * 2];
		char key[CFG_STR];
		const char *eq = strchr(argv[2], '=');
		db *target;

		if (!eq || eq == argv[2]) {
			fprintf(stderr, "usage: tortos.elf --set key=value\n");
			return 2;
		}
		snprintf(key, sizeof key, "%.*s", (int)(eq - argv[2]), argv[2]);
		paths_init();
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, env, sizeof env);
		if (!db_init(dev, lib, env)) {
			fprintf(stderr, "cannot open the settings database\n");
			return 1;
		}
		target = db_has(db_dev(), key) ? db_dev()
		       : db_has(db_lib(), key) ? db_lib() : NULL;
		if (!target) {
			fprintf(stderr, "no setting named '%s' - --dump lists them\n", key);
			db_shutdown();
			return 1;
		}
		if (!db_set_str(target, key, eq + 1)) {
			fprintf(stderr, "could not write '%s'\n", key);
			db_shutdown();
			return 1;
		}
		/* Two of these keys are exported for launch.sh, and a value the
		 * database holds but boot.env does not is a setting that takes effect
		 * everywhere except at boot. Setting wifi=1 and rebooting into a
		 * radio that stays down is how that was found. */
		db_write_boot_env();
		printf("%s = %s\n", key, eq + 1);
		db_shutdown();
		return 0;
	}

	/* --meta <file>: fill the card's games table from scraped rows.
	 *
	 * The scrape runs on a computer for now, and from there the card's
	 * database cannot simply be written: the launcher holds it open
	 * in WAL mode for as long as the device is on, and swapping the file under
	 * a live SQLite handle is how a card loses its favorites and its play time.
	 * Measured on this device: a 76 KB library.db beside a 161 KB -wal.
	 *
	 * So the rows travel as a file and the writing happens HERE, in a second
	 * process, where SQLite arbitrates between the two the way it is built to.
	 * Runs before video, like --set and --dump: it is a question asked over
	 * adb, not a screen, and nothing here presents.
	 *
	 * A record is a tab-separated header - folder, file, year, publisher,
	 * developer, players, genres, esrb, note, and the synopsis length in bytes
	 * - then that many bytes of synopsis and a newline. COUNTED RATHER THAN
	 * ESCAPED, because the synopsis is the one field with newlines in it and 42
	 * of the 78 replies this card's games got have them; an escape is a second
	 * thing to get right at both ends. */
	if (argc > 2 && !strcmp(argv[1], "--meta")) {
		char dev[CFG_STR * 2], lib[CFG_STR * 2], head[1024];
		FILE *f = fopen(argv[2], "rb");
		int wrote = 0, bad = 0;

		if (!f) {
			fprintf(stderr, "cannot read %s\n", argv[2]);
			return 1;
		}
		paths_init();
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, NULL, 0);
		if (!db_init(dev, lib, NULL)) {
			fprintf(stderr, "cannot open the library database\n");
			fclose(f);
			return 1;
		}
		while (fgets(head, sizeof head, f)) {
			game_meta m = { 0 };
			char *fld[10], *p = head;
			size_t want, take;
			long len;
			int i;

			for (i = 0; i < 9; i++) {
				char *t = strchr(p, '\t');

				if (!t) break;
				*t = '\0';
				fld[i] = p;
				p = t + 1;
			}
			if (i < 9) { bad++; continue; }
			fld[9] = p;
			/* A length that is not a length means the file is not what it says
			 * and every byte after it is at an unknown offset. Stop, rather
			 * than write whatever the rest happens to parse as.
			 *
			 * Checked digit by digit rather than left to strtol, which reads
			 * "nine" as 0 - a perfectly valid length, and the row lands with
			 * its synopsis quietly gone and the reader none the wiser. */
			for (p = fld[9]; *p >= '0' && *p <= '9'; p++)
				;
			if (p == fld[9] || (*p != '\n' && *p != '\r' && *p != '\0')) {
				bad++;
				break;
			}
			len = strtol(fld[9], NULL, 10);
			if (len < 0 || len > 1 << 20) { bad++; break; }
			want = (size_t)len;
			take = want < sizeof m.synopsis ? want : sizeof m.synopsis - 1;
			if (fread(m.synopsis, 1, take, f) != take) { bad++; break; }
			m.synopsis[take] = '\0';
			for (want -= take; want > 0; want--)
				if (fgetc(f) == EOF) break;
			fgetc(f);                            /* the record's newline */
			snprintf(m.year,      sizeof m.year,      "%s", fld[2]);
			snprintf(m.publisher, sizeof m.publisher, "%s", fld[3]);
			snprintf(m.developer, sizeof m.developer, "%s", fld[4]);
			snprintf(m.players,   sizeof m.players,   "%s", fld[5]);
			snprintf(m.genres,    sizeof m.genres,    "%s", fld[6]);
			snprintf(m.esrb,      sizeof m.esrb,      "%s", fld[7]);
			snprintf(m.note,      sizeof m.note,      "%s", fld[8]);
			if (db_game_set(db_lib(), fld[0], fld[1], &m)) wrote++;
			else bad++;
		}
		fclose(f);
		printf("%d written, %d rejected\n", wrote, bad);
		db_shutdown();
		return bad && !wrote ? 1 : 0;
	}

	/* --scrape [folder] runs the box art scraper with no video at all.
	 *
	 * The same art_begin/art_step the screen drives, stepped from here instead
	 * of from a frame loop, so a long run does not depend on somebody holding
	 * the device - and so that re-scraping just a few games is a five-minute
	 * job with a command rather than a menu to navigate.
	 *
	 * Headless, so it must NOT touch video: the live launcher is presenting
	 * while this runs, and two presenters wedge the display engine. It also
	 * takes the one async network slot, which is per process, so the launcher
	 * is unaffected either way.
	 *
	 * The shrinker runs inline here rather than on a worker - art_shrink falls
	 * back to doing it on the spot when no thread was started - which is what
	 * a run with nothing to draw wants anyway. */
	if (argc > 1 && !strcmp(argv[1], "--scrape")) {
		char dev[CFG_STR * 2], lib[CFG_STR * 2], cp[CFG_STR * 2], path[CFG_STR * 2];
		const char *only_sys = argc > 2 ? argv[2] : NULL;
		systems_cfg all, one;
		art_progress st;
		char seen[128] = "";
		int r;

		paths_init();
		snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
		if (!cfg_load_systems(path, &all)) {
			fprintf(stderr, "no usable %s\n", path);
			return 1;
		}
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, NULL, 0);
		if (!db_init(dev, lib, NULL)) {
			fprintf(stderr, "cannot open the databases\n");
			return 1;
		}
		snprintf(cp, sizeof cp, "%s/cacert.pem", P_ROOT);
		net_set_ca_path(cp);
		ss_creds_load();
		IMG_Init(IMG_INIT_PNG);

		if (only_sys) {
			int i;

			memset(&one, 0, sizeof one);
			for (i = 0; i < all.count; i++)
				if (!strcmp(all.systems[i].folder, only_sys)) {
					one.systems[0] = all.systems[i];
					one.count = 1;
					break;
				}
			if (!one.count) {
				fprintf(stderr, "no shelf called %s\n", only_sys);
				return 1;
			}
		}
		fprintf(stderr, "scrape: %s, ScreenScraper %s\n",
		        only_sys ? only_sys : "every shelf",
		        ss_signed_in() ? "signed in" : "not signed in");
		art_begin(only_sys ? &one : &all, P_ROMS, NULL);
		while ((r = art_step()) == 1) {
			art_status(&st);
			/* One line per thing it turns to, not per step: a step is mostly
			 * a poll of a request already in flight. Keyed on what it says it
			 * is doing rather than on the counters, because the interesting
			 * case - a game asked about and not found - moves no counter. */
			if (strcmp(st.now, seen)) {
				snprintf(seen, sizeof seen, "%s", st.now);
				fprintf(stderr, "scrape: %-44s  %d found, %d missing, %d had one\n",
				        st.now, st.found, st.missing, st.skipped);
			}
			usleep(20000);
		}
		art_status(&st);
		fprintf(stderr, "scrape: done - %d found, %d missing, %d already had one%s%s\n",
		        st.found, st.missing, st.skipped,
		        st.problem[0] ? " - " : "", st.problem);
		return 0;
	}

	if (argc > 1 && !strcmp(argv[1], "--dump")) {
		char dev[CFG_STR * 2], lib[CFG_STR * 2];
		paths_init();
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, NULL, 0);
		if (!db_init(dev, lib, NULL)) {
			fprintf(stderr, "cannot open the settings database\n");
			return 1;
		}
		printf("device  %s\n", dev);
		db_dump(db_dev(), stdout);
		printf("library %s\n", lib);
		db_dump(db_lib(), stdout);
		db_shutdown();
		return 0;
	}

	t_boot0 = plat_now_ms();
	paths_init();

	/* Before anything reads a setting. Two databases, because the files this
	 * replaced kept per-device and per-card data apart on purpose - see db.h.
	 * A failure here is fatal rather than silently defaulted: every setting
	 * the launcher has would be a fallback, and the player would find their
	 * volume, brightness and account quietly reset with nothing said. */
	{
		char dev[CFG_STR * 2], lib[CFG_STR * 2], env[CFG_STR * 2];
		db_paths_ready(dev, sizeof dev, lib, sizeof lib, env, sizeof env);
		/* NOT fatal, and the reason is what "fatal" costs here. launch.sh
		 * restarts the launcher when it exits, and five exits inside five
		 * seconds each makes it give up and call poweroff - so returning 1
		 * from this point turns a settings problem into a device that will
		 * not boot. It is also BEFORE plat_resident_ready() below, which is
		 * what stops a running game so that only one process is presenting;
		 * exiting here skips that entirely.
		 *
		 * Every getter takes what to say instead, and db_dev()/db_lib()
		 * return NULL harmlessly, so the launcher comes up on the shipped
		 * defaults with no favorites, no turbo and no core options. Worse
		 * than working, far better than off - and it says so where anyone
		 * looking at the log will find it. */
		if (!db_init(dev, lib, env)) {
			fprintf(stderr, "settings: cannot open the database at %s\n", dev);
			fprintf(stderr, "settings: continuing on the shipped defaults - "
			                "favorites, turbo and core options will be absent, "
			                "and nothing set here will be remembered\n");
		}
		/* Rewritten at every boot, not only on change: it is derived, so a
		 * card that lost it or never had one gets a correct one for free. */
		db_write_boot_env();
	}
	/* Local time is the card's chosen zone, in this process, from here on,
	 * and Diatom's with each game (resident.c's RUN). The system stays on
	 * UTC. */
	{
		char tz[64];

		db_get_str(db_lib(), "timezone", tz, sizeof tz, "America/New_York");
		plat_clock_zone(tz);
	}
	t_mark("databases");

	snprintf(path, sizeof path, "%s/systems.cfg", P_ROOT);
	if (!cfg_load_systems(path, &a.sys)) {
		fprintf(stderr, "no usable %s\n", path);
		return 1;
	}

	/* ChaOS: somewhere to put overlays, one folder per console. */
	overlay_dirs_ensure(&a);

	/* Before the scan, because the scan builds the Favorites shelf out of
	 * them and a shelf cannot be built from a list that has not been read. */
	fav_load();

	/* Close any session the device never saw end - a power cut, a flat
	 * battery - at its last checkpoint, before anything reads a total. See
	 * stats.h: short by up to the checkpoint interval beats lost entirely. */
	stats_recover();

	{	char cp[CFG_STR * 2];
		chv_earned_load();
		plat_resident_on_unlock(on_cheevo_unlocked);
		plat_resident_on_speed(on_speed);
		plat_resident_on_shot(on_shot);
		plat_resident_on_rewind(on_rewind);
		plat_resident_on_tick(on_game_tick);
		plat_resident_on_battery_empty(battery_empty);

		/* Without this every HTTPS request fails verification, because the
		 * device has no trust store of its own - res/ssl/README.md. */
		a.auto_off = auto_off_load();
		g_aout_policy = aout_load();
		g_channels = channels_load();

		snprintf(cp, sizeof cp, "%s/cacert.pem", P_ROOT);
		net_set_ca_path(cp);
		/* ChaOS: what is installed, and any update waiting on the card */
		upd_init(P_CARD, P_ROOT, CHAOS_VERSION, TORTOS_VERSION);
		ra_creds_load();
		ss_creds_load();
		{
			char bin[CFG_STR * 2], music[CFG_STR * 2];

			snprintf(bin, sizeof bin, "%s/muse", P_ROOT);
			snprintf(music, sizeof music, "%s", P_CARD);
			/* A shot starts no daemon: it draws one frame and exits, and a
			 * cover it would ask for is drawn as missing instead - see
			 * muse_covers_settle. */
			musec_init(shot_path ? "" : bin, music);
			musec_on_before_heard(aout_before_muse);
		}
		/* The play mode the player left it in. */
		{
			char m[32];
			int k;

			db_get_str(db_dev(), "muse.mode", m, sizeof m, "in order");
			if (shot_mode) snprintf(m, sizeof m, "%s", shot_mode);
			for (k = 0; k < MUQ_MODES; k++)
				if (!strcmp(m, MUSE_MODES[k].key)) musec_set_mode((muq_mode)k);
		}
	}
	t_mark("muse+accounts");

	/* Scan every system now, not when one is opened: it is three directory
	 * reads, it happens behind the boot animation, and it means walking into
	 * a system is a frame rather than a wait. */
	/* The Recently Played switch, read before the scan that builds the shelf */
	{
		char rs[16];

		db_get_str(db_dev(), "recent_shelf", rs, sizeof rs, "on");
		g_recent_shelf = strcmp(rs, "off") != 0;
		db_get_str(db_dev(), "fav_shelf", rs, sizeof rs, "on");
		g_fav_shelf = strcmp(rs, "off") != 0;
		db_get_str(db_dev(), "muse_shelf", rs, sizeof rs, "on");
		g_muse_shelf = strcmp(rs, "off") != 0;
		g_status_clock   = db_get_int(db_dev(), "status.clock", 1) != 0;
		g_status_clock24 = db_get_int(db_dev(), "status.clock24", 0) != 0;
		g_status_batt    = db_get_int(db_dev(), "status.battery", STATUS_BATT_ICON);
		if (g_status_batt < 0 || g_status_batt >= STATUS_BATT_COUNT) g_status_batt = STATUS_BATT_ICON;
	}
	scan_all(&a);
	t_mark("library scan");
	/* After the scan, because it indexes by system, and before anything can
	 * launch, because the mode has to reach Diatom with the first RUN. */
	look_load(&a);
	sort_load(&a);
	sort_all(&a);
	t_mark("scan");

	/* Where the shelf was and which card set it shows, read here rather than
	 * after the display is up: both are plain data, and they are what decides
	 * the cards the first frame shows, which the loader threads start on next.
	 * The card settings are only read by drawing, which comes long after. */
	{
		char set[CFG_STR];
		db_get_str(db_dev(), "cards", set, sizeof set, CARDS_DEFAULT);
		g_cards = cards_index(set);
		db_get_str(db_dev(), "cards_dir", set, sizeof set, CARDS_DIR_DEFAULT);
		g_dir = cards_dir_fit(g_cards, cards_dir_index(set));
		db_get_str(db_dev(), "menu_color", set, sizeof set, "chaos");
		g_menu_color = menu_color_index(set);
		notice_set_accent(MENU_ACCENT);
		pn_speed_load();
		db_get_str(db_dev(), "game_view", set, sizeof set, "normal");
		g_list_view = !strcmp(set, "list");
	}
	a.sys_cursor = 0;
	db_get_str(db_lib(), "startup_system", startup, sizeof startup, "");
	if (startup[0])
		for (int i = 0; i < a.sys.count; i++)
			if (strcasecmp(a.sys.systems[i].name, startup) == 0) {
				a.sys_cursor = i;
				break;
			}
	restore_place(&a);
	/* After restore_place, so it wins: `.last` says where the shelf was and
	 * this says what was actually being played. Before the card priming
	 * below, because that loads textures for whatever the cursor is on, and
	 * a fallback to the shelf should find the right ones there. */
	a.resume_menu = playing_restore(&a);
	cf_reset(&a.cf_sys, a.sys_cursor);
	a.tint = a.sys.systems[a.sys_cursor].accent;
	t_mark("restore place");

	/* After IMG_Init: the worker calls IMG_Load. Before the display, because
	 * neither needs it - decoding is CPU work, and making textures of what it
	 * decodes waits for the renderer, on this thread, in texload_drain - and
	 * because the threads would otherwise sit idle through video, input and
	 * settings. So the system cards the first frame shows are asked for now.
	 *
	 * Not for --shot. A shot draws the shelf exactly once, and with the workers
	 * running every card on that one frame is only queued - so the PNG came out
	 * with the title, the count and the rail and not a single card, while the
	 * log line named the focused game as though it had been drawn. With no
	 * worker, game_get_tex decodes on the frame the way it always did, which is
	 * what a tool for checking one frame wants: the right picture, not a fast
	 * one. */
	IMG_Init(IMG_INIT_PNG);
	loader = !shot_path && texload_start();
	if (loader && a.screen == SCREEN_SYSTEMS) sys_cards_ask_visible(&a);

	/* BEFORE the first frame this process ever draws: if a previous launcher
	 * died while a game was running, the resident is still presenting through
	 * fbdev right now, and drawing the shelf over it is the two-presenter
	 * case that wedges the display engine in-kernel (Diatom's handoff spike;
	 * it cost a power cycle to prove, twice). plat_resident_ready() connects,
	 * and on READY state=running it stops the game and drains to EXIT - so by
	 * the time video comes up, only one presenter exists. The check at launch
	 * time was too late by definition: this process draws long before the
	 * player launches anything. */
	plat_resident_ready();
	t_mark("diatom connect");

	if (!plat_video_init()) { fprintf(stderr, "video init failed\n"); return 1; }
	t_mark("video");
	a.r = plat_renderer();
	t_mark("renderer");
	plat_input_init();
	t_mark("input");
	/* Nothing is handed in any more. The two-tier lookup this replaces - a
	 * shipped default and the player's saved level - is one key each in the
	 * database, seeded once and overwritten by a nudge. That is also the end
	 * of a bug it kept reintroducing: reapplying the config afterwards put
	 * the shipped default ahead of the level the player last chose. */
	plat_settings_init();
	t_mark("settings");

	/* Before ui_init, which is where the sizes are decided; it persists across
	 * the ui_quit/ui_init pair the standalone-emulator fallback goes through. */
	/* The size the player chose beats the shipped default, the same way a
	 * saved brightness does. Read before ui_init, which is when the scale is
	 * applied. */
	if (!ui_init(a.r, P_FONT)) fprintf(stderr, "font init failed\n");
	texload_fonts(true);
	t_mark("font+settings");

	/* The system cards asked for before the display came up, installed so the
	 * first frame has every one it shows. Then the covers, and the rest of the
	 * system cards, which draw_shelf keeps asking for until all are in. */
	if (loader && a.screen == SCREEN_SYSTEMS) sys_cards_wait_visible(&a, 100);
	t_mark("system cards");
	prime_window(&a, a.sys_cursor);
	prime_sys_window(&a);
	t_mark("card assets");

	a.running = true;
	last_tint_ms = plat_now_ms();

	if (shot_updater_art) {
		updater_art(&a, shot_updater_art);
		goto done;
	}
	if (shot_path) {
		/* --sys N: the shelf on its Nth system */
		if (shot_sys >= 0 && a.sys.count > 0) {
			a.sys_cursor = shot_sys % a.sys.count;
			cf_reset(&a.cf_sys, a.sys_cursor);
			a.tint = a.sys.systems[a.sys_cursor].accent;
		}
		if (shot_screen >= 0) a.screen = (screen_id)shot_screen;
		if (shot_jump) {
			sysview *v = &a.view[a.sys_cursor];
			int k, dir = shot_jump > 0 ? 1 : -1;
			for (k = 0; k < (shot_jump < 0 ? -shot_jump : shot_jump); k++)
				if (v->list.count > 0) v->cursor = shelf_letter_jump(v, dir);
		}
		/* A shelf nobody has moved sits at position 0, wherever its cursor
		 * was restored to. On the device that never reaches the screen:
		 * update_games runs cf_set_cursor_dir every pass, which snaps an
		 * unmoved shelf to its cursor. A shot draws one frame and never runs
		 * that pass, so without this it drew the list's first card under the
		 * selected game's title. */
		cf_reset(&a.view[a.sys_cursor].cf, a.view[a.sys_cursor].cursor);
		/* CAUGHT MID-MOVE, for whatever only exists while one is happening.
		 *
		 * The pacing is applied first and the staging reads it, rather than the
		 * duration and the curve being named again here: they differ by
		 * direction mode, and a harness holding its own copy of them draws a
		 * frame the launcher never would. Cost of finding that out: --sysmove
		 * hardcoded Vertical's 360ms, so on a horizontal shelf it backdated the
		 * clock against a duration the draw did not use. */
		if (shot_sysmove != 0.0f) {
			shelf_pacing(&a.cf_sys);
			cf_stage(&a.cf_sys, (float)a.sys_cursor,
			         (float)a.sys_cursor + 1.0f, shot_sysmove);
		}
		/* The games shelf, with the cursor ALREADY at the destination - which
		 * is the state the device is in for the whole of a move, and the reason
		 * anything drawn from the cursor arrives early. */
		if (shot_gamemove != 0.0f) {
			sysview *v = &a.view[a.sys_cursor];

			if (v->list.count > 1) {
				int was = v->cursor;

				v->cursor = (v->cursor + 1) % v->list.count;
				shelf_pacing(&v->cf);
				cf_stage(&v->cf, (float)was, (float)was + 1.0f, shot_gamemove);
			}
		}
		if (a.screen == SCREEN_GAMES) prime_window(&a, a.sys_cursor);
		take_shot(&a);
		goto done;
	}

	/* Dev instrumentation, same standing as --shot: launch one game with no
	 * buttons pressed, so the resident path can be exercised over adb with
	 * nobody holding the device. TORTOS_AUTOLAUNCH="TAG<tab>rom-filename";
	 * pair with TORTOS_AUTOSTOP_S to end the game on a clock. */
	{
		const char *auto_spec = getenv("TORTOS_AUTOLAUNCH");
		if (auto_spec && strchr(auto_spec, '\t')) {
			char tag[64], file[LIB_PATH];
			const char *bar = strchr(auto_spec, '\t');
			int si, gi, found = 0;
			snprintf(tag, sizeof tag, "%.*s", (int)(bar - auto_spec), auto_spec);
			snprintf(file, sizeof file, "%s", bar + 1);
			for (si = 0; si < a.sys.count && !found; si++) {
				if (strcmp(a.sys.systems[si].tag, tag) != 0) continue;
				for (gi = 0; gi < a.view[si].list.count; gi++) {
					const char *b = strrchr(a.view[si].list.items[gi].file, '/');
					b = b ? b + 1 : a.view[si].list.items[gi].file;
					if (strcmp(b, file) == 0) {
						a.sys_cursor = si;
						a.view[si].cursor = gi;
						found = 1;
						break;
					}
				}
			}
			if (found) {
				fprintf(stderr, "autolaunch: %s / %s\n", tag, file);
				launch(&a);
			} else {
				fprintf(stderr, "autolaunch: no %s / %s in the library\n", tag, file);
			}
			goto done;
		}
	}

	/* Nothing pressed during the boot is honored. How long that takes is the
	 * device's to say - see plat_input_settle. */
	plat_input_settle(&a.in);

	/* Straight back into the game, before the shelf is ever drawn. After the
	 * input grace above, so a wake press replayed by the boot does not land in
	 * the menu that is about to open. */
	/* But not on an empty battery. A game draws far more than the shelf, and
	 * a device that comes back on with nothing left - the Pixel 2's power chip
	 * switches it on again as the cell recovers after a hard cut - resumed the
	 * game and lost power within a second, mid-write to the card. One reading,
	 * not battery_empty's two five seconds apart: this is the boot, and a wrong
	 * low reading costs only the resume. The shelf comes up on the game
	 * instead, .playing stays for the next boot, and battery_empty powers off
	 * cleanly a few seconds later if the battery really is empty. */
	if (a.resume_menu && a.running) {
		int  pct, mv;
		bool charging;

		if (plat_battery(&pct, &charging) && !charging &&
		    battery_at_or_below(pct, BATT_EMPTY_PCT, plat_battery_empty_mv(), &mv)) {
			if (mv > 0)
				fprintf(stderr, "battery: %d mV, too low to resume; the shelf instead\n", mv);
			else
				fprintf(stderr, "battery: %d%%, too low to resume; the shelf instead\n", pct);
			a.resume_menu = false;
		}
	}
	if (a.resume_menu && a.running) {
		a.screen = SCREEN_GAMES;
		launch(&a);
	}

	/* Drawing only when something changed. See g_redraw_at.
	 *
	 * The loop still runs every IDLE_POLL_MS when nothing is drawn, so the power
	 * button, Auto Off, the headphone jack and the Bluetooth sink are all still
	 * checked at the same rate as before - only the draw and the present are
	 * skipped. 16ms keeps the worst case from a press to its first frame where
	 * it was, one refresh.
	 *
	 * Why a poll and not a wait on the input devices: input arrives through two
	 * roads, SDL's own events and three raw evdev descriptors, and a held
	 * button produces no event at all while it repeats - in_repeat works from
	 * the clock. A wait that slept until the next event would have stopped key
	 * repeat dead. Polling costs a handful of syscalls a frame, against a full
	 * draw and a present. */
	const Uint32 IDLE_POLL_MS = 16;
	/* A gap this long between two passes means something else had the screen -
	 * a menu, a game, the art scraper - and whatever it drew is not the shelf.
	 * Catching it here covers every one of them without a list to keep. */
	const Uint32 AWAY_MS = 100;
	/* Drawn at least this often regardless. A backstop, not a mechanism: if some
	 * animation is ever added without telling g_redraw_at, it shows as a screen
	 * updating once a second, which is visibly wrong, rather than one that has
	 * silently frozen. */
	const Uint32 HEARTBEAT_MS = 1000;

	/* What the updater said, if it ran since the last start */
	update_after_start(&a);
	Uint32 last_pass = 0, last_render = 0;

	while (a.running) {
		plat_input_poll(&a.in);
		if (a.in.quit_requested || want_quit) break;

		/* Followed here as well as during a game. Diatom is resident and takes
		 * SETAUDIO while idle, so a cable plugged in at the shelf moves the
		 * sound before the next launch rather than at it. */
		aout_apply(false);

		/* An account answer that outlived its game, merged when it lands, and
		 * then whatever was waiting on it sent. */
		sync_poll();

		/* Unlocks sent in the background: apply what the account answered. */
		ra_flush_poll();

		/* ChaOS: the boot's update check, and whatever download is running */
		upd_poll();

		/* What Muse said since the last pass - including END, which is how
		 * the next track of an album starts while nobody is on its screen -
		 * and the covers it was asked for by Muse's shelf, which that shelf
		 * has to be drawn again to ask the worker for. */
		muse_poll();
		if (cover_answers()) redraw_now();

		/* Albums straight in Music/ filed under their artists, from what the
		 * player says their tags are; the shelf read again once anything
		 * moved. See file_album. */
		muse_file_albums();
		if (g_muse_moved && !flat_albums_waiting()) {
			g_muse_moved = false;
			rescan_all(&a);
			redraw_now();
		}

		/* Auto Off is the same line as the power button, on every screen
		 * that draws. Diatom watches it during a game, because it owns the
		 * pad then; everywhere else the launcher can see input itself.
		 *
		 * The charger HOLDS the clock, inside idle_due. It used to gate only
		 * the shot, which let the countdown run to zero while plugged in and
		 * sit there expired - so unplugging powered the device off in the
		 * same instant, however long it had been on the cable. Found on the
		 * device 2026-08-30 at a 30s timeout. Counting the charger as
		 * activity is also what the setting says: unplugging starts a whole
		 * fresh countdown, because until then the timeout was not running. */
		if (a.in.pressed[IN_POWER] || idle_due(&a)) { power_off(&a); break; }

		if (in_repeat(&a.in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a.in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a.in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a.in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		/* Back on the shelf, so any menu MENU closed is closed: put the flag
		 * down before anything here can open another. See menu_leaving. */
		g_menu_closing = false;

		/* SELECT is Muse, on the shelf and on every screen but a running
		 * game - see muse_open. Before the shelf's own keys, which do not use
		 * it, so every layout gets it from this one line. */
		if (a.in.pressed[IN_SELECT]) { muse_open(&a, NULL, NULL); continue; }

		/* MENU on the shelf is TortOS's own menu, the counterpart to the one
		 * MENU opens in a game. It draws over the shelf and returns here.
		 * Both handles its own, because there MENU and B mean two different
		 * menus and the screen cannot say which. */
		if (a.sys.count <= 0) {
			/* An empty card: the welcome (welcome_update), under MENU */
			if (a.in.pressed[IN_MENU]) { tortos_menu(&a); continue; }
			welcome_update(&a);
		} else if (CARD_DIRS[g_dir].both) {
			update_both(&a);
		} else {
			if (a.screen != SCREEN_SYSTEMS) g_seq_hist = ~0ull;
			else if (seq_step(&a.in)) { seq_card(&a); continue; }
			if (a.in.pressed[IN_MENU]) { tortos_menu(&a); continue; }
			if (a.screen == SCREEN_SYSTEMS) update_systems(&a);
			else update_games(&a);
		}
		if (!a.running) break;

		{
			Uint32 now = plat_now_ms();
			/* Held counts as touched, not only pressed: a held direction
			 * repeats from the clock and moves the shelf every 90ms. */
			bool touched = false, away = now - last_pass > AWAY_MS;
			int b;

			for (b = 0; b < IN_COUNT && !touched; b++)
				touched = a.in.pressed[b] || a.in.down[b];
			last_pass = now;

			/* texload_ready because finished art is installed while drawing:
			 * a loop that never drew would never find it. */
			if (touched || away || texload_ready() || now >= g_redraw_at ||
			    now - last_render >= HEARTBEAT_MS) {
				tick_tint(&a);
				render(&a);
				/* With the first frame up, Diatom maps its cores now (its
				 * PREMAP). Mapped the moment it listened, they were read off
				 * the card beside this process's GPU libraries and firmware,
				 * and the first frame came about 0.4 s later on most Brick
				 * boots (measured 2026-10-18). A Diatom that predates it maps
				 * them at once anyway; none at all, and this does nothing. */
				if (!last_render) {
					t_mark("first frame");
					plat_resident_line("PREMAP");
				}
				last_render = plat_now_ms();
			} else {
				SDL_Delay(IDLE_POLL_MS);
			}
		}
	}

done:
	free_all_textures(&a);
	/* First, and before IMG_Quit: the worker is inside IMG_Load, and the
	 * views it decodes into are freed just below. The resizer too, for the
	 * same IMG_Quit - it is in IMG_Load and IMG_SavePNG. */
	texload_stop();
	art_shrink_stop();
	/* Told, not waited for - see ra_flush_stop. */
	ra_flush_stop();
	for (int i = 0; i < a.sys.count; i++) {
		free(a.view[i].tex); free(a.view[i].tw); free(a.view[i].th);
		lib_free(&a.view[i].list);
	}
	ui_quit();
	IMG_Quit();
	plat_input_quit();
	plat_video_quit();
	SDL_Quit();

	/* Close the databases, which is what checkpoints their WAL and removes
	 * it. Without this the launcher never called sqlite3_close on a normal
	 * exit, so a checkpoint only ever happened when SQLite's own 1000-page
	 * autocheckpoint fired mid-write. tortos.db had crossed that and held 20
	 * keys; library.db had not, and its .db file was one empty page with the
	 * schema and every row living in library.db-wal. Measured 2026-09-07.
	 *
	 * That is not a growth problem - the WAL sits at its high-water mark
	 * either way. It is a portability one: a .db that means nothing without
	 * its sidecar is lost by any backup, card swap or cleanup that treats a
	 * -wal as scratch, and it reseeds silently rather than failing. Closing
	 * here leaves a complete file behind on every clean exit.
	 *
	 * A battery pull still leaves a WAL, which is what WAL recovery is for. */
	db_shutdown();
	return 0;
}
