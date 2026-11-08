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

	printf("the small update:\n");
	ck(ota_parse("version = 0.9.3\nbase = 2.1.1\nroot = r.squashfs.xz\nsha256 = " SHA "\nsize = 9\n"
	             "system = 0123abcd4567ef89\napp = ChaOS-v0.9.3-brick-app.tar.xz\napp_sha256 = " SHA
	             "\napp_size = 4000000\n", &o) && o.app_size == 4000000 && !strcmp(o.system, "0123abcd4567ef89"),
	   "its fields read");
	ck(ota_parse("version = 0.9.3\nbase = 2.1.1\nroot = r.squashfs.xz\nsha256 = " SHA "\nsize = 9\n"
	             "system = 0123abcd4567ef89\napp = ../x.tar.xz\napp_sha256 = " SHA "\napp_size = 4\n", &o) &&
	   !o.app[0] && !o.system[0] && !o.app_size, "a bad one is dropped, the whole one still good");
	ck(ota_parse("version = 0.9.3\nbase = 2.1.1\nroot = r.squashfs.xz\nsha256 = " SHA "\nsize = 9\n", &o) &&
	   !o.app[0], "and a release without one has none");

	printf("a small update installed:\n");
	{
		char d[] = "/tmp/update-app-XXXXXX", q2[2048], buf[64];
		FILE *f;
		if (!mkdtemp(d)) return 1;
		snprintf(q2, sizeof q2, "mkdir -p %s/ChaOS/overlays/GB %s/.dev/chaos-app/ChaOS/overlays/GB "
		         "%s/.dev/chaos-app/Licenses && echo old > %s/ChaOS/tortos.elf && echo 0.9.2 > %s/ChaOS/VERSION "
		         "&& echo mine > %s/ChaOS/keep && echo new > %s/.dev/chaos-app/ChaOS/tortos.elf "
		         "&& echo 0.9.3 > %s/.dev/chaos-app/ChaOS/VERSION && echo sh > %s/.dev/chaos-app/ChaOS/launch.sh "
		         "&& echo png > %s/.dev/chaos-app/ChaOS/overlays/GB/n.png && echo lic > %s/.dev/chaos-app/Licenses/ChaOS.md "
		         "&& printf '0.9.3\\napp\\n' > %s/.dev/chaos-update.version",
		         d, d, d, d, d, d, d, d, d, d, d, d);
		if (system(q2) != 0) fails++;
		snprintf(q2, sizeof q2, "%s/ChaOS", d);
		upd_init(d, q2, "0.9.2", "2.1.1");
		ck(upd_get() == UPD_READY && upd_latest()->small, "found waiting, small");
		ck(upd_arm(), "installed");
		snprintf(q2, sizeof q2, "%s/ChaOS/tortos.elf", d);
		f = fopen(q2, "r"); buf[0] = 0; if (f) { if (!fgets(buf, sizeof buf, f)) buf[0] = 0; fclose(f); }
		ck(!strcmp(buf, "new\n"), "the new launcher in place");
		snprintf(q2, sizeof q2, "%s/ChaOS/VERSION", d);
		f = fopen(q2, "r"); buf[0] = 0; if (f) { if (!fgets(buf, sizeof buf, f)) buf[0] = 0; fclose(f); }
		ck(!strcmp(buf, "0.9.3\n"), "and its version");
		snprintf(q2, sizeof q2, "%s/ChaOS/keep", d);
		ck(access(q2, F_OK) == 0, "a file it doesn't carry left alone");
		snprintf(q2, sizeof q2, "%s/ChaOS/overlays/GB/n.png", d);
		ck(access(q2, F_OK) == 0, "subfolders too");
		snprintf(q2, sizeof q2, "%s/Licenses/ChaOS.md", d);
		ck(access(q2, F_OK) == 0, "and the notices beside ChaOS/");
		snprintf(q2, sizeof q2, "%s/.dev/chaos-app", d);
		ck(access(q2, F_OK) != 0, "the download cleared away");
		snprintf(q2, sizeof q2, "rm -rf %s", d);
		if (system(q2) != 0) fails++;
	}

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
