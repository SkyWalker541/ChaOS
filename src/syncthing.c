/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ChaOS: Syncthing - see syncthing.h */
#include "syncthing.h"
#include "net.h"
#include "rajson.h"
#include "../tools/tortos-update/sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const sth_folder_def STH_FOLDER[STH_FOLDERS] = {
	{ "chaos-saves", "ChaOS Saves", "Saves", "Saves" },
	{ "chaos-roms",  "ChaOS Games", "Games", "Roms"  },
	{ "chaos-bios",  "ChaOS BIOS",  "BIOS",  "Bios"  },
};

/* ---- reading Syncthing's answers ---------------------------------------- */

static jsv root_of(const char *json) { return js_root(json, strlen(json)); }

static bool member_str(jsv o, const char *k, char *out, size_t n)
{
	jsv v;

	out[0] = '\0';
	return js_member(o, k, &v) && js_str(v, out, n);
}

bool sth_parse_my_id(const char *json, char *out, size_t n)
{
	return json && member_str(root_of(json), "myID", out, n) && out[0];
}

int sth_parse_devices(const char *devices, const char *connections, const char *self,
                      sth_device *out, int max)
{
	jsv arr = root_of(devices ? devices : ""), it = { 0 }, e, conns = { 0 };
	bool have_conns = false;
	int n = 0;

	if (connections) {
		jsv r = root_of(connections);
		have_conns = js_member(r, "connections", &conns);
	}
	while (n < max && js_next(arr, &it, &e)) {
		sth_device *d = &out[n];
		jsv c, v;

		memset(d, 0, sizeof *d);
		if (!member_str(e, "deviceID", d->id, sizeof d->id) || !d->id[0]) continue;
		if (self && !strcmp(d->id, self)) continue;
		member_str(e, "name", d->name, sizeof d->name);
		if (!d->name[0]) sth_short_id(d->id, d->name, sizeof d->name);
		if (have_conns && js_member(conns, d->id, &c) && js_member(c, "connected", &v))
			d->connected = js_is_true(v);
		n++;
	}
	return n;
}

int sth_parse_pending(const char *json, sth_device *out, int max)
{
	jsv r = root_of(json ? json : ""), it = { 0 }, k, v;
	int n = 0;

	while (n < max && js_next_member(r, &it, &k, &v)) {
		sth_device *d = &out[n];
		size_t len = (size_t)(k.e - k.b);

		memset(d, 0, sizeof *d);
		if (len == 0 || len >= sizeof d->id) continue;
		memcpy(d->id, k.b, len);
		d->id[len] = '\0';
		member_str(v, "name", d->name, sizeof d->name);
		if (!d->name[0]) sth_short_id(d->id, d->name, sizeof d->name);
		n++;
	}
	return n;
}

void sth_parse_folders(const char *json, sth_info *in)
{
	jsv arr = root_of(json ? json : ""), it = { 0 }, e;
	int i;

	for (i = 0; i < STH_FOLDERS; i++) in->folder[i].on = in->folder[i].paused = false;
	while (js_next(arr, &it, &e)) {
		char id[64];
		jsv p;

		if (!member_str(e, "id", id, sizeof id)) continue;
		for (i = 0; i < STH_FOLDERS; i++) {
			if (strcmp(id, STH_FOLDER[i].id)) continue;
			in->folder[i].on = true;
			in->folder[i].paused = js_member(e, "paused", &p) && js_is_true(p);
		}
	}
}

void sth_parse_db_status(const char *json, sth_folder_state *f)
{
	jsv r = root_of(json ? json : ""), v;
	long gb = 0, nb = 0;

	member_str(r, "state", f->state, sizeof f->state);
	if (js_member(r, "globalBytes", &v)) gb = js_int(v);
	if (js_member(r, "needBytes", &v)) nb = js_int(v);
	f->errors = 0;
	if (js_member(r, "errors", &v)) f->errors += (int)js_int(v);
	if (js_member(r, "pullErrors", &v)) f->errors += (int)js_int(v);
	f->percent = gb > 0 ? (int)(100 - (nb * 100) / gb) : 100;
	if (f->percent < 0) f->percent = 0;
	if (f->percent == 100 && nb > 0) f->percent = 99;
}

