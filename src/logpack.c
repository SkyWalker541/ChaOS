/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See logpack.h. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* strcasestr */
#endif
#include "logpack.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void mask_shapes(char *line, size_t cap);

void logpack_mask_word(const char *word, char *out, size_t n)
{
	size_t len = strlen(word);

	if (len < 3) { snprintf(out, n, "***"); return; }
	snprintf(out, n, "%c...%c", word[0], word[len - 1]);
}

/* Replace `len` bytes at `at` in `line` with `with`, within `cap`. */
static void splice(char *line, size_t cap, char *at, size_t len, const char *with)
{
	size_t wl = strlen(with), tail = strlen(at + len);

	if ((size_t)(at - line) + wl + tail + 1 > cap) {
		/* No room for the rest: keep what fits, masked, and cut the tail. */
		if ((size_t)(at - line) + wl + 1 > cap) { *at = '\0'; return; }
		tail = cap - (size_t)(at - line) - wl - 1;
	}
	memmove(at + wl, at + len, tail);
	memcpy(at, with, wl);
	at[wl + tail] = '\0';
}

/* Six hex pairs joined by `sep`, starting at `p`. */
static bool mac_at(const char *p, char sep)
{
	int k;

	for (k = 0; k < 6; k++) {
		if (!isxdigit((unsigned char)p[k * 3]) || !isxdigit((unsigned char)p[k * 3 + 1]))
			return false;
		if (k < 5 && p[k * 3 + 2] != sep) return false;
	}
	/* Not the middle of a longer run of hex, which would be something else. */
	return !isxdigit((unsigned char)p[17]);
}

void logpack_mask_line(char *line, size_t cap, const char *const *secrets, int n)
{
	char masked[128], *p;
	int order[64], m = 0, a, b;

	/* Longest first, so a network named "Home" cannot eat into one named
	 * "Home 5G" before that one is found. */
	for (a = 0; a < n && m < 64; a++)
		if (secrets[a] && strlen(secrets[a]) >= 3) order[m++] = a;
	for (a = 1; a < m; a++)
		for (b = a; b > 0 && strlen(secrets[order[b]]) > strlen(secrets[order[b - 1]]); b--) {
			int t = order[b];

			order[b] = order[b - 1];
			order[b - 1] = t;
		}
	for (a = 0; a < m; a++) {
		const char *s = secrets[order[a]];
		size_t sl = strlen(s);

		logpack_mask_word(s, masked, sizeof masked);
		for (p = strstr(line, s); p; p = strstr(p + strlen(masked), s))
			splice(line, cap, p, sl, masked);
	}

	for (p = line; strlen(p) >= 17; p++) {
		char sep;
		int k;

		if (!mac_at(p, ':') && !mac_at(p, '_')) continue;
		/* Not the tail of a longer run of hex either. */
		if (p > line && isxdigit((unsigned char)p[-1])) continue;
		sep = p[2];
		for (k = 0; k < 5; k++) {
			p[k * 3] = 'X';
			p[k * 3 + 1] = 'X';
			p[k * 3 + 2] = sep;
		}
		p += 16;
	}
	mask_shapes(line, cap);
}

/* ---- what is masked by its shape ------------------------------------------
 *
 * The names above are the ones the device knows. These are found by what they
 * look like, so one in a line nobody thought of - a port's own log, Syncthing's,
 * a server's answer - is caught too (2026-11-23: logs are for finding faults,
 * and nothing in one should identify its owner or open an account). Each is
 * replaced by what it was, in brackets, so the line still reads. */

static bool word_ch(char c) { return isalnum((unsigned char)c) || c == '_' || c == '-'; }

/* user:password@ in an address */
static void mask_userinfo(char *line, size_t cap)
{
	char *p = line;

	while ((p = strstr(p, "://"))) {
		char *h = p + 3, *q = h;

		while (*q && !strchr("/?#@ \t\r\n\"'<>", *q)) q++;
		if (*q == '@' && q > h) {
			splice(line, cap, h, (size_t)(q - h), "(hidden)");
			q = h + 8;
		}
		p = q;
	}
}

/* An address's parameters that carry a key, a token, a password or an
 * account: their values. RetroAchievements' API names them u, p, t and y. */
