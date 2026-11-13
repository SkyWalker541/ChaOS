/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: a game's cover from ChaOS Transfer's page (src/cover.c), against a
 * card in /tmp. Offline by default; CHAOS_NET_CHECKS=1 also follows real
 * links (a picture on GitHub, the same through a Google Images result, and a
 * page that is no picture). */
#include "../src/cover.h"

#include <SDL.h>
#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *P_ROMS;

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void put(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	fputs(text, f);
	fclose(f);
}

static int png_w(const char *path)
{
	SDL_Surface *s = IMG_Load(path);
	int w = s ? s->w : -1;

	if (s) SDL_FreeSurface(s);
	return w;
}

int main(void)
{
	char root[] = "/tmp/chaos-cover-check-XXXXXX", roms[600], game[700], pic[700], out[700],
	     why[128], media[700];
	SDL_Surface *s;
	bool ok;

	if (!mkdtemp(root)) return 2;
	snprintf(roms, sizeof roms, "%s/Roms", root);
	P_ROMS = roms;
	mkdir(roms, 0755);
	snprintf(game, sizeof game, "%s/NES", roms);
	mkdir(game, 0755);
	snprintf(game, sizeof game, "%s/NES/Super Tilt Bro (Homebrew).nes", roms);
	put(game, "NES\x1a");
	snprintf(media, sizeof media, "%s/NES/.media/Super Tilt Bro (Homebrew).png", roms);

	/* A picture from the phone */
	s = SDL_CreateRGBSurfaceWithFormat(0, 315, 250, 32, SDL_PIXELFORMAT_RGBA32);
	SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 200, 80, 40, 255));
	snprintf(pic, sizeof pic, "%s/phone.png", root);
	IMG_SavePNG(s, pic);
	SDL_FreeSurface(s);

	printf("a picture from the phone\n");
	why[0] = '\0';
	ok = cover_from_web(game, NULL, pic, out, sizeof out, why, sizeof why);
	CHECK(ok, "taken: %s", why);
	CHECK(!strcmp(out, media), "as the game's .media PNG: %s", out);
	CHECK(png_w(media) == 315, "a picture there, the size sent: %d", png_w(media));
	CHECK(access(pic, F_OK) == 0, "the sent file is the caller's to remove");

	printf("what isn't taken\n");
	put(pic, "not a picture at all");
	ok = cover_from_web(game, NULL, pic, out, sizeof out, why, sizeof why);
	CHECK(!ok && strstr(why, "picture"), "something that isn't a picture: %s", why);
	CHECK(png_w(media) == 315, "and the old cover stays");
	{
		char deep[700], top[700], other[700];

		snprintf(deep, sizeof deep, "%s/NES/sub", roms);
		mkdir(deep, 0755);
		snprintf(deep, sizeof deep, "%s/NES/sub/game.nes", roms);
		put(deep, "x");
		CHECK(!cover_from_web(deep, NULL, pic, out, sizeof out, why, sizeof why),
		      "a game in a folder in a console's folder");
		snprintf(top, sizeof top, "%s/game.nes", roms);
		put(top, "x");
		CHECK(!cover_from_web(top, NULL, pic, out, sizeof out, why, sizeof why),
		      "a file in Roms itself");
		snprintf(other, sizeof other, "%s/elsewhere.nes", root);
		put(other, "x");
		CHECK(!cover_from_web(other, NULL, pic, out, sizeof out, why, sizeof why),
		      "a file outside Roms");
		snprintf(other, sizeof other, "%s/NES/missing.nes", roms);
		CHECK(!cover_from_web(other, NULL, pic, out, sizeof out, why, sizeof why),
		      "a game that isn't there");
		snprintf(other, sizeof other, "%sX/NES/a.nes", roms);
		CHECK(!cover_from_web(other, NULL, pic, out, sizeof out, why, sizeof why),
		      "a folder whose name only starts like Roms");
	}
	CHECK(!cover_from_web(game, "ftp://example.com/a.png", NULL, out, sizeof out, why, sizeof why) &&
	      strstr(why, "web address"), "a link that isn't http(s): %s", why);
	CHECK(!cover_from_web(game, "not a link", NULL, out, sizeof out, why, sizeof why),
	      "text that isn't a link");

	if (getenv("CHAOS_NET_CHECKS")) {
		const char *png = "https://raw.githubusercontent.com/SkyWalker541/ChaOS/main/res/readme/chaos-menu.png";
		char g[1024];

		printf("links (the network)\n");
		unlink(media);
		ok = cover_from_web(game, png, NULL, out, sizeof out, why, sizeof why);
		CHECK(ok && png_w(media) == 512, "a picture's own address: %s %d", why, png_w(media));
		unlink(media);
		snprintf(g, sizeof g, "https://www.google.com/imgres?imgurl=%s&imgrefurl=https%%3A%%2F%%2Fexample.com&tbnid=abc",
		         "https%3A%2F%2Fraw.githubusercontent.com%2FSkyWalker541%2FChaOS%2Fmain%2Fres%2Freadme%2Fchaos-menu.png");
		ok = cover_from_web(game, g, NULL, out, sizeof out, why, sizeof why);
		CHECK(ok && png_w(media) == 512, "a Google Images result, by its imgurl: %s", why);
		ok = cover_from_web(game, "https://raw.githubusercontent.com/SkyWalker541/ChaOS/main/README.md",
		                    NULL, out, sizeof out, why, sizeof why);
		CHECK(!ok && strstr(why, "picture") && png_w(media) == 512,
		      "a page that is no picture leaves the cover: %s", why);
		ok = cover_from_web(game, "https://raw.githubusercontent.com/SkyWalker541/ChaOS/main/no-such-file.png",
		                    NULL, out, sizeof out, why, sizeof why);
		CHECK(!ok && strstr(why, "download"), "a link that 404s: %s", why);
	}

	{
		char cmd[700];
		snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
		if (system(cmd) != 0) { }
	}
	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("ok: every cover check passed\n");
	return 0;
}