/* JSON's string escapes, for a path or a name in a body */
static size_t put_js(char *out, size_t n, size_t o, const char *s)
{
	if (o < n) out[o] = '"';
	o++;
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\') { if (o + 1 < n) { out[o] = '\\'; out[o + 1] = (char)c; } o += 2; }
		else if (c < 0x20) { if (o + 6 < n) snprintf(out + o, n - o, "\\u%04x", c); o += 6; }
		else { if (o < n) out[o] = (char)c; o++; }
	}
	if (o < n) out[o] = '"';
	return o + 1;
}

int sth_folder_json(int which, const char *card, const char *const *ids, int nids,
                    char *out, size_t n)
{
	const sth_folder_def *f = &STH_FOLDER[which];
	char path[512];
	size_t o = 0;
	int i;

	if (which < 0 || which >= STH_FOLDERS || n == 0) return -1;
	snprintf(path, sizeof path, "%s/%s", card, f->sub);
	o += (size_t)snprintf(out + o, o < n ? n - o : 0, "{\"id\":\"%s\",\"label\":", f->id);
	o = put_js(out, n, o, f->label);
	o += (size_t)snprintf(out + o, o < n ? n - o : 0, ",\"path\":");
	o = put_js(out, n, o, path);
	/* The card is exFAT: no permissions to keep, and its clock is FAT's
	 * two seconds at worst */
	o += (size_t)snprintf(out + o, o < n ? n - o : 0,
	                      ",\"type\":\"sendreceive\",\"ignorePerms\":true,"
	                      "\"fsWatcherEnabled\":true,\"modTimeWindowS\":2,\"paused\":false,"
	                      "\"devices\":[");
	for (i = 0; i < nids; i++) {
		o += (size_t)snprintf(out + o, o < n ? n - o : 0, "%s{\"deviceID\":", i ? "," : "");
		o = put_js(out, n, o, ids[i]);
		o += (size_t)snprintf(out + o, o < n ? n - o : 0, "}");
	}
	o += (size_t)snprintf(out + o, o < n ? n - o : 0, "]}");
	if (o >= n) { out[n - 1] = '\0'; return -1; }
	out[o] = '\0';
	return (int)o;
}

void sth_short_id(const char *id, char *out, size_t n)
{
	const char *dash = strchr(id, '-');
	size_t len = dash ? (size_t)(dash - id) : strlen(id);

	if (len >= n) len = n - 1;
	memcpy(out, id, len);
	out[len] = '\0';
}

/* ---- the process ---------------------------------------------------------- */

static char g_home[400], g_card[400], g_bin[480], g_st[480], g_why[160];
static pid_t g_pid = -1;
static bool g_held, g_stopping;
static char g_key[48], g_pass[24];
static void ended(int status);
static void probe(void);
static void to_log(const char *name);

void sth_init(const char *userdata, const char *card)
{
	snprintf(g_home, sizeof g_home, "%s/syncthing", userdata);
	snprintf(g_card, sizeof g_card, "%s", card);
	snprintf(g_bin, sizeof g_bin, "%s/syncthing", g_home);
	snprintf(g_st, sizeof g_st, "%s/home", g_home);
}

bool sth_installed(void) { return g_bin[0] && access(g_bin, X_OK) == 0; }

bool sth_running(void)
{
	int status;

	if (g_pid <= 0) return false;
	if (waitpid(g_pid, &status, WNOHANG) == g_pid) {
		g_pid = -1;
		if (!g_stopping) ended(status);
		return false;
	}
	return kill(g_pid, 0) == 0;
}

const char *sth_problem(void) { return g_why; }

