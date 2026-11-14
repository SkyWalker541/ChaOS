/* ChaOS: Syncthing (src/syncthing.h) - its answers read, a folder's config
 * written, on Syncthing's own replies as v2.1.7 gives them.
 *
 * With STH_LIVE=<an x86 syncthing binary> it runs the real thing too: two
 * Syncthings in a temporary folder, the second asking to pair with the
 * first, the first accepting and sharing its Saves, the file arriving, and a
 * stop and go-on in between. Not part of make check (it needs the binary
 * and a minute); run by hand: STH_LIVE=... build-native/syncthing-check */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../src/net.h"
#include "../src/syncthing.h"

static int fails;
#define CK(c, ...) do { if (!(c)) { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static const char *STATUS = "{\"alloc\":5262696,\"myID\":\"PQVYOOZ-D4QN3KW-JSH4GNT-FNUHCUA-YC4OVJG-5K7YNQZ-NSUV7HG-OFIW6AY\",\"uptime\":3}";
static const char *DEVICES = "[{\"deviceID\":\"PQVYOOZ-D4QN3KW-JSH4GNT-FNUHCUA-YC4OVJG-5K7YNQZ-NSUV7HG-OFIW6AY\",\"name\":\"brick\",\"addresses\":[\"dynamic\"]},"
                             "{\"deviceID\":\"73MDBVS-YMPQ5SC-OOUBCKK-UVEAAFV-WRRBON2-N2XPLKA-YM32ZS3-XF2S3QF\",\"name\":\"Desk \\\"PC\\\"\",\"addresses\":[\"dynamic\"]},"
                             "{\"deviceID\":\"AAAAAAA-BBBBBBB-CCCCCCC-DDDDDDD-EEEEEEE-FFFFFFF-GGGGGGG-HHHHHHH\",\"name\":\"\"}]";
static const char *CONNS = "{\"connections\":{\"73MDBVS-YMPQ5SC-OOUBCKK-UVEAAFV-WRRBON2-N2XPLKA-YM32ZS3-XF2S3QF\":{\"connected\":true,\"clientVersion\":\"v2.1.7\"},"
                           "\"AAAAAAA-BBBBBBB-CCCCCCC-DDDDDDD-EEEEEEE-FFFFFFF-GGGGGGG-HHHHHHH\":{\"connected\":false}},\"total\":{}}";
static const char *PENDING = "{\"73MDBVS-YMPQ5SC-OOUBCKK-UVEAAFV-WRRBON2-N2XPLKA-YM32ZS3-XF2S3QF\":{\"time\":\"2026-11-14T08:56:49Z\",\"name\":\"vm\",\"address\":\"127.0.0.1:22001\"}}";
static const char *FOLDERS = "[{\"id\":\"chaos-saves\",\"label\":\"ChaOS Saves\",\"paused\":false},{\"id\":\"music\",\"paused\":false},{\"id\":\"chaos-bios\",\"paused\":true}]";
static const char *DBST = "{\"errors\":0,\"pullErrors\":1,\"globalBytes\":1000,\"needBytes\":250,\"state\":\"syncing\"}";

