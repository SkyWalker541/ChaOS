/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See ss.h for where the two credentials come from and why. */
#include <stdio.h>
#include <string.h>

#include "ss.h"

#include "db.h"
#include "net.h"
#include "rajson.h"
#include "ss_creds.h"

#define SS_API "https://api.screenscraper.fr/api2"

/* The name every call identifies this software by. ScreenScraper issues a
 * developer id to a named piece of software and this is that name; it is not
 * decoration and it is what an abuse report would arrive against. */
#define SS_SOFT "ChaOS"

static char g_user[SS_USER_MAX];
static char g_pass[SS_PASS_MAX];
/* ChaOS: the player's own developer pair, when the build has none */
static char g_devid[SS_DEVID_MAX];
static char g_devpass[SS_PASS_MAX];

bool ss_dev_builtin(void)
{
	return SS_DEVID[0] != '\0' && SS_DEVPASS[0] != '\0';
}

static const char *dev_id(void)   { return ss_dev_builtin() ? SS_DEVID : g_devid; }
static const char *dev_pass(void) { return ss_dev_builtin() ? SS_DEVPASS : g_devpass; }

bool ss_have_dev(void)
{
	return dev_id()[0] != '\0' && dev_pass()[0] != '\0';
}

const char *ss_dev_user_id(void) { return g_devid; }

static void dev_load(void)
{
	char id[SS_DEVID_MAX * 2], pass[SS_PASS_MAX * 2];

	memset(g_devid, 0, sizeof g_devid);
	memset(g_devpass, 0, sizeof g_devpass);
	db_get_str(db_dev(), "ss.dev.id", id, sizeof id, "");
	db_get_str(db_dev(), "ss.dev.password", pass, sizeof pass, "");
	if (id[0] && pass[0] && strlen(id) < sizeof g_devid && strlen(pass) < sizeof g_devpass) {
		memcpy(g_devid, id, strlen(id) + 1);
		memcpy(g_devpass, pass, strlen(pass) + 1);
	}
	memset(pass, 0, sizeof pass);
}

bool ss_dev_set(const char *id, const char *password)
{
	bool clear = (!id || !*id) && (!password || !*password);

	if (!clear && (!id || !*id || !password || !*password ||
	               strlen(id) >= sizeof g_devid || strlen(password) >= sizeof g_devpass))
		return false;
	memset(g_devid, 0, sizeof g_devid);
	memset(g_devpass, 0, sizeof g_devpass);
	if (!clear) {
		snprintf(g_devid, sizeof g_devid, "%s", id);
		snprintf(g_devpass, sizeof g_devpass, "%s", password);
	}
	return db_set_str(db_dev(), "ss.dev.id", g_devid) &&
	       db_set_str(db_dev(), "ss.dev.password", g_devpass);
}

bool ss_dev_check(const char *id, const char *password, char *err, size_t errn)
{
	char body[4096], url[128];
	net_field f[4];
	jsv root, resp;
	long got;

	if (err && errn) err[0] = '\0';
	if (!id || !*id || !password || !*password) {
		if (err) snprintf(err, errn, "the ID and password are both needed");
		return false;
	}
	if (strlen(id) >= SS_DEVID_MAX || strlen(password) >= SS_PASS_MAX) {
		if (err) snprintf(err, errn, "that ID or password is too long");
		return false;
	}
	if (!net_online()) {
		if (err) snprintf(err, errn, "not on a network");
		return false;
	}
	f[0].k = "devid";       f[0].v = id;
	f[1].k = "devpassword"; f[1].v = password;
	f[2].k = "softname";    f[2].v = SS_SOFT;
	f[3].k = "output";      f[3].v = "json";
	snprintf(url, sizeof url, "%s/ssinfraInfos.php", SS_API);
	got = net_get_buf(url, f, 4, body, sizeof body, 20);
	if (got < 0) {
		/* as at sign-in: a refused pair and a site that is down look alike */
		if (err) snprintf(err, errn, "not accepted - check the ID, or try later");
		return false;
	}
	root = js_root(body, (size_t)got);
	if (!js_member(root, "response", &resp)) {
		if (err) snprintf(err, errn, "that developer ID was not recognized");
		return false;
	}
	return true;
}

bool ss_sign_out(void)
{
	ss_creds_clear();
	return ss_creds_save();
}

bool ss_signed_in(void)
{
	return ss_have_dev() && g_user[0] != '\0' && g_pass[0] != '\0';
}

const char *ss_user(void) { return g_user; }

void ss_creds_clear(void)
{
	/* Wiped rather than truncated. The password is the credential itself
	 * here, not a token standing in for one. */
	memset(g_user, 0, sizeof g_user);
	memset(g_pass, 0, sizeof g_pass);
}