static void fail(const char *why)
{
	snprintf(g_why, sizeof g_why, "%s", why);
	fprintf(stderr, "syncthing: %s\n", why);
}

/* A secret of `n` letters and digits, from the kernel's randomness */
static void secret(char *out, size_t n)
{
	static const char abc[] = "abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
	unsigned char r[64];
	int fd = open("/dev/urandom", O_RDONLY);
	size_t i, got = 0;

	if (fd >= 0) { ssize_t k = read(fd, r, sizeof r); got = k > 0 ? (size_t)k : 0; close(fd); }
	for (i = 0; i + 1 < n; i++)
		out[i] = abc[(i < got ? r[i] : (unsigned char)rand()) % (sizeof abc - 1)];
	out[n - 1] = '\0';
}

/* A small file of ours in the Syncthing folder: read, or made */
static void keep_secret(const char *name, char *out, size_t n, size_t len)
{
	char p[520];
	FILE *f;

	snprintf(p, sizeof p, "%s/%s", g_home, name);
	if ((f = fopen(p, "r"))) {
		if (fgets(out, (int)n, f)) out[strcspn(out, "\r\n")] = '\0';
		fclose(f);
		if (strlen(out) >= 8) return;
	}
	secret(out, len + 1 < n ? len + 1 : n);
	{
		int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0600);

		if (fd >= 0) {
			if (write(fd, out, strlen(out)) < 0) {}
			close(fd);
		}
	}
}

const char *sth_password(void)
{
	if (!g_pass[0] && g_home[0]) keep_secret("gui-password", g_pass, sizeof g_pass, 10);
	return g_pass;
}

/* ---- fetching ------------------------------------------------------------- */

static pid_t g_get = -1;
static sth_get_state g_get_st;
static char g_tar[520], g_stage[520];

static void remove_tree(const char *path)
{
	pid_t pid = fork();

	if (pid == 0) {
		execl("/bin/rm", "rm", "-rf", path, (char *)NULL);
		_exit(127);
	}
	if (pid > 0) waitpid(pid, NULL, 0);
}

bool sth_get_begin(void)
{
	g_why[0] = '\0';
	mkdir(g_home, 0755);
	snprintf(g_tar, sizeof g_tar, "%s/.download.tar.gz", g_home);
	snprintf(g_stage, sizeof g_stage, "%s/.unpack", g_home);
	unlink(g_tar);
	g_get = net_get_spawn(STH_URL, g_tar, 1800);
	if (g_get <= 0) { fail("Couldn't start the download"); return false; }
	g_get_st = STH_GET_DOWNLOAD;
	return true;
}

long sth_get_bytes(void)
{
	struct stat st;

	return stat(g_tar, &st) == 0 ? (long)st.st_size : 0;
}

static bool hash_ok(const char *path)
{
	sha256 h;
	uint8_t d[32];
	char hex[65], buf[65536];
	FILE *f = fopen(path, "rb");
	size_t got;

	if (!f) return false;
	sha256_start(&h);
	while ((got = fread(buf, 1, sizeof buf, f)) > 0) sha256_add(&h, buf, got);
	fclose(f);
	sha256_finish(&h, d, hex);
	return !strcmp(hex, STH_SHA256);
}

