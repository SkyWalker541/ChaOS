/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: updates over Wi-Fi. See update.h. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "update.h"
#include "net.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/mount.h>
#endif

#define OTA_URL "https://github.com/SkyWalker541/ChaOS/releases/latest/download/ChaOS-ota.txt"
#define DL_URL  "https://github.com/SkyWalker541/ChaOS/releases/download/v%s/%s"
#define OTA_TMP "/tmp/chaos-ota.txt"
#define BOOT_MNT "/tmp/chaos-boot"

static char g_dev[256];          /* <card>/.dev */
static char g_dl[300];           /* the download, compressed */
static char g_dl_part[310];
static char g_root_upd[300];     /* what rootswitch looks for */
static char g_stamp[300];        /* which version that is */
static char g_version[32];       /* running */
static char g_base[32];          /* this card's TortOS base */

static upd_state g_state = UPD_IDLE;
static ota_info  g_latest;
static char      g_err[120];
static pid_t     g_pid = -1;     /* the check, the download or the unpack */
static bool      g_checked;      /* answered, this boot */
static bool      g_check_failed; /* the last FAILED was the check's */
static time_t    g_auto_at;      /* the boot's own check, last tried */
static char      g_announced[32];
static char      g_row[48];

/* ---- parsing -------------------------------------------------------------- */

static void trim(char *s)
{
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
	memmove(s, s + strspn(s, " \t"), strlen(s + strspn(s, " \t")) + 1);
}

static bool version_ok(const char *v)
{
	int dots = 0;
	if (!isdigit((unsigned char)*v)) return false;
	for (; *v; v++) {
		if (*v == '.') dots++;
		else if (!isdigit((unsigned char)*v)) return false;
	}
	return dots >= 1 && dots <= 3;
}

bool ota_parse(const char *text, ota_info *o)
{
	const char *p = text;
	bool hex = true;
	int i;

	memset(o, 0, sizeof *o);
	while (p && *p) {
		const char *nl = strchr(p, '\n');
		size_t n = nl ? (size_t)(nl - p) : strlen(p);
		char line[256], *eq;

		if (n >= sizeof line) n = sizeof line - 1;
		memcpy(line, p, n);
		line[n] = '\0';
		p = nl ? nl + 1 : NULL;
		if (line[0] == '#' || !(eq = strchr(line, '='))) continue;
		*eq = '\0';
		trim(line);
		trim(eq + 1);
#define FIELD(name) if (!strcmp(line, #name)) snprintf(o->name, sizeof o->name, "%s", eq + 1)
		FIELD(version);
		else FIELD(base);
		else FIELD(root);
		else FIELD(sha256);
		else FIELD(notes);
		else if (!strcmp(line, "size")) o->size = atoll(eq + 1);
#undef FIELD
	}
	for (i = 0; i < 64; i++)
		if (!isxdigit((unsigned char)o->sha256[i])) hex = false;
	/* The root's name is a file name in the release, nothing else */
	return version_ok(o->version) && o->base[0] && o->root[0] &&
	       !strchr(o->root, '/') && strstr(o->root, ".squashfs.xz") &&
	       hex && strlen(o->sha256) == 64 && o->size > 0;
}

int ota_vercmp(const char *a, const char *b)
{
	while (*a || *b) {
		long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
		if (x != y) return x < y ? -1 : 1;
		if (*a == '.') a++;
		if (*b == '.') b++;
		if (!*a && !*b) break;
	}
	return 0;
}

/* ---- the card's side ------------------------------------------------------ */

static bool read_small(const char *path, char *out, size_t n)
{
	FILE *f = fopen(path, "r");
	size_t got;
	if (!f) return false;
	got = fread(out, 1, n - 1, f);
	fclose(f);
	out[got] = '\0';
	return got > 0;
}

static void fail(const char *why)
{
	snprintf(g_err, sizeof g_err, "%s", why);
	g_state = UPD_FAILED;
	fprintf(stderr, "update: %s\n", why);
}

/* A finished download waiting from before, or leftovers of an installed one */
static void look_on_card(void)
{
	char v[32];
	struct stat st;

	if (read_small(g_stamp, v, sizeof v)) {
		trim(v);
		if (stat(g_root_upd, &st) == 0 && version_ok(v) &&
		    ota_vercmp(v, g_version) > 0) {
			snprintf(g_latest.version, sizeof g_latest.version, "%s", v);
			snprintf(g_latest.base, sizeof g_latest.base, "%s", g_base);
			g_state = UPD_READY;
			return;
		}
		/* Installed (rootswitch removed the root), or older than this */
		unlink(g_root_upd);
		unlink(g_stamp);
	}
	unlink(g_dl);
	unlink(g_dl_part);
}

