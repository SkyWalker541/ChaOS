/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: ports and PortMaster - see ports.h */
#include "ports.h"
#include "net.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void ports_pm_dir(const char *roms, char *out, size_t n)
{
	snprintf(out, n, "%s/Ports/PortMaster", roms);
}

void ports_pm_kept_dir(const char *roms, char *out, size_t n)
{
	snprintf(out, n, "%s/Ports/.PortMaster.kept", roms);
}

bool ports_pm_ready(const char *roms)
{
	char p[512];

	snprintf(p, sizeof p, "%s/Ports/PortMaster/control.txt", roms);
	return access(p, R_OK) == 0;
}

bool ports_script_ok(const char *path, char *why, size_t n)
{
	char head[64] = { 0 };
	int fd = open(path, O_RDONLY);
	ssize_t got;

	if (fd < 0) { snprintf(why, n, "Can't read this port"); return false; }
	got = read(fd, head, sizeof head - 1);
	close(fd);
	if (got <= 0) { snprintf(why, n, "This port's script is empty"); return false; }
	/* A #! line, or at least something that reads as shell: PortMaster's
	 * scripts all start #!/bin/bash, and a few hand-made ones start with a
	 * comment */
	if (head[0] != '#' && strncmp(head, "export", 6) && strncmp(head, "GAMEDIR", 7)) {
		snprintf(why, n, "Not a port's script");
		return false;
	}
	return true;
}

/* ---- fetching ---------------------------------------------------------- */

static ports_setup_state g_state = PS_IDLE;
static pid_t g_pid = -1;
static char g_zip[512], g_stage[512], g_pm[512], g_roms[400];
static char g_why[96];
static const char *g_url = PORTS_PM_URL;

void ports_setup_url(const char *url) { g_url = url ? url : PORTS_PM_URL; }

static void fail(const char *why)
{
	snprintf(g_why, sizeof g_why, "%s", why);
	g_state = PS_FAILED;
}

const char *ports_setup_problem(void) { return g_why; }

long ports_setup_bytes(void)
{
	struct stat st;

	return stat(g_zip, &st) == 0 ? (long)st.st_size : 0;
}

bool ports_setup_begin(const char *roms)
{
	char dir[400];

	g_why[0] = '\0';
	snprintf(g_roms, sizeof g_roms, "%s", roms);
	snprintf(dir, sizeof dir, "%s/Ports", roms);
	mkdir(dir, 0755);
	snprintf(g_zip, sizeof g_zip, "%s/.PortMaster.zip", dir);
	snprintf(g_stage, sizeof g_stage, "%s/.PortMaster.new", dir);
	ports_pm_dir(roms, g_pm, sizeof g_pm);
	unlink(g_zip);
	g_pid = net_get_spawn(g_url, g_zip, 1800);
	if (g_pid <= 0) { fail("Couldn't start the download"); return false; }
	g_state = PS_DOWNLOAD;
	return true;
}

/* rm -rf, for the staging folder: a child, as nothing else here walks trees */
static void remove_tree(const char *path)
{
	pid_t pid = fork();

	if (pid == 0) {
		execl("/bin/rm", "rm", "-rf", path, (char *)NULL);
		_exit(127);
	}
	if (pid > 0) waitpid(pid, NULL, 0);
}

static bool is_zip(const char *path)
{
	unsigned char m[4] = { 0 };
	int fd = open(path, O_RDONLY);
	bool ok;

	if (fd < 0) return false;
	ok = read(fd, m, 4) == 4 && m[0] == 'P' && m[1] == 'K' && m[2] == 3 && m[3] == 4;
	close(fd);
	return ok;
}

static bool unpack_begin(void)
{
	const char *unzip = access("/usr/bin/unzip", X_OK) == 0 ? "/usr/bin/unzip" : "/bin/unzip";

	remove_tree(g_stage);
	mkdir(g_stage, 0755);
	g_pid = fork();
	if (g_pid < 0) return false;
	if (g_pid == 0) {
		int devnull = open("/dev/null", O_RDWR);

		setpgid(0, 0);
		if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); close(devnull); }
		execl(unzip, "unzip", "-oq", g_zip, "-d", g_stage, (char *)NULL);
		_exit(127);
	}
	setpgid(g_pid, g_pid);
	return true;
}