sth_get_state sth_get_step(void)
{
	int status;

	if (g_get_st == STH_GET_DONE || g_get_st == STH_GET_FAILED) return g_get_st;
	if (g_get > 0 && waitpid(g_get, &status, WNOHANG) == 0) return g_get_st;
	if (g_get_st == STH_GET_DOWNLOAD) {
		net_spawn_done(g_get);
		g_get = -1;
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			unlink(g_tar);
			fail("The download didn't finish:\ncheck Wi-Fi and try again");
			return g_get_st = STH_GET_FAILED;
		}
		if (!hash_ok(g_tar)) {
			unlink(g_tar);
			fail("The download wasn't Syncthing's own:\nits checksum is wrong");
			return g_get_st = STH_GET_FAILED;
		}
		remove_tree(g_stage);
		mkdir(g_stage, 0755);
		g_get = fork();
		if (g_get == 0) {
			int devnull = open("/dev/null", O_RDWR);

			if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); close(devnull); }
			execl("/bin/tar", "tar", "-xzf", g_tar, "-C", g_stage, (char *)NULL);
			_exit(127);
		}
		if (g_get < 0) { fail("Couldn't unpack it"); return g_get_st = STH_GET_FAILED; }
		return g_get_st = STH_GET_UNPACK;
	}
	/* unpacked */
	g_get = -1;
	{
		char from[700], tmp[520];

		snprintf(from, sizeof from, "%s/syncthing-linux-arm64-%s/syncthing", g_stage, STH_VERSION);
		snprintf(tmp, sizeof tmp, "%s.new", g_bin);
		unlink(tmp);
		/* chmod's answer not asked: on the card's exFAT every file is
		 * executable already, and a mode change may be refused */
		bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0 && rename(from, tmp) == 0;

		if (ok && chmod(tmp, 0755) != 0) {}
		if (!ok || rename(tmp, g_bin) != 0) {
			remove_tree(g_stage);
			unlink(g_tar);
			fail("Couldn't unpack it:\nis the card full?");
			return g_get_st = STH_GET_FAILED;
		}
	}
	remove_tree(g_stage);
	unlink(g_tar);
	fprintf(stderr, "syncthing: %s installed\n", STH_VERSION);
	return g_get_st = STH_GET_DONE;
}

void sth_get_cancel(void)
{
	if (g_get > 0) {
		kill(g_get, SIGTERM);
		waitpid(g_get, NULL, 0);
		if (g_get_st == STH_GET_DOWNLOAD) net_spawn_done(g_get);
	}
	g_get = -1;
	g_get_st = STH_GET_FAILED;
	unlink(g_tar);
	remove_tree(g_stage);
}

/* ---- running -------------------------------------------------------------- */

/* In the child: stdin from /dev/null, stdout and stderr to a log of ours
 * in the Syncthing folder, started afresh */
static void to_log(const char *name)
{
	char p[560];
	int in = open("/dev/null", O_RDONLY), out;

	snprintf(p, sizeof p, "%s/%s", g_home, name);
	out = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (in >= 0) { dup2(in, 0); close(in); }
	if (out >= 0) { dup2(out, 1); dup2(out, 2); close(out); }
}

/* Syncthing ended by itself: why, as far as its own words say - the last
 * lines of serve.log into the launcher's log, and the last into the screen's */
static void ended(int status)
{
	char p[560], line[256], last[256] = "";
	FILE *f;
	int kept = 0;
	long sz;

	snprintf(p, sizeof p, "%s/serve.log", g_home);
	fprintf(stderr, "syncthing: ended by itself (%s %d); serve.log ends:\n",
	        WIFSIGNALED(status) ? "signal" : "exit",
	        WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status));
	if ((f = fopen(p, "r"))) {
		fseek(f, 0, SEEK_END);
		sz = ftell(f);
		if (sz > 4096) fseek(f, sz - 4096, SEEK_SET); else fseek(f, 0, SEEK_SET);
		while (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\r\n")] = '\0';
			if (!line[0]) continue;
			fprintf(stderr, "  | %s\n", line);
			snprintf(last, sizeof last, "%s", line);
			kept++;
		}
		fclose(f);
	}
	if (!kept) fprintf(stderr, "  (nothing)\n");
	{
		/* its words without the date and level, in two short lines that
		 * fit the screen's box */
		const char *m = last;
		const char *lv[] = { "ERR ", "WRN ", "INF ", "FATAL ", "panic: " };
		char a[40] = "", b[40] = "";
		size_t i, k;

		for (i = 0; i < sizeof lv / sizeof lv[0]; i++) {
			const char *h = strstr(last, lv[i]);
			if (h) { m = h + (i < 4 ? strlen(lv[i]) : 0); break; }
		}
		if (!*m) m = "see serve.log";
		k = strlen(m);
		if (k <= 30) snprintf(a, sizeof a, "%.30s", m);
		else {
			size_t cut = 30;
			while (cut > 15 && m[cut] != ' ') cut--;
			snprintf(a, sizeof a, "%.*s", (int)cut, m);
			snprintf(b, sizeof b, "%.30s", m + cut + (m[cut] == ' '));
		}
		snprintf(g_why, sizeof g_why, "Syncthing stopped:\n%s%s%s", a, b[0] ? "\n" : "", b);
	}
}