void upd_init(const char *card, const char *root, const char *version,
              const char *base)
{
	char path[300], b[32];

	snprintf(g_dev, sizeof g_dev, "%s/.dev", card);
	snprintf(g_dl, sizeof g_dl, "%s/chaos-update.squashfs.xz", g_dev);
	snprintf(g_dl_part, sizeof g_dl_part, "%s.part", g_dl);
	snprintf(g_root_upd, sizeof g_root_upd, "%s/root-update.squashfs", g_dev);
	snprintf(g_stamp, sizeof g_stamp, "%s/chaos-update.version", g_dev);
	snprintf(g_version, sizeof g_version, "%s", version);
	snprintf(path, sizeof path, "%s/BASE", root);
	if (read_small(path, b, sizeof b)) { trim(b); snprintf(g_base, sizeof g_base, "%s", b); }
	else snprintf(g_base, sizeof g_base, "%s", base);
	look_on_card();
}

upd_state upd_get(void) { return g_state; }
const ota_info *upd_latest(void) { return &g_latest; }
const char *upd_error(void) { return g_err; }
const char *upd_base(void) { return g_base; }

int upd_percent(void)
{
	struct stat st;
	if (g_state == UPD_DOWNLOADING && g_latest.size > 0 && stat(g_dl_part, &st) == 0) {
		long long p = (long long)st.st_size * 100 / g_latest.size;
		return p > 99 ? 99 : (int)p;
	}
	return 0;
}

/* ---- the steps -------------------------------------------------------------- */

bool upd_check(void)
{
	g_err[0] = '\0';
	if (g_pid > 0) return g_state == UPD_CHECKING;
	if (g_state == UPD_READY) return true;   /* nothing to ask until it goes */
	unlink(OTA_TMP);
	g_pid = net_get_spawn(OTA_URL, OTA_TMP, 20);
	if (g_pid <= 0) { fail("Couldn't start the check"); return false; }
	g_state = UPD_CHECKING;
	return true;
}

static void checked(bool ok)
{
	char text[2048];
	ota_info o;

	g_check_failed = true;
	if (!ok || !read_small(OTA_TMP, text, sizeof text)) {
		fail("Couldn't reach GitHub");
		return;
	}
	unlink(OTA_TMP);
	if (!ota_parse(text, &o)) { fail("Update info unreadable"); return; }
	g_check_failed = false;
	g_checked = true;
	g_latest = o;
	if (ota_vercmp(o.version, g_version) <= 0) g_state = UPD_CURRENT;
	else if (strcmp(o.base, g_base) != 0) g_state = UPD_INSTALLER;
	else g_state = UPD_AVAILABLE;
	fprintf(stderr, "update: running %s on %s, latest %s on %s\n",
	        g_version, g_base, o.version, o.base);
}

bool upd_download(void)
{
	char url[400];
	struct statvfs vs;

	if (!g_latest.root[0]) return false;
	if (g_state != UPD_AVAILABLE && !(g_state == UPD_FAILED && g_latest.version[0] &&
	                                  !strcmp(g_latest.base, g_base)))
		return false;
	g_check_failed = false;
	g_err[0] = '\0';
	mkdir(g_dev, 0755);
	/* The download, then the root it unpacks to (about 1.4 times as big),
	 * with room to spare */
	if (statvfs(g_dev, &vs) == 0 &&
	    (long long)vs.f_bavail * (long long)vs.f_frsize < g_latest.size * 4 + (64LL << 20)) {
		fail("Not enough space on the card");
		return false;
	}
	unlink(g_dl_part);
	snprintf(url, sizeof url, DL_URL, g_latest.version, g_latest.root);
	g_pid = net_get_spawn(url, g_dl_part, 1800);
	if (g_pid <= 0) { fail("Couldn't start the download"); return false; }
	g_state = UPD_DOWNLOADING;
	fprintf(stderr, "update: downloading %s\n", url);
	return true;
}

/* Checked against the release's SHA-256, unpacked (xz checks its own
 * contents), and put where rootswitch looks, all by the device's own tools in
 * one child, so the launcher keeps drawing */
