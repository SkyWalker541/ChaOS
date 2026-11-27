/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS Pad Test: what a port sees of the pad, shown and logged.
 *
 * A port, installed as one (tools/build-padtest.sh makes the zip), so it runs
 * the way every port does: through ChaOS's ports runner, with ChaOS's SDL,
 * CHAOS_PAD's mapping (Controls Layout, the Brick Pro's sticks) and
 * PortMaster's gptokeyb. Three columns: SDL's game controller by name (what
 * most ports read), the raw joystick by number (where each key really
 * lands), and the keys gptokeyb sends (what keyboard ports read). Every event
 * goes to the port's log; quitting (ChaOS's Select+Start, or power's Close
 * Port) writes which controller buttons and axes never moved. Made for the
 * Brick Pro's arrival, and as useful on a Brick. */
#include <SDL.h>
#include <SDL_ttf.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#define W 1024
#define H 768

static SDL_GameController *gc;
static SDL_Joystick *joy;
static int seen_btn[SDL_CONTROLLER_BUTTON_MAX], seen_axis[SDL_CONTROLLER_AXIS_MAX];
static int axis_lo[SDL_CONTROLLER_AXIS_MAX], axis_hi[SDL_CONTROLLER_AXIS_MAX];
static int raw_down[64], raw_seen[64];
static char keys[8][32];
static int nkeys;
static volatile sig_atomic_t stop;

static void on_term(int s) { (void)s; stop = 1; }

static void summary(void)
{
	int i, any = 0;

	printf("padtest: summary. controller buttons never pressed:");
	for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
		if (!seen_btn[i] && SDL_GameControllerGetStringForButton(i)) { printf(" %s", SDL_GameControllerGetStringForButton(i)); any = 1; }
	printf(any ? "\n" : " none\n");
	for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++)
		printf("padtest: axis %s %s, %d to %d\n", SDL_GameControllerGetStringForAxis(i),
		       seen_axis[i] ? "moved" : "never moved", axis_lo[i], axis_hi[i]);
	printf("padtest: raw buttons seen:");
	for (i = 0; i < 64; i++) if (raw_seen[i]) printf(" b%d", i);
	printf("\n");
	fflush(stdout);
}

static void text(SDL_Renderer *r, TTF_Font *f, const char *s, int x, int y, SDL_Color c)
{
	SDL_Surface *su;
	SDL_Texture *t;

	if (!f || !s || !*s || !(su = TTF_RenderUTF8_Blended(f, s, c))) return;
	if ((t = SDL_CreateTextureFromSurface(r, su))) {
		SDL_RenderCopy(r, t, NULL, &(SDL_Rect){ x, y, su->w, su->h });
		SDL_DestroyTexture(t);
	}
	SDL_FreeSurface(su);
}

static void open_pad(int i)
{
	char guid[64];

	if (SDL_IsGameController(i) && !gc) {
		gc = SDL_GameControllerOpen(i);
		printf("padtest: controller %d \"%s\", mapping: %s\n", i, SDL_GameControllerName(gc),
		       SDL_GameControllerMapping(gc) ? SDL_GameControllerMapping(gc) : "(none)");
	}
	if (!joy) {
		joy = SDL_JoystickOpen(i);
		if (joy) {
			SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), guid, sizeof guid);
			printf("padtest: joystick %d \"%s\" guid %s: %d buttons, %d axes, %d hats\n", i,
			       SDL_JoystickName(joy), guid, SDL_JoystickNumButtons(joy), SDL_JoystickNumAxes(joy),
			       SDL_JoystickNumHats(joy));
		}
	}
	fflush(stdout);
}