/* syncthing generate: the device's keys and a config with the web page's
 * user and password. Once; a second or so for the keys. */
static bool generate(void)
{
	char cfg[520], user[64], pass[64], home[520];
	pid_t pid;
	int status = -1;

	snprintf(cfg, sizeof cfg, "%s/config.xml", g_st);
	if (access(cfg, R_OK) == 0) return true;
	mkdir(g_st, 0700);
	snprintf(home, sizeof home, "--home=%s", g_st);
	snprintf(user, sizeof user, "--gui-user=%s", STH_GUI_USER);
	snprintf(pass, sizeof pass, "--gui-password=%s", sth_password());
	pid = fork();
	if (pid == 0) {
		to_log("generate.log");
		setenv("STNODEFAULTFOLDER", "1", 1);
		execl(g_bin, "syncthing", "generate", home, user, pass, "--no-port-probing", (char *)NULL);
		_exit(127);
	}
	if (pid > 0) waitpid(pid, &status, 0);
	if (access(cfg, R_OK) != 0) { fail("Syncthing couldn't make its keys"); return false; }
	return true;
}

/* The loopback interface up. plastron's inittab never runs rcS, so nothing
 * does `ifup lo`: lo stays down, and 127.0.0.1 - where Syncthing's API is -
 * goes nowhere (a connect to it times out). Up, the kernel gives it
 * 127.0.0.1 itself. */
static void lo_up(void)
{
	struct ifreq ifr;
	int s = socket(AF_INET, SOCK_DGRAM, 0);

	if (s < 0) return;
	memset(&ifr, 0, sizeof ifr);
	snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "lo");
	if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0 && !(ifr.ifr_flags & IFF_UP)) {
		ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
		if (ioctl(s, SIOCSIFFLAGS, &ifr) == 0) fprintf(stderr, "syncthing: brought lo up\n");
		else fprintf(stderr, "syncthing: couldn't bring lo up: %s\n", strerror(errno));
	}
	close(s);
}

bool sth_start(void)
{
	char home[520], gui[64], log[560];

	if (sth_running()) return true;
	g_why[0] = '\0';
	if (!sth_installed()) { fail("Syncthing isn't on the card"); return false; }
	lo_up();
	mkdir(g_home, 0755);
	if (!generate()) return false;
	keep_secret("apikey", g_key, sizeof g_key, 32);
	snprintf(home, sizeof home, "--home=%s", g_st);
	snprintf(gui, sizeof gui, "--gui-address=0.0.0.0:%d", STH_GUI_PORT);
	snprintf(log, sizeof log, "--log-file=%s/syncthing.log", g_home);
	g_held = false;
	g_pid = fork();
	if (g_pid < 0) { fail("Couldn't start Syncthing"); return false; }
	if (g_pid == 0) {
		setpgid(0, 0);
		/* What it says before its own log is open - a flag it doesn't
		 * know, a folder it can't write - goes here */
		to_log("serve.log");
		/* On the shelf, after the launcher and the music: low, and on two
		 * of the four cores */
		if (setpriority(PRIO_PROCESS, 0, 15) != 0) {}
		setenv("STGUIAPIKEY", g_key, 1);
		setenv("STNODEFAULTFOLDER", "1", 1);
		setenv("GOMAXPROCS", "2", 1);
		/* The Brick has no certificate store of its own: ChaOS's, for
		 * the relays and discovery servers Syncthing reaches over https */
		{
			char ca[520];

			snprintf(ca, sizeof ca, "%s/ChaOS/cacert.pem", g_card);
			if (access(ca, R_OK) == 0) setenv("SSL_CERT_FILE", ca, 1);
		}
		/* Go's garbage collector, kept to the Brick's memory */
		setenv("GOMEMLIMIT", "160MiB", 1);
		execl(g_bin, "syncthing", "serve", home, gui, log, "--log-max-size=1048576",
		      "--log-max-old-files=1", "--no-browser", "--no-restart", "--no-upgrade",
		      (char *)NULL);
		_exit(127);
	}
	setpgid(g_pid, g_pid);
	fprintf(stderr, "syncthing: started, pid %d\n", (int)g_pid);
	return true;
}