static void unpack(void)
{
	pid_t pid;

	if (rename(g_dl_part, g_dl) != 0) { fail("The download went missing"); return; }
	pid = fork();
	if (pid < 0) { fail("Couldn't check the download"); return; }
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c",
		      "h=$(sha256sum \"$1\" | cut -d' ' -f1) || exit 2\n"
		      "[ \"$h\" = \"$2\" ] || exit 3\n"
		      "xz -dc \"$1\" > \"$3.part\" || exit 4\n"
		      "head -c 4 \"$3.part\" | grep -q hsqs || exit 5\n"
		      "sync\n"
		      "mv -f \"$3.part\" \"$3\" && echo \"$4\" > \"$5\" && rm -f \"$1\" && sync\n",
		      "unpack", g_dl, g_latest.sha256, g_root_upd, g_latest.version, g_stamp,
		      (char *)NULL);
		_exit(127);
	}
	g_pid = pid;
	g_state = UPD_UNPACKING;
}

static void unpacked(int code)
{
	char part[320];

	snprintf(part, sizeof part, "%s.part", g_root_upd);
	unlink(part);
	if (code == 0) {
		g_state = UPD_READY;
		fprintf(stderr, "update: %s checked and ready\n", g_latest.version);
		return;
	}
	unlink(g_dl);
	/* Every message here is in the README's table (make check-update) */
	if (code == 3) fail("Checksum didn't match");
	else if (code == 4 || code == 5) fail("The download wouldn't unpack");
	else fail("Couldn't check the download");
}

void upd_cancel(void)
{
	if (g_pid > 0 && (g_state == UPD_DOWNLOADING || g_state == UPD_UNPACKING)) {
		kill(g_pid, SIGTERM);
		waitpid(g_pid, NULL, 0);
		net_spawn_done(g_pid);
		g_pid = -1;
	}
	if (g_state == UPD_DOWNLOADING || g_state == UPD_UNPACKING) {
		char part[320];
		snprintf(part, sizeof part, "%s.part", g_root_upd);
		unlink(g_dl_part);
		unlink(g_dl);
		unlink(part);
		g_state = UPD_AVAILABLE;
	}
}

bool upd_remove(void)
{
	if (g_state != UPD_READY) return false;
	unlink(g_root_upd);
	unlink(g_stamp);
	g_state = g_latest.root[0] ? UPD_AVAILABLE : UPD_IDLE;
	return true;
}

bool upd_tick(bool online)
{
	upd_state was = g_state;
	int st;

	/* The boot's own check: when Wi-Fi is up, and again a quarter of an hour
	 * after one that could not reach GitHub, until one answers */
	if (!g_checked && online && g_pid <= 0 &&
	    (g_state == UPD_IDLE || (g_state == UPD_FAILED && g_check_failed)) &&
	    time(NULL) - g_auto_at >= 15 * 60) {
		g_auto_at = time(NULL);
		upd_check();
	}
	if (g_pid > 0 && waitpid(g_pid, &st, WNOHANG) == g_pid) {
		int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
		pid_t done = g_pid;

		g_pid = -1;
		switch (g_state) {
		case UPD_CHECKING:
			net_spawn_done(done);
			checked(code == 0);
			break;
		case UPD_DOWNLOADING:
			net_spawn_done(done);
			if (code == 0) unpack();
			else { unlink(g_dl_part); fail("The download stopped"); }
			break;
		case UPD_UNPACKING:
			unpacked(code);
			break;
		default:
			break;
		}
	}
	return g_state != was;
}

void upd_offline(bool radio_off)
{
	if (g_pid > 0) return;
	g_check_failed = !g_latest.version[0];
	if (radio_off) fail("Wi-Fi is off");
	else fail("Wi-Fi isn't connected");
}

/* ---- arming the boot ------------------------------------------------------ */

/* /dev/<name> of the GPT partition called `label` */
static bool partition(const char *label, char *dev, size_t n, long long *bytes)
{
	DIR *d = opendir("/sys/class/block");
	struct dirent *e;
	bool found = false;

	if (!d) return false;
	while (!found && (e = readdir(d))) {
		char path[300], line[128], want[80];
		FILE *f;

		snprintf(path, sizeof path, "/sys/class/block/%s/uevent", e->d_name);
		if (!(f = fopen(path, "r"))) continue;
		snprintf(want, sizeof want, "PARTNAME=%s\n", label);
		while (fgets(line, sizeof line, f))
			if (!strcmp(line, want)) { found = true; break; }
		fclose(f);
		if (found) {
			snprintf(dev, n, "/dev/%s", e->d_name);
			if (bytes) {
				snprintf(path, sizeof path, "/sys/class/block/%s/size", e->d_name);
				*bytes = read_small(path, line, sizeof line) ? atoll(line) * 512 : 0;
			}
		}
	}
	closedir(d);
	return found;
}

