/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: a game's cover set from the phone. See cover.h. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "cover.h"
#include "net.h"
#include "platform.h"

#include <SDL.h>
#include <SDL_image.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "library.h"            /* LIB_PATH */

/* ---- ChaOS: a game's cover set from the phone (hare_set_cover) ------------
 *
 * The page sends a picture, or a link the device follows itself: a page can't
 * read another site's pictures, the device can. A link may be the picture's
 * own address, a Google Images result (its imgurl= is the picture), or an
 * itch.io game's page, whose public data.json names the cover its developer
 * chose. Whatever arrives must decode as a picture; it is written as the
 * game's PNG beside the old one and renamed over it once whole, and hare's
 * after-write (hareart_after_write) shrinks it as every uploaded cover is.
 * Runs on the transfer screen's request, which waits for it. */

#define COVER_DL "/tmp/chaos-cover-link"

/* "%2F" and the like in a query value, in place */
static void url_unescape(char *s)
{
	char *o = s;

	for (; *s; s++) {
		if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
			char h[3] = { s[1], s[2], 0 };
			*o++ = (char)strtol(h, NULL, 16);
			s += 2;
		} else {
			*o++ = *s == '+' ? ' ' : *s;
		}
	}
	*o = '\0';
}

/* The host of an http(s) address, lower-cased, into `out`; false when it is
 * not one */
static bool url_host(const char *u, char *out, size_t n)
{
	const char *h, *e;
	size_t i, l;

	if (!strncmp(u, "https://", 8)) h = u + 8;
	else if (!strncmp(u, "http://", 7)) h = u + 7;
	else return false;
	e = h + strcspn(h, "/?#:");
	l = (size_t)(e - h);
	if (!l || l >= n) return false;
	for (i = 0; i < l; i++) out[i] = (char)tolower((unsigned char)h[i]);
	out[l] = '\0';
	return true;
}

static bool cover_fetch(const char *url, char *why, size_t wn)
{
	pid_t pid;
	int st = 0;

	unlink(COVER_DL);
	pid = net_get_spawn(url, COVER_DL, 25);
	if (pid <= 0) { snprintf(why, wn, "Couldn't start the download"); return false; }
	waitpid(pid, &st, 0);
	net_spawn_done(pid);
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
		snprintf(why, wn, "Couldn't download that link");
		unlink(COVER_DL);
		return false;
	}
	return true;
}

bool cover_from_web(const char *rom, const char *url, const char *file,
                           char *out, size_t on, char *why, size_t wn)
{
	char dir[LIB_PATH * 3], stem[LIB_PATH], link[1024], host[256], tmp[LIB_PATH * 3 + 8];
	const char *src = file, *rest, *slash, *dot;
	SDL_Surface *s;
	struct stat st;
	size_t rl = strlen(P_ROMS);
	bool ok;

	/* A game in a console's folder: Roms/<console>/<file>, a file, and
	 * nothing deeper (a cover lives beside the games it is for) */
	if (strncmp(rom, P_ROMS, rl) || rom[rl] != '/') { snprintf(why, wn, "That isn't a game"); return false; }
	rest = rom + rl + 1;
	slash = strchr(rest, '/');
	if (!slash || strchr(slash + 1, '/') || stat(rom, &st) != 0 || !S_ISREG(st.st_mode)) {
		snprintf(why, wn, "That isn't a game");
		return false;
	}
	snprintf(dir, sizeof dir, "%.*s/.media", (int)(slash - rom), rom);
	snprintf(stem, sizeof stem, "%s", slash + 1);
	if ((dot = strrchr(stem, '.')) && dot != stem) stem[dot - stem] = '\0';

	if (!src) {
		if (!net_online()) { snprintf(why, wn, "The Brick needs Wi-Fi to follow a link"); return false; }
		snprintf(link, sizeof link, "%s", url);
		link[strcspn(link, " \t\r\n")] = '\0';
		if (!url_host(link, host, sizeof host)) {
			snprintf(why, wn, "That isn't a web address");
			return false;
		}
		/* A Google Images result: the picture is its imgurl= */
		if (strstr(host, "google.")) {
			const char *q = strstr(link, "imgurl=");

			if (q) {
				char pic[1024];

				snprintf(pic, sizeof pic, "%.*s", (int)strcspn(q + 7, "&#"), q + 7);
				url_unescape(pic);
				snprintf(link, sizeof link, "%s", pic);
				if (!url_host(link, host, sizeof host)) {
					snprintf(why, wn, "That Google link doesn't lead to a picture");
					return false;
				}
			}
		}
		/* An itch.io game's page: the cover its developer chose, from the
		 * page's data.json (public; itch.io widened it for
		 * NextUI-Itchio-Pak) */
		{
			size_t hl = strlen(host);

			if (hl > 8 && !strcmp(host + hl - 8, ".itch.io")) {
				char dj[1100], *buf = malloc(64 * 1024), *k, img[512];
				long got;
				size_t i = 0;

				if (!buf) { snprintf(why, wn, "Out of memory"); return false; }
				snprintf(dj, sizeof dj, "%.*s/data.json", (int)strcspn(link, "?#"), link);
				/* "…/game/" and "…/game" alike */
				{
					char *d = strstr(dj, "//data.json");
					if (d) memmove(d, d + 1, strlen(d));
				}
				got = net_get_buf(dj, NULL, 0, buf, 64 * 1024 - 1, 20);
				if (got <= 0) { free(buf); snprintf(why, wn, "itch.io didn't answer for that game"); return false; }
				buf[got] = '\0';
				k = strstr(buf, "\"cover_image\"");
				if (k) k = strchr(k + 13, '"');
				if (!k) { free(buf); snprintf(why, wn, "That itch.io game has no cover"); return false; }
				for (k++; *k && *k != '"' && i + 1 < sizeof img; k++)
					if (!(k[0] == '\\' && k[1] == '/')) img[i++] = *k;     /* "\/" is "/" */
				img[i] = '\0';
				free(buf);
				if (strncmp(img, "https://img.itch.zone/", 22)) {
					snprintf(why, wn, "That itch.io game has no cover");
					return false;
				}
				snprintf(link, sizeof link, "%s", img);
			}
		}
		if (!cover_fetch(link, why, wn)) return false;
		src = COVER_DL;
	}

	s = IMG_Load(src);
	if (src == COVER_DL) unlink(COVER_DL);
	if (!s) { snprintf(why, wn, "That isn't a picture the Brick can read"); return false; }
	mkdir(dir, 0755);
	snprintf(out, on, "%s/%s.png", dir, stem);
	snprintf(tmp, sizeof tmp, "%s.new", out);
	ok = IMG_SavePNG(s, tmp) == 0 && rename(tmp, out) == 0;
	SDL_FreeSurface(s);
	if (!ok) { unlink(tmp); snprintf(why, wn, "Couldn't write the cover"); return false; }
	fprintf(stderr, "cover: %s from %s\n", out, file ? "the phone" : link);
	return true;
}