void sth_stop(void)
{
	int i;

	if (g_pid <= 0) return;
	g_stopping = true;
	if (g_held) kill(-g_pid, SIGCONT);
	kill(-g_pid, SIGTERM);
	/* Its database closed properly: up to three seconds */
	for (i = 0; i < 60; i++) {
		if (waitpid(g_pid, NULL, WNOHANG) == g_pid) { g_pid = -1; g_stopping = false; return; }
		usleep(50000);
	}
	kill(-g_pid, SIGKILL);
	waitpid(g_pid, NULL, 0);
	g_pid = -1;
	g_held = g_stopping = false;
	fprintf(stderr, "syncthing: stopped\n");
}

void sth_hold(bool hold)
{
	if (g_pid <= 0 || hold == g_held) return;
	if (kill(-g_pid, hold ? SIGSTOP : SIGCONT) == 0) g_held = hold;
	fprintf(stderr, "syncthing: %s\n", hold ? "stopped for the game" : "going on");
}

/* ---- the REST API --------------------------------------------------------- */

static void api_url(char *out, size_t n, const char *path)
{
	snprintf(out, n, "http://127.0.0.1:%d%s", STH_GUI_PORT, path);
}

static bool call(const char *method, const char *path, const char *json, char *out, size_t n)
{
	char url[256], tmp[256];
	int http = 0;
	net_req r = { 0 };

	if (!sth_running() || g_held) return false;
	if (!g_key[0]) keep_secret("apikey", g_key, sizeof g_key, 32);
	api_url(url, sizeof url, path);
	r.method = method;
	r.url = url;
	r.bearer = g_key;
	r.json = json;
	r.timeout_s = 5;
	if (!out) { out = tmp; n = sizeof tmp; }
	net_request(&r, out, n, &http);
	if (http < 200 || http >= 300) {
		fprintf(stderr, "syncthing: %s %s: HTTP %d\n", method ? method : "GET", path, http);
		return false;
	}
	return true;
}

/* The poll: every request at once, each to its own file */
enum { Q_STATUS, Q_DEVICES, Q_CONNS, Q_PENDING, Q_FOLDERS, Q_DB0, Q_N = Q_DB0 + STH_FOLDERS };
static pid_t g_q[Q_N];
static char g_qf[Q_N][64];
static bool g_polling;