bool upd_arm(void)
{
#ifdef __linux__
	char boot[64], root[64], scr[4096], marker[64];
	unsigned char magic[4] = { 0 };
	long long room = 0;
	struct stat st;
	FILE *f;
	int fd;
	bool knows;

	if (g_state != UPD_READY) return false;
	g_err[0] = '\0';
	if (stat(g_root_upd, &st) != 0 || !(f = fopen(g_root_upd, "rb"))) {
		fail("The download is gone");
		unlink(g_stamp);
		g_state = UPD_FAILED;
		return false;
	}
	if (fread(magic, 1, 4, f) != 4) magic[0] = 0;
	fclose(f);
	if (memcmp(magic, "hsqs", 4) != 0) {
		fail("The download isn't a system");
		g_state = UPD_READY;   /* Remove Download is the way out */
		return false;
	}
	if (!partition("plastron-root", root, sizeof root, &room) || st.st_size > room) {
		fail("Too big for this card");
		g_state = UPD_READY;   /* still on the card, to try again */
		return false;
	}
	if (!partition("boot", boot, sizeof boot, NULL)) {
		fail("No boot partition found");
		g_state = UPD_READY;   /* still on the card, to try again */
		return false;
	}
	mkdir(BOOT_MNT, 0755);
	if (mount(boot, BOOT_MNT, "vfat", 0, NULL) != 0) {
		fail("Couldn't open the boot files");
		g_state = UPD_READY;   /* still on the card, to try again */
		return false;
	}
	/* This boot must be one that installs a waiting root: TortOS 2.1.1's
	 * does, and any card that cannot is told to use the installer rather
	 * than left with a file it will never act on */
	knows = read_small(BOOT_MNT "/boot.scr", scr, sizeof scr) &&
	        memmem(scr, sizeof scr - 1, "root-update", 11);
	if (knows) {
		snprintf(marker, sizeof marker, "%s/root-update", BOOT_MNT);
		fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0 || write(fd, "1\n", 2) != 2 || fsync(fd) != 0) knows = false;
		if (fd >= 0) close(fd);
	}
	sync();
	umount(BOOT_MNT);
	if (!knows) {
		fail("Needs ChaOS Installer");
		g_state = UPD_READY;   /* still on the card, to try again */
		return false;
	}
	fprintf(stderr, "update: %s armed for the next boot\n", g_latest.version);
	return true;
#else
	fail("No boot partition found");   /* a computer has none of the Brick's */
	return false;
#endif
}

void upd_demo(upd_state s, const char *version, const char *error)
{
	snprintf(g_latest.version, sizeof g_latest.version, "%s", version);
	snprintf(g_latest.base, sizeof g_latest.base, "%s", g_base);
	snprintf(g_latest.root, sizeof g_latest.root, "ChaOS-v%s-brick-root.squashfs.xz", version);
	g_latest.size = 37680452;
	snprintf(g_err, sizeof g_err, "%s", error ? error : "");
	g_state = s;
}

/* ---- what the shelf and the menu say ------------------------------------- */

bool upd_announce(char *out, int outn)
{
	if ((g_state != UPD_AVAILABLE && g_state != UPD_INSTALLER) ||
	    !strcmp(g_announced, g_latest.version))
		return false;
	snprintf(g_announced, sizeof g_announced, "%s", g_latest.version);
	snprintf(out, outn, "ChaOS %s is available: MENU > Updates", g_latest.version);
	return true;
}

const char *upd_row_value(void)
{
	switch (g_state) {
	case UPD_AVAILABLE:
	case UPD_INSTALLER:
		snprintf(g_row, sizeof g_row, "%s available", g_latest.version);
		return g_row;
	case UPD_DOWNLOADING:
		snprintf(g_row, sizeof g_row, "downloading %d%%", upd_percent());
		return g_row;
	case UPD_UNPACKING:
		return "checking";
	case UPD_READY:
		snprintf(g_row, sizeof g_row, "%s ready", g_latest.version);
		return g_row;
	default:
		return NULL;
	}
}