static void pure(void)
{
	char id[STH_ID_LEN], js[4096], sh[16];
	sth_device d[STH_MAX_DEV];
	sth_info in;
	int n;
	const char *ids[2] = { "73MDBVS-YMPQ5SC", "X\"Y" };

	CK(sth_parse_my_id(STATUS, id, sizeof id) && !strncmp(id, "PQVYOOZ-", 8), "my ID: %s", id);
	n = sth_parse_devices(DEVICES, CONNS, id, d, STH_MAX_DEV);
	CK(n == 2, "two devices besides this one, not %d", n);
	CK(n >= 1 && !strcmp(d[0].name, "Desk \"PC\"") && d[0].connected, "the first is connected and named");
	CK(n >= 2 && !strcmp(d[1].name, "AAAAAAA") && !d[1].connected, "a nameless one is its short ID: %s", n >= 2 ? d[1].name : "");
	n = sth_parse_pending(PENDING, d, STH_MAX_DEV);
	CK(n == 1 && !strcmp(d[0].name, "vm") && !strncmp(d[0].id, "73MDBVS", 7), "one asking to pair");
	CK(sth_parse_pending("{}", d, STH_MAX_DEV) == 0, "nobody asking");
	memset(&in, 0, sizeof in);
	sth_parse_folders(FOLDERS, &in);
	CK(in.folder[0].on && !in.folder[0].paused && !in.folder[1].on && in.folder[2].on && in.folder[2].paused,
	   "Saves on, Games off, BIOS on and paused");
	sth_parse_db_status(DBST, &in.folder[0]);
	CK(in.folder[0].percent == 75 && in.folder[0].errors == 1 && !strcmp(in.folder[0].state, "syncing"),
	   "75%% syncing with an error: %d %d %s", in.folder[0].percent, in.folder[0].errors, in.folder[0].state);
	sth_parse_db_status("{\"globalBytes\":0,\"needBytes\":0,\"state\":\"idle\"}", &in.folder[1]);
	CK(in.folder[1].percent == 100, "an empty folder is all there");
	CK(sth_folder_json(1, "/mnt/SDCARD", ids, 2, js, sizeof js) > 0 &&
	   strstr(js, "\"id\":\"chaos-roms\"") && strstr(js, "\"path\":\"/mnt/SDCARD/Roms\"") &&
	   strstr(js, "\"ignorePerms\":true") && strstr(js, "{\"deviceID\":\"X\\\"Y\"}"),
	   "a folder's config: %s", js);
	CK(sth_folder_json(1, "/mnt/SDCARD", ids, 2, js, 40) < 0, "too small a buffer says so");
	sth_short_id("PQVYOOZ-D4QN3KW", sh, sizeof sh);
	CK(!strcmp(sh, "PQVYOOZ"), "short ID");
	CK(strlen(STH_SHA256) == 64, "the pinned checksum is a SHA-256");
}

/* ---- live ---- */

static int sh(const char *cmd) { int r = system(cmd); return WIFEXITED(r) ? WEXITSTATUS(r) : -1; }

static bool poll_info(sth_info *in, int secs)
{
	time_t end = time(NULL) + secs;

	while (time(NULL) < end) {
		if (sth_poll_begin()) {
			while (!sth_poll_step(in)) usleep(20000);
			if (in->ok) return true;
		}
		usleep(300000);
	}
	return false;
}

