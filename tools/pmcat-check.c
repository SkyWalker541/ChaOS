/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: src/pmcat.c against a real ports.json.
 *
 *   make check-pmcat PMCAT_JSON=/path/to/ports.json
 *
 * Without one it checks a small catalog of its own. Either way: every port
 * read, the sections add up, the runtimes each port needs are all in the
 * catalog's utils, and an installed port is seen, with its update. */
#include "pmcat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *mini =
	"{\"ports\": {"
	"\"a.zip\": {\"version\": 3, \"name\": \"a.zip\", \"items\": [\"A Game.sh\", \"agame/\"],"
	"  \"attr\": {\"title\": \"A \\u00e9 Game\", \"porter\": [\"x\", \"y\"], \"desc\": \"Line\\none\","
	"   \"genres\": [\"action\", \"puzzle\"], \"image\": {\"screenshot\": \"screenshot.jpg\"},"
	"   \"rtr\": true, \"exp\": false, \"runtime\": [\"weston_pkg_0.2\"], \"reqs\": [\"power\"], \"arch\": []},"
	"  \"source\": {\"md5\": \"abc\", \"size\": 10, \"url\": \"https://x/a.zip\", \"date_added\": \"2024-02-03\"}},"
	"\"b.zip\": {\"version\": 1, \"items\": [\"b/\", \"B.sh\"], \"attr\": {\"title\": \"Be\", \"rtr\": false,"
	"   \"genres\": [\"action\"], \"reqs\": [\"analog_2\"], \"runtime\": []}, \"source\": {}},"
	"\"c.zip\": {\"version\": 1, \"items\": [\"C.sh\", \"c\"], \"attr\": {\"title\": \"Ce\", \"rtr\": true,"
	"   \"genres\": [\"puzzle\"], \"reqs\": [\"analog_1|analog_2\"], \"arch\": [\"aarch64\"]}},"
	"\"d.zip\": {\"version\": 1, \"items\": [\"D.sh\", \"d\"], \"attr\": {\"title\": \"De\","
	"   \"arch\": [\"armhf\", \"x86_64\"]}}"
	"}, \"utils\": {\"weston_pkg_0.2.squashfs\": {\"url\": \"https://x/w\", \"md5\": \"m\", \"size\": 5},"
	"  \"zulu.aarch64.squashfs\": {\"url\": \"https://x/z\", \"md5\": \"z\", \"size\": 6}}}";

static void write_file(const char *path, const char *s)
{
	FILE *f = fopen(path, "w");

	if (f) { fputs(s, f); fclose(f); }
}

