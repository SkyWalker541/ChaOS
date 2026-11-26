/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: src/cleanup.c - what older releases left on a card goes, once per
 * version, and nothing else: saves, settings, ports, runtimes in use and a
 * player's own files stay. Built in a scratch folder. */
#include "cleanup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static bool here(const char *root, const char *rel)
{
	char p[1024];

	snprintf(p, sizeof p, "%s/%s", root, rel);
	return access(p, F_OK) == 0;
}

int main(void)
{
	char d[] = "/tmp/cleanup-check-XXXXXX", cmd[4096], ud[600], roms[600];
	int n;

	if (!mkdtemp(d)) return 1;
	snprintf(cmd, sizeof cmd,
	         "cd %s && mkdir -p ud/ports/installed ud/ports/home/.config ud/ports/lib ud/ports/checked "
	         "ud/ports/python.new/bin ud/ports/python/bin roms/Ports/PortMaster/libs/.chaos/weston_pkg_0.2 "
	         "roms/Ports/PortMaster/libs/.chaos/godot_4.3 roms/Ports/PortMaster/libs/.chaos/godot_4.3.new "
	         "roms/Ports/switchfin && touch ud/ports/.img-retired ud/ports/ports.json ud/ports/installed/x.txt "
	         "ud/ports/lib/libudev.so.1 ud/ports/lib/libevdev.so.2 ud/ports/libpack.json ud/ports/checked/a.sh "
	         "ud/ports/home/.config/save.cfg ud/ports/python/bin/python3 ud/tortos.db "
	         "roms/Ports/.chaos-2048.zip roms/Ports/.chaos-portmaster roms/Ports/Switchfin.sh "
	         "roms/Ports/switchfin/port.json roms/Ports/PortMaster/libs/weston_pkg_0.2.squashfs "
	         "roms/Ports/PortMaster/libs/.chaos/weston_pkg_0.2/wp_weston "
	         "roms/Ports/PortMaster/libs/.chaos/godot_4.3/godot", d);
	if (system(cmd) != 0) { printf("FAIL: couldn't build the card\n"); return 1; }
	snprintf(ud, sizeof ud, "%s/ud", d);
	snprintf(roms, sizeof roms, "%s/roms", d);

	printf("cleanup: what old releases left goes\n");
	n = cleanup_run(ud, roms, "9.9.9-test");
	CHECK(n == 7, "seven things removed (%d)", n);
	CHECK(!here(d, "ud/ports/ports.json") && !here(d, "ud/ports/installed"), "the old screens' catalog and lists");
	CHECK(!here(d, "ud/ports/python.new") && !here(d, "ud/ports/lib/libudev.so.1"), "a cut-short unpack, the test libudev");
	CHECK(!here(d, "roms/Ports/.chaos-2048.zip"), "a cut-short download");
	CHECK(!here(d, "roms/Ports/PortMaster/libs/.chaos/godot_4.3") &&
	      !here(d, "roms/Ports/PortMaster/libs/.chaos/godot_4.3.new"), "an orphaned runtime, and its cut-short unpack");

	printf("and nothing else\n");
	CHECK(here(d, "ud/tortos.db") && here(d, "ud/ports/home/.config/save.cfg"), "settings and a port's saves");
	CHECK(here(d, "ud/ports/python/bin/python3") && here(d, "ud/ports/lib/libevdev.so.2") &&
	      here(d, "ud/ports/libpack.json") && here(d, "ud/ports/checked/a.sh"), "what ports still use");
	CHECK(here(d, "roms/Ports/.chaos-portmaster") && here(d, "roms/Ports/Switchfin.sh") &&
	      here(d, "roms/Ports/switchfin/port.json"), "PortMaster's card, a port and its folder");
	CHECK(here(d, "roms/Ports/PortMaster/libs/.chaos/weston_pkg_0.2/wp_weston"), "a runtime in use, unpacked");

	printf("once per version\n");
	CHECK(cleanup_run(ud, roms, "9.9.9-test") == -1, "not again for the same version");
	CHECK(cleanup_run(ud, roms, "9.9.10-test") == 0, "a new version runs, and finds nothing");

	/* the old screens' lists stay while their pictures haven't been retired */
	snprintf(cmd, sizeof cmd, "cd %s && rm -f ud/ports/.img-retired && mkdir -p ud/ports/img ud/ports/installed", d);
	if (system(cmd) != 0) fails++;
	cleanup_run(ud, roms, "9.9.11-test");
	CHECK(here(d, "ud/ports/installed"), "install lists kept until the pictures are retired");

	snprintf(cmd, sizeof cmd, "rm -rf %s", d);
	if (system(cmd) != 0) fails++;
	if (fails) { printf("cleanup: %d failed\n", fails); return 1; }
	printf("ok: old releases' leftovers removed, and nothing else\n");
	return 0;
}