static void live(const char *bin)
{
	char base[] = "/tmp/sth-live-XXXXXX", cmd[2048], ud[200], card[200], p[400];
	sth_info in;
	int i;
	time_t end;

	if (!mkdtemp(base)) return;
	snprintf(ud, sizeof ud, "%s/ud", base);
	snprintf(card, sizeof card, "%s/card", base);
	snprintf(cmd, sizeof cmd, "mkdir -p %s/syncthing %s/Saves && cp '%s' %s/syncthing/syncthing && echo hi > %s/Saves/game.srm",
	         ud, card, bin, ud, card);
	sh(cmd);
	sth_init(ud, card);
	CK(sth_installed(), "installed");
	CK(sth_start(), "started: %s", sth_problem());
	CK(poll_info(&in, 30), "answered");
	printf("  live: this one is %.7s, password %s\n", in.my_id, sth_password());
	CK(sth_set_name("ChaOS Brick"), "named");
	CK(sth_folder_set(0, true), "Saves on");
	/* B, the "PC": its own Syncthing, which adds A by ID and dials it */
	snprintf(cmd, sizeof cmd,
	         "B=%s/B; S='%s'; STNODEFAULTFOLDER=1 $S generate --home=$B >/dev/null 2>&1;"
	         "(STGUIAPIKEY=kb $S serve --home=$B --no-browser --no-restart --no-upgrade --gui-address=127.0.0.1:18385 >/dev/null 2>&1 &);"
	         "until curl -s -H 'Authorization: Bearer kb' http://127.0.0.1:18385/rest/system/status >/dev/null; do :; done;"
	         "curl -s -H 'Authorization: Bearer kb' -X PATCH -d '{\"listenAddresses\":[\"tcp://127.0.0.1:22001\"],\"globalAnnounceEnabled\":false,\"relaysEnabled\":false,\"localAnnounceEnabled\":false}' http://127.0.0.1:18385/rest/config/options;"
	         "curl -s -H 'Authorization: Bearer kb' -X POST -d '{\"deviceID\":\"%s\",\"name\":\"Desk\",\"addresses\":[\"tcp://127.0.0.1:22000\"]}' http://127.0.0.1:18385/rest/config/devices",
	         base, bin, in.my_id);
	sh(cmd);
	end = time(NULL) + 90;
	do { poll_info(&in, 5); } while (in.npend == 0 && time(NULL) < end);
	CK(in.npend == 1, "B asks to pair");
	if (in.npend == 1) {
		char bid[STH_ID_LEN];

		snprintf(bid, sizeof bid, "%s", in.pend[0].id);
		CK(sth_accept(bid, in.pend[0].name), "accepted");
		poll_info(&in, 5);
		CK(in.ndev == 1 && in.npend == 0, "paired: %d devices, %d asking", in.ndev, in.npend);
		/* A dials B as well, to be quick about it */
		snprintf(cmd, sizeof cmd, "curl -s -H 'Authorization: Bearer %s' -X PATCH -d '{\"addresses\":[\"tcp://127.0.0.1:22001\"]}' http://127.0.0.1:%d/rest/config/devices/%s",
		         "x", STH_GUI_PORT, bid);
		/* (the key is ChaOS's own; read it from its file) */
		snprintf(cmd, sizeof cmd, "curl -s -H \"Authorization: Bearer $(cat %s/syncthing/apikey)\" -X PATCH -d '{\"addresses\":[\"tcp://127.0.0.1:22001\"]}' http://127.0.0.1:%d/rest/config/devices/%s",
		         ud, STH_GUI_PORT, bid);
		sh(cmd);
		/* B takes the offered Saves into its own folder */
		snprintf(cmd, sizeof cmd,
		         "until curl -s -H 'Authorization: Bearer kb' http://127.0.0.1:18385/rest/cluster/pending/folders | grep -q chaos-saves; do :; done;"
		         "curl -s -H 'Authorization: Bearer kb' -X PUT -d '{\"id\":\"chaos-saves\",\"label\":\"ChaOS Saves\",\"path\":\"%s/Bsaves\",\"devices\":[{\"deviceID\":\"%s\"}]}' http://127.0.0.1:18385/rest/config/folders/chaos-saves",
		         base, in.my_id);
		CK(sh(cmd) == 0, "B took Saves");
		snprintf(p, sizeof p, "%s/Bsaves/game.srm", base);
		end = time(NULL) + 60;
		while (access(p, R_OK) != 0 && time(NULL) < end) usleep(200000);
		CK(access(p, R_OK) == 0, "the save reached B");
		/* held for a game: a change made then arrives after */
		sth_hold(true);
		snprintf(cmd, sizeof cmd, "echo changed > %s/Saves/game.srm", card);
		sh(cmd);
		CK(!sth_poll_begin(), "no asking while it's held");
		sleep(3);
		snprintf(cmd, sizeof cmd, "grep -q changed %s", p);
		CK(sh(cmd) != 0, "nothing moves while it's held");
		sth_hold(false);
		end = time(NULL) + 60;
		while (sh(cmd) != 0 && time(NULL) < end) usleep(300000);
		CK(sh(cmd) == 0, "and it arrives once it goes on");
		poll_info(&in, 5);
		CK(in.ndev == 1 && in.dev[0].connected && in.folder[0].on, "connected, Saves on: %d %d %d",
		   in.ndev, in.ndev ? in.dev[0].connected : 0, in.folder[0].on);
		CK(sth_folder_set(2, true) && poll_info(&in, 5) && in.folder[2].on, "BIOS on");
		CK(sth_folder_set(2, false) && poll_info(&in, 5) && !in.folder[2].on, "and off");
		CK(sth_forget(bid) && poll_info(&in, 5) && in.ndev == 0, "forgotten");
	}
	sth_stop();
	CK(!sth_running(), "stopped");
	snprintf(cmd, sizeof cmd, "pkill -f 'home=%s/B'; rm -rf %s", base, base);
	sh(cmd);
	for (i = 0; i < 1; i++) {}
}

int main(void)
{
	const char *bin = getenv("STH_LIVE");

	pure();
	if (bin && *bin) live(bin);
	if (fails) { printf("syncthing: %d failures\n", fails); return 1; }
	printf("ok: Syncthing's answers read%s\n", bin && *bin ? ", and two of them paired and syncing" : "");
	return 0;
}