/* The unpacked PortMaster into place. A folder already there without a
 * control.txt is a broken or half-copied one: kept, renamed, not deleted -
 * it may have someone's settings in it. */
/* What PortMaster keeps of its own beside its code: the runtimes it has
 * fetched (libs/: some are hundreds of MB, and ChaOS's unpacked copies are
 * in libs/.chaos) and its settings (config/). An update carries them over. */
static const char *const g_keep[] = { "libs", "config", NULL };

static void carry_over(const char *old)
{
	int i;

	for (i = 0; g_keep[i]; i++) {
		char from[700], to[700];
		struct stat st;

		snprintf(from, sizeof from, "%s/%s", old, g_keep[i]);
		snprintf(to, sizeof to, "%s/%s", g_pm, g_keep[i]);
		if (stat(from, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		/* The new release's own (an empty libs/, a default config):
		 * the old one, with what was fetched and set, wins */
		remove_tree(to);
		rename(from, to);
	}
}

static bool unpack_finish(void)
{
	char from[600], old[600] = "";
	struct stat st;
	bool update = false;

	snprintf(from, sizeof from, "%s/PortMaster", g_stage);
	if (stat(from, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
	if (stat(g_pm, &st) == 0) {
		char ctl[620];

		snprintf(ctl, sizeof ctl, "%s/control.txt", g_pm);
		update = access(ctl, R_OK) == 0;
		/* A name nothing has: two in one second would collide */
		{
			int k = 0;
			struct stat o;

			do snprintf(old, sizeof old, k ? "%s.old-%ld-%d" : "%s.old-%ld", g_pm,
			            (long)time(NULL), k);
			while (stat(old, &o) == 0 && ++k < 100);
		}
		if (rename(g_pm, old) != 0) return false;
	}
	if (rename(from, g_pm) != 0) {
		if (old[0]) rename(old, g_pm);
		return false;
	}
	/* A working PortMaster updated: what it had fetched and set comes
	 * along, and the old one goes. A broken one stays, renamed. */
	if (update) {
		carry_over(old);
		remove_tree(old);
	} else {
		/* Installed again after an uninstall that kept its settings
		 * (ports_pm_kept_dir): they come back */
		char kept[600];
		struct stat ks;

		ports_pm_kept_dir(g_roms, kept, sizeof kept);
		if (stat(kept, &ks) == 0 && S_ISDIR(ks.st_mode)) {
			carry_over(kept);
			remove_tree(kept);
		}
	}
	remove_tree(g_stage);
	unlink(g_zip);
	return true;
}

ports_setup_state ports_setup_step(void)
{
	int st = 0;
	pid_t r;

	if (g_state != PS_DOWNLOAD && g_state != PS_UNPACK) return g_state;
	r = waitpid(g_pid, &st, WNOHANG);
	if (r == 0) return g_state;
	if (r < 0 && errno == EINTR) return g_state;

	if (g_state == PS_DOWNLOAD) {
		net_spawn_done(g_pid);
		g_pid = -1;
		if (r < 0) { fail("The download stopped"); return g_state; }
		if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
			unlink(g_zip);
			fail("Couldn't download PortMaster");
			return g_state;
		}
		if (!is_zip(g_zip)) {
			unlink(g_zip);
			fail("The download wasn't PortMaster");
			return g_state;
		}
		if (!unpack_begin()) { fail("Couldn't unpack PortMaster"); return g_state; }
		g_state = PS_UNPACK;
		return g_state;
	}
	/* PS_UNPACK */
	g_pid = -1;
	if (r < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0 || !unpack_finish()) {
		remove_tree(g_stage);
		unlink(g_zip);
		fail("Couldn't unpack PortMaster");
		return g_state;
	}
	g_state = PS_DONE;
	return g_state;
}

void ports_setup_cancel(void)
{
	if (g_state == PS_DOWNLOAD && g_pid > 0) {
		kill(g_pid, SIGTERM);
		waitpid(g_pid, NULL, 0);
		net_spawn_done(g_pid);
		unlink(g_zip);
	} else if (g_state == PS_UNPACK && g_pid > 0) {
		kill(-g_pid, SIGTERM);
		waitpid(g_pid, NULL, 0);
		remove_tree(g_stage);
		unlink(g_zip);
	}
	g_pid = -1;
	g_state = PS_IDLE;
}