static void check_mini(void)
{
	char dir[] = "/tmp/pmcat-check-XXXXXX", p[300], url[300], md5[64], rt[4][64], txt[64];
	pm_catalog c;
	int idx[8], n, a, b, cc;
	long sz;

	if (!mkdtemp(dir)) { fails++; return; }
	snprintf(p, sizeof p, "%s/ports.json", dir);
	write_file(p, mini);
	snprintf(url, sizeof url, "%s/Ports", dir);
	mkdir(url, 0755);
	snprintf(url, sizeof url, "%s/Ports/agame", dir);
	mkdir(url, 0755);
	snprintf(url, sizeof url, "%s/Ports/agame/port.json", dir);
	write_file(url, "{\"version\": 2, \"name\": \"a.zip\"}");
	snprintf(url, sizeof url, "%s/tested.txt", dir);
	write_file(url, "# tried\nc.zip\n");
	{
		char ports[300];

		snprintf(ports, sizeof ports, "%s/Ports", dir);
		CHECK(pmcat_load(&c, p, ports, url), "mini catalog loads");
	}
	CHECK(c.n == 4, "4 ports, got %d", c.n);
	a = pmcat_find(&c, "a.zip"); b = pmcat_find(&c, "b.zip"); cc = pmcat_find(&c, "c.zip");
	CHECK(a >= 0 && b >= 0 && cc >= 0, "found by key");
	if (a < 0 || b < 0 || cc < 0) return;
	CHECK(!strcmp(c.p[a].title, "A \xC3\xA9 Game"), "title unescaped: %s", c.p[a].title);
	CHECK(!strcmp(c.p[a].dir, "agame") && !strcmp(c.p[a].script, "A Game.sh"), "dir %s script %s",
	      c.p[a].dir, c.p[a].script);
	CHECK(!strcmp(c.p[b].dir, "b") && !strcmp(c.p[b].script, "B.sh"), "items either order");
	CHECK(!strcmp(c.p[a].porter, "x, y") && !strcmp(c.p[a].genres, "action, puzzle"), "joined");
	CHECK(c.p[a].flags & PMF_POWER && !(c.p[a].flags & PMF_NO), "power warns only");
	CHECK(c.p[a].flags & PMF_RUNTIME, "runtime flagged");
	CHECK(c.p[a].flags & PMF_INSTALLED && c.p[a].flags & PMF_UPDATE && c.p[a].have_version == 2,
	      "installed v2, catalog v3: an update");
	CHECK(c.p[a].added == 20240203 && c.p[a].size == 10, "added %d size %ld", c.p[a].added, c.p[a].size);
	CHECK(c.p[b].flags & PMF_NO && !strcmp(c.p[b].why, "Needs two analog sticks"), "analog_2 out");
	CHECK(!(c.p[cc].flags & PMF_NO), "analog_1|analog_2 is fine: the d-pad is a stick");
	CHECK(c.p[cc].flags & PMF_TESTED, "tested list read");
	CHECK(c.p[pmcat_find(&c, "d.zip")].flags & PMF_NO, "no aarch64 build: out");
	n = pmcat_list(&c, PMS_TESTED, NULL, idx, 8);
	CHECK(n == 1 && idx[0] == cc, "Runs on ChaOS: c");
	n = pmcat_list(&c, PMS_MAY, NULL, idx, 8);
	CHECK(n == 1 && idx[0] == a, "May Run: a only (b, d ruled out, c tested), got %d", n);
	n = pmcat_list(&c, PMS_ALL, NULL, idx, 8);
	CHECK(n == 4, "all 4");
	CHECK(n == 4 && !(c.p[idx[3]].flags & PMF_RTR) && c.p[idx[0]].flags & PMF_RTR, "ready to run first");
	n = pmcat_list(&c, PMS_ALL, "puzzle", idx, 8);
	CHECK(n == 2, "genre puzzle: 2, got %d", n);
	n = pmcat_list(&c, PMS_UPDATES, NULL, idx, 8);
	CHECK(n == 1 && idx[0] == a, "updates: a");
	CHECK(pmcat_text(&c.p[a], "desc", txt, sizeof txt) && !strcmp(txt, "Line\none"), "desc");
	CHECK(pmcat_source(&c.p[a], url, sizeof url, md5, sizeof md5) && !strcmp(url, "https://x/a.zip")
	      && !strcmp(md5, "abc"), "source");
	CHECK(!pmcat_source(&c.p[b], url, sizeof url, md5, sizeof md5), "no url, no source");
	n = pmcat_runtimes(&c.p[a], rt, 4);
	CHECK(n == 1 && !strcmp(rt[0], "weston_pkg_0.2.squashfs"), "runtime named as its script wants");
	CHECK(pmcat_runtime(&c, rt[0], url, sizeof url, md5, sizeof md5, &sz) && sz == 5, "runtime found");
	CHECK(pmcat_runtime(&c, "zulu.squashfs", url, sizeof url, md5, sizeof md5, &sz)
	      && !strcmp(url, "https://x/z"), "the aarch64 build stands in");
	CHECK(pmcat_shot(&c.p[a], url, sizeof url, txt, sizeof txt) && !strcmp(txt, "a.jpg") &&
	      !strcmp(url, PMCAT_IMG_BASE "a/screenshot.jpg"), "shot %s", url);
	pmcat_free(&c);
	{
		char cmd[400];

		snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
		if (system(cmd) != 0) fails++;
	}
}

/* A port's zip, unpacked, played (a save appears), then removed keeping
 * saves: its own files go, the save and its folder stay */