int main(int argc, char **argv)
{
	SDL_Window *win;
	SDL_Renderer *r;
	TTF_Font *f = NULL, *fs = NULL;
	SDL_Event e;
	int i, frames = 0;
	Uint32 t0;
	const char *font = argc > 1 ? argv[1] : "font.ttf";
	SDL_RendererInfo ri;
	SDL_Color on = { 61, 214, 255, 255 }, was = { 90, 190, 120, 255 }, off = { 120, 120, 130, 255 },
	          head = { 255, 200, 60, 255 };

	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	setvbuf(stdout, NULL, _IOLBF, 0);
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
		printf("padtest: SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	printf("padtest: SDL %d.%d.%d, video %s; CHAOS_PAD_LAYOUT=%s CHAOS_STICKS=%s\n",
	       SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL, SDL_GetCurrentVideoDriver(),
	       getenv("CHAOS_PAD_LAYOUT") ? getenv("CHAOS_PAD_LAYOUT") : "(xbox)",
	       getenv("CHAOS_STICKS") ? getenv("CHAOS_STICKS") : "(none)");
	win = SDL_CreateWindow("ChaOS Pad Test", 0, 0, W, H, SDL_WINDOW_FULLSCREEN);
	r = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
	if (!r) { printf("padtest: no renderer: %s\n", SDL_GetError()); return 1; }
	SDL_GetRendererInfo(r, &ri);
	{
		int ww, wh;
		SDL_GetWindowSize(win, &ww, &wh);
		printf("padtest: window %dx%d, renderer %s\n", ww, wh, ri.name);
	}
	if (TTF_Init() == 0) { f = TTF_OpenFont(font, 30); fs = TTF_OpenFont(font, 22); }
	if (!f) printf("padtest: no font at %s; drawing without words\n", font);
	for (i = 0; i < SDL_NumJoysticks(); i++) open_pad(i);
	if (!SDL_NumJoysticks()) printf("padtest: no joystick found\n");
	t0 = SDL_GetTicks();
	while (!stop) {
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_QUIT: stop = 1; break;
			case SDL_CONTROLLERDEVICEADDED: case SDL_JOYDEVICEADDED: open_pad(e.cdevice.which); break;
			case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP:
				if (e.cbutton.button < SDL_CONTROLLER_BUTTON_MAX) seen_btn[e.cbutton.button] = 1;
				printf("padtest: controller %s %s\n", SDL_GameControllerGetStringForButton(e.cbutton.button),
				       e.type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up");
				break;
			case SDL_CONTROLLERAXISMOTION:
				if (e.caxis.axis < SDL_CONTROLLER_AXIS_MAX) {
					int a = e.caxis.axis, v = e.caxis.value;
					if (v < axis_lo[a]) axis_lo[a] = v;
					if (v > axis_hi[a]) axis_hi[a] = v;
					if ((v > 8000 || v < -8000) && !seen_axis[a]) {
						seen_axis[a] = 1;
						printf("padtest: axis %s moved (%d)\n", SDL_GameControllerGetStringForAxis(a), v);
					}
				}
				break;
			case SDL_JOYBUTTONDOWN: case SDL_JOYBUTTONUP:
				if (e.jbutton.button < 64) {
					raw_down[e.jbutton.button] = e.type == SDL_JOYBUTTONDOWN;
					if (raw_down[e.jbutton.button]) raw_seen[e.jbutton.button] = 1;
				}
				printf("padtest: raw b%d %s\n", e.jbutton.button, e.type == SDL_JOYBUTTONDOWN ? "down" : "up");
				break;
			case SDL_JOYHATMOTION:
				printf("padtest: raw hat %d = %d\n", e.jhat.hat, e.jhat.value);
				break;
			case SDL_KEYDOWN: case SDL_KEYUP:
				printf("padtest: key %s %s\n", SDL_GetKeyName(e.key.keysym.sym), e.type == SDL_KEYDOWN ? "down" : "up");
				if (e.type == SDL_KEYDOWN) {
					memmove(keys[1], keys[0], sizeof keys - sizeof keys[0]);
					snprintf(keys[0], sizeof keys[0], "%s", SDL_GetKeyName(e.key.keysym.sym));
					if (nkeys < 8) nkeys++;
				}
				break;
			}
		}
		SDL_SetRenderDrawColor(r, 18, 18, 24, 255);
		SDL_RenderClear(r);
		text(r, f, "ChaOS Pad Test", 24, 12, head);
		text(r, fs, "Every press is in the port's log. Select+Start (ChaOS) quits.", 300, 20, off);
		text(r, fs, "Controller (by name)", 24, 64, head);
		for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
			const char *n = SDL_GameControllerGetStringForButton(i);
			int d = gc && SDL_GameControllerGetButton(gc, i);
			if (!n) continue;
			text(r, fs, n, 24 + (i / 11) * 170, 100 + (i % 11) * 30, d ? on : seen_btn[i] ? was : off);
		}
		for (i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++) {
			int v = gc ? SDL_GameControllerGetAxis(gc, i) : 0, y = 450 + i * 34;
			char s[48];
			snprintf(s, sizeof s, "%s %d", SDL_GameControllerGetStringForAxis(i), v);
			text(r, fs, s, 24, y, seen_axis[i] ? was : off);
			SDL_SetRenderDrawColor(r, 60, 60, 70, 255);
			SDL_RenderFillRect(r, &(SDL_Rect){ 230, y + 8, 160, 12 });
			SDL_SetRenderDrawColor(r, on.r, on.g, on.b, 255);
			SDL_RenderFillRect(r, &(SDL_Rect){ 310 + (v < 0 ? v * 80 / 32768 : 0), y + 8, (v < 0 ? -v : v) * 80 / 32768 + 1, 12 });
		}
		text(r, fs, "Raw joystick (by number)", 430, 64, head);
		for (i = 0; joy && i < SDL_JoystickNumButtons(joy) && i < 32; i++) {
			char s[8];
			snprintf(s, sizeof s, "b%d", i);
			text(r, fs, s, 430 + (i % 6) * 60, 100 + (i / 6) * 34, raw_down[i] ? on : raw_seen[i] ? was : off);
		}
		for (i = 0; joy && i < SDL_JoystickNumAxes(joy) && i < 8; i++) {
			char s[32];
			snprintf(s, sizeof s, "a%d %d", i, SDL_JoystickGetAxis(joy, i));
			text(r, fs, s, 430 + (i % 2) * 180, 330 + (i / 2) * 30, off);
		}
		text(r, fs, "Keys (gptokeyb)", 820, 64, head);
		for (i = 0; i < nkeys; i++) text(r, fs, keys[i], 820, 100 + i * 30, i ? off : on);
		SDL_RenderPresent(r);
		if (++frames == 120) { printf("padtest: %.1f frames a second\n", frames * 1000.0 / (SDL_GetTicks() - t0)); }
	}
	summary();
	SDL_Quit();
	return 0;
}