static void mask_query(char *line, size_t cap)
{
	static const char *const names[] = {
		"token", "access_token", "refresh_token", "id_token", "key", "apikey", "api_key",
		"password", "passwd", "pass", "pwd", "secret", "auth", "signature", "sig", "code",
		"user", "username", "login", "email", "u", "p", "t", "y", NULL
	};
	char *p;

	for (p = line; *p; p++) {
		char *n, *v, *e;
		size_t nl;
		int k;

		if (*p != '?' && *p != '&') continue;
		n = p + 1;
		for (v = n; isalnum((unsigned char)*v) || *v == '_'; v++) ;
		if (*v != '=' || v == n) continue;
		nl = (size_t)(v - n);
		for (k = 0; names[k]; k++)
			if (strlen(names[k]) == nl && !strncasecmp(n, names[k], nl)) break;
		if (!names[k]) continue;
		v++;
		for (e = v; *e && !strchr("&# \t\r\n\"'<>", *e); e++) ;
		if (e > v) splice(line, cap, v, (size_t)(e - v), "(hidden)");
	}
}

/* "Bearer x", "Authorization: x", "password=x", "token: x" in prose */
static void mask_labelled(char *line, size_t cap)
{
	static const char *const labels[] = {
		"bearer ", "authorization:", "authorization=", "password:", "password=",
		"passwd=", "token:", "token=", "api_key=", "apikey=", "api-key:", "secret=", "secret:",
		NULL
	};
	int k;

	for (k = 0; labels[k]; k++) {
		char *p = line;
		size_t ll = strlen(labels[k]);

		while ((p = strcasestr(p, labels[k]))) {
			char *v = p + ll, *e;

			/* the end of a longer word ("csrf_token=") is still a token's
			 * label; the middle of one ("tokens:") is not */
			while (*v == ' ' || *v == '"') v++;
			/* "Authorization: Bearer x": the scheme stays, x goes */
			if (!strncasecmp(v, "bearer ", 7)) v += 7;
			else if (!strncasecmp(v, "basic ", 6)) v += 6;
			for (e = v; *e && !strchr(" \t\r\n\"',;&)", *e); e++) ;
			if (e > v && strncmp(v, "(hidden)", 8))
				splice(line, cap, v, (size_t)(e - v), "(hidden)");
			p = v;
			if (!*p) break;
			p++;
		}
	}
}

static void mask_emails(char *line, size_t cap)
{
	char *at;

	for (at = strchr(line, '@'); at; at = strchr(at + 1, '@')) {
		char *a = at, *b = at + 1;
		bool dot = false;

		while (a > line && (isalnum((unsigned char)a[-1]) || strchr("._%+-", a[-1]))) a--;
		while (isalnum((unsigned char)*b) || *b == '.' || *b == '-') { if (*b == '.') dot = true; b++; }
		while (b > at + 1 && b[-1] == '.') b--;
		if (a == at || b == at + 1 || !dot) continue;
		splice(line, cap, a, (size_t)(b - a), "(email)");
		at = a + 6;
	}
}