static void check_manifest(void)
{
	char dir[] = "/tmp/pmcat-man-XXXXXX", cmd[1200], p[400], man[400];
	int gone;

	if (!mkdtemp(dir)) { fails++; return; }
	snprintf(cmd, sizeof cmd,
	         "cd '%s' && mkdir -p Ports src/game/data && echo s > 'src/Game One.sh' && "
	         "echo a > src/game/a.bin && echo b > src/game/data/b.bin && "
	         "cd src && python3 -I -c 'import zipfile; z=zipfile.ZipFile(\"../g.zip\",\"w\"); "
	         "z.write(\"Game One.sh\"); z.write(\"game/\"); z.write(\"game/a.bin\"); "
	         "z.write(\"game/data/\"); z.write(\"game/data/b.bin\"); z.close()' && "
	         "cd .. && unzip -oq g.zip -d Ports && echo save > Ports/game/save.dat", dir);
	if (system(cmd) != 0) { fails++; printf("FAIL: couldn't make the zip\n"); return; }
	snprintf(p, sizeof p, "%s/g.zip", dir);
	snprintf(man, sizeof man, "%s/g.txt", dir);
	CHECK(pmcat_zip_names(p, man), "zip listed");
	{
		FILE *f = fopen(man, "a");

		if (f) { fputs("../outside.txt\n/etc/passwd\n", f); fclose(f); }
		snprintf(cmd, sizeof cmd, "echo keep > '%s/outside.txt'", dir);
		if (system(cmd) != 0) fails++;
	}
	snprintf(p, sizeof p, "%s/Ports", dir);
	gone = pmcat_manifest_remove(p, man);
	CHECK(gone == 3, "its 3 files went, got %d", gone);
	snprintf(p, sizeof p, "%s/Ports/game/save.dat", dir);
	CHECK(access(p, R_OK) == 0, "the save stays");
	snprintf(p, sizeof p, "%s/Ports/game/data", dir);
	CHECK(access(p, F_OK) != 0, "a folder left empty goes");
	snprintf(p, sizeof p, "%s/Ports/Game One.sh", dir);
	CHECK(access(p, F_OK) != 0, "the script goes");
	snprintf(p, sizeof p, "%s/outside.txt", dir);
	CHECK(access(p, F_OK) == 0, "nothing outside Ports, whatever the list says");
	/* and once the save goes too, the folder does */
	snprintf(p, sizeof p, "%s/Ports/game/save.dat", dir);
	unlink(p);
	snprintf(p, sizeof p, "%s/Ports", dir);
	pmcat_manifest_remove(p, man);
	snprintf(p, sizeof p, "%s/Ports/game", dir);
	CHECK(access(p, F_OK) != 0, "an emptied port's folder goes");
	snprintf(p, sizeof p, "%s/nope.zip", dir);
	CHECK(!pmcat_zip_names(man, p), "not a zip: no list");
	snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
	if (system(cmd) != 0) fails++;
}

static void check_real(const char *path)
{
	pm_catalog c;
	int i, n[PMS_COUNT], missing = 0, rtn = 0, idx[1];
	char rt[8][64], url[400], md5[64];
	long sz;

	CHECK(pmcat_load(&c, path, NULL, NULL), "%s loads", path);
	if (!c.n) return;
	for (i = 0; i < PMS_COUNT; i++) n[i] = pmcat_list(&c, (pm_section)i, NULL, idx, 1);
	printf("  %d ports: may run %d, all %d\n", c.n, n[PMS_MAY], n[PMS_ALL]);
	CHECK(n[PMS_ALL] == c.n, "all is all");
	CHECK(n[PMS_MAY] > c.n / 2, "most may run");
	for (i = 0; i < c.n; i++) {
		int k, nr = pmcat_runtimes(&c.p[i], rt, 8);

		CHECK(c.p[i].dir[0] && c.p[i].script[0], "%s has a folder and a script", c.p[i].zip);
		CHECK(pmcat_source(&c.p[i], url, sizeof url, md5, sizeof md5) && md5[0], "%s source", c.p[i].zip);
		for (k = 0; k < nr; k++) {
			rtn++;
			if (!pmcat_runtime(&c, rt[k], url, sizeof url, md5, sizeof md5, &sz)) {
				missing++;
				printf("  %s: runtime %s not in utils\n", c.p[i].zip, rt[k]);
			}
		}
	}
	printf("  %d runtime needs, %d not found\n", rtn, missing);
	pmcat_free(&c);
}

int main(int argc, char **argv)
{
	const char *real = argc > 1 ? argv[1] : getenv("PMCAT_JSON");

	check_mini();
	check_manifest();
	if (real && real[0]) check_real(real);
	if (fails) { printf("pmcat: %d failed\n", fails); return 1; }
	printf("pmcat: ok\n");
	return 0;
}
