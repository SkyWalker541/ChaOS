/* ChaOS: the screenshots' paths, list, names and deleting (src/shots.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../src/shots.h"

static int fails;
static void ck(int c, const char *what) { if (!c) { printf("  FAIL %s\n", what); fails++; } }

static void touch(const char *p) { FILE *f = fopen(p, "w"); if (f) fclose(f); }

int main(void)
{
	char dir[512], p[600], l[64], names[8][SHOTS_NAME];
	const char *card = "/tmp/chaos-shots-check";
	struct stat st;
	struct tm tm = { .tm_year = 126, .tm_mon = 10, .tm_mday = 8, .tm_hour = 18,
	                 .tm_min = 5, .tm_sec = 9, .tm_isdst = -1 };
	time_t t = mktime(&tm);
	int n;

	if (system("rm -rf /tmp/chaos-shots-check")) { }
	printf("screenshots:\n");
	ck(shots_dir(card, "GB", "/mnt/SDCARD/Roms/GB/Zelda (USA).gb", dir, sizeof dir) &&
	   !strcmp(dir, "/tmp/chaos-shots-check/Screenshots/GB/Zelda (USA)"),
	   "a game's folder: its console's folder, its name less the extension");
	ck(shots_list(dir, names, 8) == 0, "none before the first");
	ck(shots_new_path(dir, t, p, sizeof p) &&
	   !strcmp(p, "/tmp/chaos-shots-check/Screenshots/GB/Zelda (USA)/2026-11-08 18.05.09.png"),
	   "named by when, with no colons, the folders made");
	ck(stat(dir, &st) == 0, "the folder is there");
	touch(p);
	ck(shots_new_path(dir, t, p, sizeof p) && strstr(p, "18.05.09-2.png"),
	   "two in the same second do not collide");
	touch(p);
	t += 60;
	shots_new_path(dir, t, p, sizeof p);
	touch(p);
	snprintf(p, sizeof p, "%s/notes.txt", dir); touch(p);
	n = shots_list(dir, names, 8);
	ck(n == 3, "three listed, and not the text file");
	ck(n == 3 && !strcmp(names[0], "2026-11-08 18.06.09.png"), "newest first");
	shots_label(names[0], l, sizeof l);
	ck(!strcmp(l, "Nov 8, 6:06 PM"), "labelled by when");
	ck(!shots_delete(dir, "../x.png"), "a name with a slash is refused");
	ck(shots_delete(dir, names[0]) && shots_list(dir, names, 8) == 2, "deleting one");
	unlink(p);
	n = shots_list(dir, names, 8);
	shots_delete(dir, names[0]);
	shots_delete(dir, names[1]);
	ck(stat(dir, &st) != 0, "the last one takes the game's folder with it");
	snprintf(p, sizeof p, "%s/Screenshots/GB", card);
	ck(stat(p, &st) != 0, "and the console's");
	if (system("rm -rf /tmp/chaos-shots-check")) { }
	printf(fails ? "FAIL\n" : "\nok: screenshots are kept per game\n");
	return fails != 0;
}