bool sth_poll_begin(void)
{
	static const char *paths[Q_DB0] = {
		"/rest/system/status", "/rest/config/devices", "/rest/system/connections",
		"/rest/cluster/pending/devices", "/rest/config/folders",
	};
	int i;

	if (g_polling || !sth_running() || g_held) return false;
	if (!g_key[0]) keep_secret("apikey", g_key, sizeof g_key, 32);
	for (i = 0; i < Q_N; i++) {
		char url[256], p[128];
		net_req r = { 0 };

		if (i < Q_DB0) snprintf(p, sizeof p, "%s", paths[i]);
		else snprintf(p, sizeof p, "/rest/db/status?folder=%s", STH_FOLDER[i - Q_DB0].id);
		api_url(url, sizeof url, p);
		snprintf(g_qf[i], sizeof g_qf[i], "/tmp/chaos-sth-%d.json", i);
		unlink(g_qf[i]);
		r.url = url;
		r.bearer = g_key;
		r.timeout_s = 5;
		g_q[i] = net_request_spawn(&r, g_qf[i]);
	}
	g_polling = true;
	return true;
}

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	long len;

	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len < 0 || len > 4 * 1024 * 1024 || !(buf = malloc((size_t)len + 1))) { fclose(f); return NULL; }
	len = (long)fread(buf, 1, (size_t)len, f);
	buf[len] = '\0';
	fclose(f);
	return buf;
}

bool sth_poll_step(sth_info *out)
{
	char *body[Q_N];
	int i;

	if (!g_polling) return false;
	for (i = 0; i < Q_N; i++) {
		if (g_q[i] <= 0) continue;
		if (waitpid(g_q[i], NULL, WNOHANG) == 0) return false;
		net_spawn_done(g_q[i]);
		g_q[i] = -1;
	}
	g_polling = false;
	for (i = 0; i < Q_N; i++) { body[i] = slurp(g_qf[i]); unlink(g_qf[i]); }
	memset(out, 0, sizeof *out);
	out->ok = body[Q_STATUS] && sth_parse_my_id(body[Q_STATUS], out->my_id, sizeof out->my_id);
	{
		/* Said once, then every half a minute while it goes on */
		static unsigned misses;

		if (out->ok) misses = 0;
		else if (misses++ % 15 == 0) {
			fprintf(stderr, "syncthing: the API at 127.0.0.1:%d didn't answer (%u times); "
			        "status body %s\n", STH_GUI_PORT, misses,
			        !body[Q_STATUS] ? "missing" : !body[Q_STATUS][0] ? "empty" : "unreadable");
			if (body[Q_STATUS] && body[Q_STATUS][0])
				fprintf(stderr, "  | %.160s\n", body[Q_STATUS]);
			probe();
		}
	}
	if (out->ok) {
		out->ndev = sth_parse_devices(body[Q_DEVICES], body[Q_CONNS], out->my_id,
		                              out->dev, STH_MAX_DEV);
		out->npend = sth_parse_pending(body[Q_PENDING], out->pend, STH_MAX_DEV);
		sth_parse_folders(body[Q_FOLDERS], out);
		for (i = 0; i < STH_FOLDERS; i++)
			if (out->folder[i].on) sth_parse_db_status(body[Q_DB0 + i], &out->folder[i]);
	}
	for (i = 0; i < Q_N; i++) free(body[i]);
	return true;
}

/* Why the API isn't answering, in curl's own words: the same request, by
 * hand, its errors and status code into the launcher's log (the key not) */
static void probe(void)
{
	char cmd[512], line[256], url[128];
	FILE *p;

	api_url(url, sizeof url, "/rest/system/status");
	snprintf(cmd, sizeof cmd,
	         "curl -sS -o /dev/null -m 3 -w 'http %%{http_code}, connect %%{time_connect}s\n' "
	         "-H \"Authorization: Bearer $(cat '%s/apikey')\" '%s' 2>&1; "
	         "echo \"curl exit $?\"; grep -n '127.0.0.1\\|localhost' /etc/hosts 2>&1 | head -3; "
	         "env | grep -i '_proxy' | sed 's/=.*/=(set)/'",
	         g_home, url);
	if (!(p = popen(cmd, "r"))) return;
	while (fgets(line, sizeof line, p)) fprintf(stderr, "  probe| %s", line);
	pclose(p);
}

