/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: what src/update.c decides without a network - reading a release's
 * ChaOS-ota.txt, ordering versions, and what it makes of a card with a
 * download already on it. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/update.h"

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static void put(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	if (f) { fputs(text, f); fclose(f); }
}

#define SHA "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

int main(void)
{
	ota_info o;
	char dir[] = "/tmp/update-check-XXXXXX", p[256], q[256], msg[96];

	printf("ChaOS-ota.txt:\n");
	ck(ota_parse("# ChaOS\nversion = 0.8.0\nbase = 2.1.1\n"
	             "root = ChaOS-v0.8.0-brick-root.squashfs.xz\nsha256 = " SHA "\n"
	             "size = 37680452\nnotes = Updates over Wi-Fi\n", &o),
	   "a release's file reads");
	ck(!strcmp(o.version, "0.8.0") && !strcmp(o.base, "2.1.1") && o.size == 37680452 &&
	   !strcmp(o.notes, "Updates over Wi-Fi"), "every field, spaces trimmed");
	ck(!ota_parse("version = 0.8.0\nbase = 2.1.1\nroot = x.squashfs.xz\nsize = 1\n", &o),
	   "no checksum, no update");
	ck(!ota_parse("version = 0.8.0\nbase = 2.1.1\nroot = ../x.squashfs.xz\nsha256 = " SHA
	              "\nsize = 1\n", &o), "the root is a file name, not a path");
	ck(!ota_parse("version = 0.8.0\nbase = 2.1.1\nroot = x.squashfs.xz\nsha256 = 12\nsize = 1\n", &o),
	   "a checksum is 64 hex digits");
	ck(!ota_parse("version = latest\nbase = 2.1.1\nroot = x.squashfs.xz\nsha256 = " SHA
	              "\nsize = 1\n", &o), "a version is numbers");
	ck(!ota_parse("<html>Not Found</html>", &o), "and a web page is not one");

	printf("versions:\n");
	ck(ota_vercmp("0.10.0", "0.9.2") > 0, "0.10.0 is after 0.9.2");
	ck(ota_vercmp("0.7.1", "0.7.1") == 0, "the same is the same");
	ck(ota_vercmp("0.7", "0.7.1") < 0 && ota_vercmp("1.0.0", "0.99.9") > 0, "and the rest");

	printf("a card with a download on it:\n");
	if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
	snprintf(p, sizeof p, "%s/.dev", dir); mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/.dev/root-update.squashfs", dir); put(p, "hsqs....");
	snprintf(q, sizeof q, "%s/.dev/chaos-update.version", dir); put(q, "0.8.0\n");
	upd_init(dir, dir, "0.7.1", "2.1.1");
	ck(upd_get() == UPD_READY && !strcmp(upd_latest()->version, "0.8.0"),
	   "a newer one waiting is ready to install");
	ck(upd_row_value() && !strcmp(upd_row_value(), "0.8.0 ready"), "and the menu row says so");
	ck(!upd_announce(msg, sizeof msg), "ready is not announced: it is the player's already");
	ck(upd_remove() && access(p, F_OK) != 0 && access(q, F_OK) != 0,
	   "Remove Download removes it");

	put(p, "hsqs....");
	put(q, "0.8.0\n");
	upd_init(dir, dir, "0.8.0", "2.1.1");
	ck(upd_get() != UPD_READY && access(p, F_OK) != 0 && access(q, F_OK) != 0,
	   "one already installed is cleared away");
	snprintf(p, sizeof p, "%s/BASE", dir); put(p, "2.2.0\n");
	upd_init(dir, dir, "0.8.0", "2.1.1");
	ck(!strcmp(upd_base(), "2.2.0"), "the card's BASE says which TortOS it is on");

	snprintf(p, sizeof p, "rm -rf %s", dir);
	if (system(p) != 0) fails++;
	if (fails) { printf("%d FAILED\n", fails); return 1; }
	printf("ok: every check passed\n");
	return 0;
}