/* Syncthing's device IDs: eight groups of seven, A-Z and 2-7, joined by - */
static void mask_syncthing_ids(char *line, size_t cap)
{
	char *p;

	for (p = line; strlen(p) >= 63; p++) {
		int g, i;
		bool ok = !(p > line && word_ch(p[-1]));

		for (g = 0; ok && g < 8; g++) {
			for (i = 0; ok && i < 7; i++) {
				char c = p[g * 8 + i];

				ok = (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7');
			}
			if (ok && g < 7) ok = p[g * 8 + 7] == '-';
		}
		if (ok && !word_ch(p[63])) splice(line, cap, p, 63, "(device ID)");
	}
}

/* An IPv4 address on the internet. One on the home network (10., 172.16-31.,
 * 192.168.), the device itself, link-local, carrier NAT and 0.x (a version
 * number, more likely: "0.9.8.10") stay, since they say how the Brick was
 * reached and nothing about who has it. */
static void mask_public_ips(char *line, size_t cap)
{
	char *p;

	for (p = line; *p; p++) {
		int o[4], k, len = 0;
		char *q = p;

		if (!isdigit((unsigned char)*p) || (p > line && (isalnum((unsigned char)p[-1]) || p[-1] == '.')))
			continue;
		for (k = 0; k < 4; k++) {
			int v = 0, d = 0;

			while (isdigit((unsigned char)*q) && d < 4) { v = v * 10 + (*q - '0'); q++; d++; }
			if (!d || d > 3 || v > 255) break;
			o[k] = v;
			if (k < 3) { if (*q != '.') break; q++; }
		}
		if (k < 4 || isalnum((unsigned char)*q) || (*q == '.' && isdigit((unsigned char)q[1])))
			continue;
		len = (int)(q - p);
		/* a version, by the word before it: the GPU's "BVNC 22.102.54.38" */
		{
			const char *w = p;

			while (w > line && w[-1] == ' ') w--;
			if ((w - line >= 4 && !strncasecmp(w - 4, "BVNC", 4)) ||
			    (w - line >= 7 && !strncasecmp(w - 7, "version", 7)) ||
			    (w > line && (w[-1] == 'v' || w[-1] == 'V') && w == p)) {
				p = q - 1;
				continue;
			}
		}
		if (o[0] == 0 || o[0] == 10 || o[0] == 127 || o[0] >= 224 ||
		    (o[0] == 169 && o[1] == 254) || (o[0] == 172 && o[1] >= 16 && o[1] <= 31) ||
		    (o[0] == 192 && o[1] == 168) || (o[0] == 100 && o[1] >= 64 && o[1] <= 127)) {
			p = q - 1;
			continue;
		}
		splice(line, cap, p, (size_t)len, "(public IP)");
		p += 10;
	}
}

/* A key or token nobody named: 32 or more letters, digits, - and _, with
 * letters and at least one digit in six, and not all hex - a checksum or a
 * hash is something to compare, not a secret. A file name has words in it and
 * too few digits to be taken for one. */
static void mask_long_tokens(char *line, size_t cap)
{
	char *p = line;

	while (*p) {
		char *e = p;
		size_t len, digits = 0, alpha = 0, i;
		bool hex = true;

		if (!word_ch(*p)) { p++; continue; }
		while (word_ch(*e)) e++;
		len = (size_t)(e - p);
		for (i = 0; i < len; i++) {
			if (isdigit((unsigned char)p[i])) digits++;
			else if (isalpha((unsigned char)p[i])) alpha++;
			if (!isxdigit((unsigned char)p[i])) hex = false;
		}
		if (len >= 32 && !hex && alpha && digits * 6 >= len) {
			char with[24];

			snprintf(with, sizeof with, "%.4s(hidden)", p);
			splice(line, cap, p, len, with);
			e = p + strlen(with);
		}
		p = e;
	}
}

static void mask_shapes(char *line, size_t cap)
{
	mask_userinfo(line, cap);
	mask_query(line, cap);
	mask_labelled(line, cap);
	mask_emails(line, cap);
	mask_syncthing_ids(line, cap);
	mask_public_ips(line, cap);
	mask_long_tokens(line, cap);
}

/* ---- the pack ------------------------------------------------------------- */

static bool copy_masked(const char *from, const char *to,
                        const char *const *secrets, int n)
{
	FILE *in = fopen(from, "r"), *out;
	char line[4096];

	if (!in) return false;
	if (!(out = fopen(to, "w"))) { fclose(in); return false; }
	while (fgets(line, sizeof line, in)) {
		logpack_mask_line(line, sizeof line, secrets, n);
		fputs(line, out);
	}
	fclose(in);
	return fclose(out) == 0;
}

static bool run(char *const argv[])
{
	pid_t pid = fork();
	int status;

	if (pid < 0) return false;
	if (pid == 0) {
		execvp(argv[0], argv);
		_exit(127);
	}
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR) return false;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool logpack_build(const char *logs_dir, const char *about,
                   const char *const *secrets, int n,
                   const char *folder, const char *out, char *err, size_t en)
{
	return logpack_build_more(logs_dir, about, secrets, n, NULL, 0, folder, out, err, en);
}

/* ChaOS: a session's log is packed under the date and time it began, so a
 * pack reads in order and a report's "it happened Tuesday evening" finds its
 * file: chaos-2026-11-25-2031.log. From the "logs: session YYYY-MM-DD HH:MM"
 * line the launcher writes once its zone is set (2.7.9-test2 on), else from the time the
 * file was last written, which for an older log is when its session ended
 * (then "-ended" says so). Clock wrong before Wi-Fi: the name is what the
 * Brick believed. */
static void session_name(const char *from, char *out, size_t n)
{
	FILE *f = fopen(from, "r");
	char line[256];
	int y, mo, d, h, mi, k;
	struct stat st;

	if (f) {
		for (k = 0; k < 400 && fgets(line, sizeof line, f); k++) {
			const char *s = strstr(line, "logs: session ");

			if (s && sscanf(s, "logs: session %4d-%2d-%2d %2d:%2d", &y, &mo, &d, &h, &mi) == 5) {
				fclose(f);
				snprintf(out, n, "chaos-%04d-%02d-%02d-%02d%02d.log", y, mo, d, h, mi);
				return;
			}
		}
		fclose(f);
	}
	if (stat(from, &st) == 0) {
		struct tm tm;
		time_t t = st.st_mtime;

		localtime_r(&t, &tm);
		snprintf(out, n, "chaos-%04d-%02d-%02d-%02d%02d-ended.log", tm.tm_year + 1900,
		         tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
		return;
	}
	snprintf(out, n, "chaos-unknown.log");
}

bool logpack_build_more(const char *logs_dir, const char *about,
                        const char *const *secrets, int n,
                        const char *const *more, int nmore,
                        const char *folder, const char *out, char *err, size_t en)
{
	char stage[] = "/tmp/tortos-logpack.XXXXXX", dir[512], path[1024], from[1024];
	DIR *d;
	struct dirent *e;
	FILE *f;
	int logs = 0;
	bool ok;

	err[0] = '\0';
	if (!mkdtemp(stage)) { snprintf(err, en, "no room in /tmp"); return false; }
	snprintf(dir, sizeof dir, "%s/%s", stage, folder);
	if (mkdir(dir, 0755) != 0) { snprintf(err, en, "no room in /tmp"); ok = false; goto out; }

	if ((d = opendir(logs_dir))) {
		while ((e = readdir(d))) {
			/* ChaOS: chaos.log, and tortos.log from before the rename,
			 * both packed as chaos.log so the download names one system */
			const char *rest;
			if (!strncmp(e->d_name, "chaos.log", 9)) rest = e->d_name + 9;
			else if (!strncmp(e->d_name, "tortos.log", 10)) rest = e->d_name + 10;
			else continue;
			snprintf(from, sizeof from, "%s/%s", logs_dir, e->d_name);
			{
				/* by when it began (session_name); two the same minute
				 * get -2, -3 */
				char name[96], base[96];
				int dup;

				(void)rest;
				session_name(from, base, sizeof base);
				snprintf(name, sizeof name, "%s", base);
				for (dup = 2; dup < 100; dup++) {
					snprintf(path, sizeof path, "%s/%s", dir, name);
					if (access(path, F_OK) != 0) break;
					snprintf(name, sizeof name, "%.*s-%d.log", (int)(strlen(base) - 4), base, dup);
				}
			}
			if (copy_masked(from, path, secrets, n)) logs++;
		}
		closedir(d);
	}
	/* ChaOS: the other logs, each masked as chaos.log is, in more/ under
	 * the name given: a port's, PICO-8's, the updater's, the kernel's. A
	 * file that isn't there is simply left out. */
	if (nmore > 0) {
		int k;

		snprintf(path, sizeof path, "%s/more", dir);
		mkdir(path, 0755);
		for (k = 0; k + 1 < nmore * 2; k += 2) {
			if (!more[k] || !more[k + 1] || access(more[k], R_OK) != 0) continue;
			snprintf(path, sizeof path, "%s/more/%s", dir, more[k + 1]);
			copy_masked(more[k], path, secrets, n);
		}
	}
	snprintf(path, sizeof path, "%s/about.txt", dir);
	if ((f = fopen(path, "w"))) {
		const char *line = about ? about : "";

		/* about.txt names the same things a log does, so it is masked too.
		 * Line by line and blank lines kept: strtok ran two newlines into
		 * one and lost the gap before the shelves. */
		while (*line) {
			const char *nl = strchr(line, '\n');
			size_t len = nl ? (size_t)(nl - line) : strlen(line);
			char buf[1024];

			snprintf(buf, sizeof buf, "%.*s", (int)(len < sizeof buf ? len : sizeof buf - 1), line);
			logpack_mask_line(buf, sizeof buf, secrets, n);
			fprintf(f, "%s\n", buf);
			line += len + (nl ? 1 : 0);
		}
		fclose(f);
	}
	if (!logs) { snprintf(err, en, "no logs found"); ok = false; goto out; }

	{
		char *argv[] = { (char *)"tar", (char *)"-czf", (char *)out, (char *)"-C",
		                 stage, (char *)folder, NULL };

		remove(out);
		ok = run(argv);
		if (!ok) snprintf(err, en, "tar failed");
	}
out:
	{
		char *argv[] = { (char *)"rm", (char *)"-rf", stage, NULL };

		run(argv);
	}
	return ok;
}