/* Every paired device's ID, this Brick's left out */
static int device_ids(char ids[][STH_ID_LEN], int max)
{
	char buf[16384], self[STH_ID_LEN] = "", st[2048];
	sth_device d[STH_MAX_DEV];
	int n, i;

	if (call(NULL, "/rest/system/status", NULL, st, sizeof st)) sth_parse_my_id(st, self, sizeof self);
	if (!call(NULL, "/rest/config/devices", NULL, buf, sizeof buf)) return -1;
	n = sth_parse_devices(buf, NULL, self, d, STH_MAX_DEV);
	for (i = 0; i < n && i < max; i++) snprintf(ids[i], STH_ID_LEN, "%s", d[i].id);
	return n < max ? n : max;
}

/* Each of ChaOS's folders that's on, shared again with every paired device */
static void reshare(void)
{
	char ids[STH_MAX_DEV][STH_ID_LEN], buf[16384];
	const char *p[STH_MAX_DEV];
	sth_info in;
	int n = device_ids(ids, STH_MAX_DEV), i;

	if (n < 0 || !call(NULL, "/rest/config/folders", NULL, buf, sizeof buf)) return;
	sth_parse_folders(buf, &in);
	for (i = 0; i < n; i++) p[i] = ids[i];
	for (i = 0; i < STH_FOLDERS; i++) {
		char js[4096], path[96];

		if (!in.folder[i].on) continue;
		snprintf(path, sizeof path, "/rest/config/folders/%s", STH_FOLDER[i].id);
		if (sth_folder_json(i, g_card, p, n, js, sizeof js) > 0) call("PUT", path, js, NULL, 0);
	}
}

bool sth_set_name(const char *name)
{
	char st[2048], id[STH_ID_LEN], path[128], js[256];
	size_t o = 0;

	if (!call(NULL, "/rest/system/status", NULL, st, sizeof st) || !sth_parse_my_id(st, id, sizeof id))
		return false;
	snprintf(path, sizeof path, "/rest/config/devices/%s", id);
	o = (size_t)snprintf(js, sizeof js, "{\"name\":");
	o = put_js(js, sizeof js, o, name);
	snprintf(js + o, sizeof js - o, "}");
	return call("PATCH", path, js, NULL, 0);
}

bool sth_accept(const char *id, const char *name)
{
	char js[512];
	size_t o;

	o = (size_t)snprintf(js, sizeof js, "{\"deviceID\":");
	o = put_js(js, sizeof js, o, id);
	o += (size_t)snprintf(js + o, sizeof js - o, ",\"name\":");
	o = put_js(js, sizeof js, o, name);
	snprintf(js + o, sizeof js - o, "}");
	if (!call("POST", "/rest/config/devices", js, NULL, 0)) return false;
	reshare();
	return true;
}

bool sth_ignore(const char *id)
{
	char path[160];

	snprintf(path, sizeof path, "/rest/cluster/pending/devices?device=%s", id);
	return call("DELETE", path, NULL, NULL, 0);
}

bool sth_forget(const char *id)
{
	char path[160];

	snprintf(path, sizeof path, "/rest/config/devices/%s", id);
	if (!call("DELETE", path, NULL, NULL, 0)) return false;
	reshare();
	return true;
}

bool sth_folder_set(int which, bool on)
{
	char path[96];

	if (which < 0 || which >= STH_FOLDERS) return false;
	snprintf(path, sizeof path, "/rest/config/folders/%s", STH_FOLDER[which].id);
	if (!on) return call("DELETE", path, NULL, NULL, 0);
	{
		char ids[STH_MAX_DEV][STH_ID_LEN], js[4096], dir[520];
		const char *p[STH_MAX_DEV];
		int n = device_ids(ids, STH_MAX_DEV), i;

		if (n < 0) return false;
		for (i = 0; i < n; i++) p[i] = ids[i];
		snprintf(dir, sizeof dir, "%s/%s", g_card, STH_FOLDER[which].sub);
		mkdir(dir, 0755);
		if (sth_folder_json(which, g_card, p, n, js, sizeof js) < 0) return false;
		return call("PUT", path, js, NULL, 0);
	}
}
