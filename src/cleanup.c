/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See cleanup.h. */
#include "cleanup.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void rm_tree(const char *path)
{
	struct stat st;
	DIR *d;
	struct dirent *e;

	if (lstat(path, &st) != 0) return;
	if (!S_ISDIR(st.st_mode)) { unlink(path); return; }
	if ((d = opendir(path))) {
		while ((e = readdir(d))) {
			char p[1024];

			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			snprintf(p, sizeof p, "%s/%s", path, e->d_name);
			rm_tree(p);
		}
		closedir(d);
	}
	rmdir(path);
}

static int gone(const char *path, const char *why)
{
	struct stat st;

	if (lstat(path, &st) != 0) return 0;
	rm_tree(path);
	if (lstat(path, &st) == 0) {
		fprintf(stderr, "cleanup: couldn't remove %s\n", path);
		return 0;
	}
	fprintf(stderr, "cleanup: removed %s (%s)\n", path, why);
	return 1;
}

int cleanup_run(const char *userdata, const char *roms, const char *version)
{
	char p[1024], q[1024], mark[1024], had[64] = "";
	FILE *f;
	DIR *d;
	struct dirent *e;
	int n = 0;

	snprintf(mark, sizeof mark, "%s/cleanup", userdata);
	if ((f = fopen(mark, "r"))) {
		if (!fgets(had, sizeof had, f)) had[0] = '\0';
		fclose(f);
		had[strcspn(had, "\n")] = '\0';
	}
	if (version && !strcmp(had, version)) return -1;

	/* the old PortMaster screens */
	snprintf(p, sizeof p, "%s/ports/ports.json", userdata);
	n += gone(p, "the old PortMaster screens' catalog");
	snprintf(p, sizeof p, "%s/ports/.img-retired", userdata);
	snprintf(q, sizeof q, "%s/ports/img", userdata);
	if (access(p, F_OK) == 0 || access(q, F_OK) != 0) {
		snprintf(p, sizeof p, "%s/ports/installed", userdata);
		n += gone(p, "the old PortMaster screens' install lists");
	}
	snprintf(p, sizeof p, "%s/ports/python.new", userdata);
	n += gone(p, "a Python unpack cut short");
	snprintf(p, sizeof p, "%s/ports/lib/libudev.so.1", userdata);
	n += gone(p, "a test copy; ChaOS ships its own for Weston ports");

	snprintf(p, sizeof p, "%s/Ports", roms);
	if ((d = opendir(p))) {
		while ((e = readdir(d))) {
			size_t l = strlen(e->d_name);

			if (strncmp(e->d_name, ".chaos-", 7) || l < 11 || strcmp(e->d_name + l - 4, ".zip"))
				continue;
			snprintf(q, sizeof q, "%s/%s", p, e->d_name);
			n += gone(q, "a port download cut short");
		}
		closedir(d);
	}

	/* unpacked runtimes whose image is gone, and unpacks cut short */
	snprintf(p, sizeof p, "%s/Ports/PortMaster/libs/.chaos", roms);
	if ((d = opendir(p))) {
		while ((e = readdir(d))) {
			size_t l = strlen(e->d_name);
			struct stat st;

			if (e->d_name[0] == '.') continue;
			snprintf(q, sizeof q, "%s/%s", p, e->d_name);
			if (stat(q, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
			if (l > 4 && !strcmp(e->d_name + l - 4, ".new")) {
				n += gone(q, "a runtime unpack cut short");
				continue;
			}
			{
				char img[1024];

				snprintf(img, sizeof img, "%s/Ports/PortMaster/libs/%s.squashfs", roms, e->d_name);
				if (access(img, F_OK) != 0) n += gone(q, "an unpacked runtime whose image is gone");
			}
		}
		closedir(d);
	}

	if (version && (f = fopen(mark, "w"))) {
		fprintf(f, "%s\n", version);
		fclose(f);
	}
	fprintf(stderr, "cleanup: %s, %d removed\n", version ? version : "?", n);
	return n;
}