bool ss_creds_load(void)
{
	char user[SS_USER_MAX * 2], pass[SS_PASS_MAX * 2];

	ss_creds_clear();
	dev_load();
	db_get_str(db_dev(), "ss.user", user, sizeof user, "");
	db_get_str(db_dev(), "ss.password", pass, sizeof pass, "");
	/* Over-long is REFUSED, not stored short, the same as the RA account: half
	 * a password reads as signed in and then every request comes back refused,
	 * which looks like a wrong account rather than a truncated one. The
	 * buffers are twice the limit so an over-long value arrives whole and can
	 * be seen to be over-long. */
	if (strlen(user) >= sizeof g_user || strlen(pass) >= sizeof g_pass) {
		fprintf(stderr, "ss: stored credentials are over-long; ignoring them\n");
		ss_creds_clear();
		return false;
	}
	memcpy(g_user, user, strlen(user) + 1);
	memcpy(g_pass, pass, strlen(pass) + 1);
	memset(pass, 0, sizeof pass);
	return ss_signed_in();
}

bool ss_creds_save(void)
{
	/* The device database is 0600, which is what makes this storable at all.
	 * See db_open, and ss.h on why there is a password here and not a token. */
	return db_set_str(db_dev(), "ss.user", g_user) &&
	       db_set_str(db_dev(), "ss.password", g_pass);
}

bool ss_auth_query(char *out, size_t n)
{
	char user[SS_USER_MAX * 3], pass[SS_PASS_MAX * 3];
	char did[SS_DEVID_MAX * 3], dpass[SS_PASS_MAX * 3];
	int len;

	if (!out || !n) return false;
	out[0] = '\0';
	if (!ss_signed_in()) return false;

	net_urlencode(g_user, user, sizeof user);
	net_urlencode(g_pass, pass, sizeof pass);
	/* encoded too: a pair the player typed may hold anything */
	net_urlencode(dev_id(), did, sizeof did);
	net_urlencode(dev_pass(), dpass, sizeof dpass);
	len = snprintf(out, n, "devid=%s&devpassword=%s&softname=%s&output=json"
	                       "&ssid=%s&sspassword=%s",
	               did, dpass, SS_SOFT, user, pass);
	memset(pass, 0, sizeof pass);
	memset(dpass, 0, sizeof dpass);
	if (len < 0 || (size_t)len >= n) { out[0] = '\0'; return false; }
	return true;
}

bool ss_sign_in(const char *user, const char *password, char *err, size_t errn)
{
	char body[4096], url[128];
	net_field f[6];
	jsv root, resp, who;
	long got;
	bool ok;

	if (err && errn) err[0] = '\0';
	if (!ss_have_dev()) {
		if (err) snprintf(err, errn, "no developer ID: add one first");
		return false;
	}
	if (!user || !*user || !password || !*password) {
		if (err) snprintf(err, errn, "user and password are both needed");
		return false;
	}
	if (strlen(user) >= SS_USER_MAX || strlen(password) >= SS_PASS_MAX) {
		if (err) snprintf(err, errn, "that user or password is too long");
		return false;
	}
	if (!net_online()) {
		if (err) snprintf(err, errn, "not on a network");
		return false;
	}

	f[0].k = "devid";       f[0].v = dev_id();
	f[1].k = "devpassword"; f[1].v = dev_pass();
	f[2].k = "softname";    f[2].v = SS_SOFT;
	f[3].k = "output";      f[3].v = "json";
	f[4].k = "ssid";        f[4].v = user;
	f[5].k = "sspassword";  f[5].v = password;

	/* Their account record, which is the nearest thing to a sign-in: there is
	 * no token to ask for, so the question is simply whether they will answer
	 * to this pair at all. */
	snprintf(url, sizeof url, "%s/ssuserInfos.php", SS_API);
	got = net_get_buf(url, f, 6, body, sizeof body, 20);
	if (got < 0) {
		/* curl runs with `fail`, so an HTTP error is an exit status and no
		 * body: a wrong password and a site that is down arrive identically.
		 * Guessing between them would put a confident wrong sentence on the
		 * screen, which is worse than a vague right one. */
		if (err) snprintf(err, errn,
		                  "could not sign in - check the account, or try later");
		return false;
	}

	/* An `ssuser` object in the reply is the whole test. They answer a bad
	 * pair with an error rather than with an empty account, so its presence
	 * is the confirmation and nothing inside it has to be read. */
	root = js_root(body, (size_t)got);
	ok = js_member(root, "response", &resp) && js_member(resp, "ssuser", &who);
	if (!ok) {
		if (err) snprintf(err, errn, "that account was not recognized");
		return false;
	}

	ss_creds_clear();
	snprintf(g_user, sizeof g_user, "%s", user);
	snprintf(g_pass, sizeof g_pass, "%s", password);
	if (!ss_creds_save()) {
		ss_creds_clear();
		if (err) snprintf(err, errn, "signed in, but the account could not be saved");
		return false;
	}
	return true;
}
