/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: ports (src/ports.h) on a card in /tmp - the scripts the shelf
 * lists, the ones refused, and PortMaster fetched (from a file:// zip, so
 * no network) and put in place, including over a broken copy.
 *
 *   make check-ports */
#include "library.h"
#include "ports.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;

#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); putchar('\n'); } } while (0)

static void put(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");

	if (!f) { perror(path); exit(1); }
	fputs(text, f);
	fclose(f);
}

static ports_setup_state run_setup(const char *roms)
{
	ports_setup_state st = PS_IDLE;
	int i;

	if (!ports_setup_begin(roms)) return PS_FAILED;
	for (i = 0; i < 3000; i++) {
		st = ports_setup_step();
		if (st == PS_DONE || st == PS_FAILED) break;
		usleep(10 * 1000);
	}
	return st;
}

int main(void)
{
	char tmpl[] = "/tmp/chaos-ports-check-XXXXXX";
	char *t = mkdtemp(tmpl);
	char roms[256], p[512], why[96], url[600], cmd[1200];
	game_list l = { 0 };
	int i;
	bool found_port = false;

	if (!t) { perror("mkdtemp"); return 1; }
	snprintf(roms, sizeof roms, "%s/Roms", t);
	snprintf(p, sizeof p, "%s/Ports/Some Game", roms);
	snprintf(cmd, sizeof cmd, "mkdir -p '%s'", p);
	if (system(cmd)) return 1;

	/* The shelf: scripts at the top, never a port's folder of files */
	snprintf(p, sizeof p, "%s/Ports/Some Game.sh", roms);
	put(p, "#!/bin/bash\necho hello\n");
	snprintf(p, sizeof p, "%s/Ports/Some Game/data.bin", roms);
	put(p, "not a disc");
	snprintf(p, sizeof p, "%s/Ports/Some Game/inner.sh", roms);
	put(p, "#!/bin/bash\n");
	snprintf(p, sizeof p, "%s/Ports/readme.txt", roms);
	put(p, "hi");
	CHECK(lib_scan(roms, "Ports", "sh", &l), "the Ports shelf scans");
	CHECK(l.count == 1, "one port listed, not %d", l.count);
	for (i = 0; i < l.count; i++)
		if (!strcmp(l.items[i].file, "Some Game.sh") && !strcmp(l.items[i].title, "Some Game"))
			found_port = true;
	CHECK(found_port, "the script, titled without .sh");
	CHECK(l.skipped == 1, "readme.txt skipped, folders not counted: %d", l.skipped);
	lib_free(&l);

	/* Scripts looked at before they run */
	snprintf(p, sizeof p, "%s/Ports/Some Game.sh", roms);
	CHECK(ports_script_ok(p, why, sizeof why), "a #! script is fine");
	snprintf(p, sizeof p, "%s/Ports/bad.sh", roms);
	put(p, "\x7f" "ELF....");
	CHECK(!ports_script_ok(p, why, sizeof why) && why[0], "a binary named .sh is refused");
	put(p, "");
	CHECK(!ports_script_ok(p, why, sizeof why), "an empty script is refused");
	snprintf(p, sizeof p, "%s/Ports/none.sh", roms);
	CHECK(!ports_script_ok(p, why, sizeof why), "a missing script is refused");

	/* PortMaster: not there, then fetched */
	CHECK(!ports_pm_ready(roms), "no PortMaster yet");
	snprintf(cmd, sizeof cmd,
	         "cd '%s' && mkdir -p z/PortMaster && echo control > z/PortMaster/control.txt && "
	         "echo 2026 > z/PortMaster/version && cd z && "
	         "python3 -I -c 'import zipfile,os\n"
	         "z=zipfile.ZipFile(\"../pm.zip\",\"w\")\n"
	         "[z.write(os.path.join(d,f)) for d,_,fs in os.walk(\"PortMaster\") for f in fs]' && "
	         "echo nope > ../notzip.zip", t);
	if (system(cmd)) { printf("couldn't make the zip\n"); return 1; }

	snprintf(url, sizeof url, "file://%s/notzip.zip", t);
	ports_setup_url(url);
	CHECK(run_setup(roms) == PS_FAILED, "a download that isn't a zip fails");
	CHECK(!strcmp(ports_setup_problem(), "The download wasn't PortMaster"),
	      "and says so: %s", ports_setup_problem());
	CHECK(!ports_pm_ready(roms), "and leaves nothing");

	snprintf(url, sizeof url, "file://%s/missing.zip", t);
	ports_setup_url(url);
	CHECK(run_setup(roms) == PS_FAILED, "a download that fails, fails");

	/* A broken PortMaster folder already there: kept, renamed */
	snprintf(p, sizeof p, "%s/Ports/PortMaster", roms);
	mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/Ports/PortMaster/config.json", roms);
	put(p, "{}");
	snprintf(url, sizeof url, "file://%s/pm.zip", t);
	ports_setup_url(url);
	CHECK(run_setup(roms) == PS_DONE, "PortMaster fetched: %s", ports_setup_problem());
	CHECK(ports_pm_ready(roms), "and ready");
	{
		DIR *d;
		struct dirent *e;
		bool old = false, leftover = false;

		snprintf(p, sizeof p, "%s/Ports", roms);
		d = opendir(p);
		while (d && (e = readdir(d))) {
			char kept[800];

			if (!strncmp(e->d_name, "PortMaster.old-", 15)) {
				snprintf(kept, sizeof kept, "%s/%s/config.json", p, e->d_name);
				old = access(kept, R_OK) == 0;
			}
			if (!strcmp(e->d_name, ".PortMaster.zip") || !strcmp(e->d_name, ".PortMaster.new"))
				leftover = true;
		}
		if (d) closedir(d);
		CHECK(old, "the broken folder kept, with what was in it");
		CHECK(!leftover, "no download or staging left behind");
	}
	/* An update of a working PortMaster: its runtimes and settings come
	 * along, and no old copy is left */
	snprintf(p, sizeof p, "%s/Ports/PortMaster/libs", roms);
	mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/Ports/PortMaster/libs/godot.squashfs", roms);
	put(p, "rt");
	snprintf(p, sizeof p, "%s/Ports/PortMaster/config", roms);
	mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/Ports/PortMaster/config/config.json", roms);
	put(p, "{\"theme\": 1}");
	CHECK(run_setup(roms) == PS_DONE, "PortMaster updated: %s", ports_setup_problem());
	snprintf(p, sizeof p, "%s/Ports/PortMaster/libs/godot.squashfs", roms);
	CHECK(access(p, R_OK) == 0, "the runtimes kept through an update");
	snprintf(p, sizeof p, "%s/Ports/PortMaster/config/config.json", roms);
	CHECK(access(p, R_OK) == 0, "the settings kept through an update");
	{
		DIR *d;
		struct dirent *e;
		int olds = 0;

		snprintf(p, sizeof p, "%s/Ports", roms);
		d = opendir(p);
		while (d && (e = readdir(d)))
			if (!strncmp(e->d_name, "PortMaster.old-", 15)) olds++;
		if (d) closedir(d);
		CHECK(olds == 1, "only the broken one kept, not the updated one: %d", olds);
	}
	/* Uninstalled keeping its settings, then installed again: they're back */
	snprintf(cmd, sizeof cmd, "cd '%s/Ports' && mkdir -p .PortMaster.kept && "
	         "mv PortMaster/config .PortMaster.kept/ && rm -rf PortMaster", roms);
	CHECK(system(cmd) == 0, "uninstalled");
	CHECK(!ports_pm_ready(roms), "gone");
	CHECK(run_setup(roms) == PS_DONE, "installed again: %s", ports_setup_problem());
	snprintf(p, sizeof p, "%s/Ports/PortMaster/config/config.json", roms);
	CHECK(access(p, R_OK) == 0, "the kept settings back");
	snprintf(p, sizeof p, "%s/Ports/.PortMaster.kept", roms);
	CHECK(access(p, F_OK) != 0, "and the kept folder tidied away");
	CHECK(lib_scan(roms, "Ports", "sh", &l) && l.count == 2,
	      "PortMaster's folder isn't a port: %d", l.count);
	lib_free(&l);

	snprintf(cmd, sizeof cmd, "rm -rf '%s'", t);
	if (system(cmd)) { /* left in /tmp */ }
	if (fails) { printf("%d port check(s) failed\n", fails); return 1; }
	printf("ok: every port check passed\n");
	return 0;
}
