/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_SYNCTHING_H
#define CHAOS_SYNCTHING_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: Syncthing (https://syncthing.net), folders kept the same across the
 * player's devices - the Brick's saves with a PC's RetroArch, its games with
 * another Brick - over the network at home, with no server in between.
 *
 * NOT IN THE IMAGE. Syncthing is fetched when it's first turned on, from the
 * project's own GitHub release, the version and its SHA-256 pinned here, so
 * what runs is exactly what was checked. It lives in the userdata
 * (.userdata/brick/syncthing), with its keys, its config and its database,
 * so an update leaves it as it was.
 *
 * NOTHING RUNS DURING A GAME. While a game, a port or PICO-8 is up the
 * process is stopped (SIGSTOP): not slowed, stopped, using no CPU at all,
 * and it goes on (SIGCONT) when the shelf is back. What changed meanwhile is
 * picked up then - Syncthing writes into temporary files and renames them, so
 * a stop never leaves half a file under a save's name. On the shelf it runs
 * at a low priority, on two cores at most.
 *
 * ON THE BRICK, the ChaOS menu's ROM Management > Syncthing: on and off, this
 * Brick's device ID, three folders to share - Saves, Games (Roms) and BIOS -
 * each on or off, the devices paired with it and whether each is connected,
 * and a device asking to pair, accepted or ignored with a button. A folder
 * that's on is shared with every paired device; the IDs are fixed
 * (chaos-saves, chaos-roms, chaos-bios), so two Bricks share each other's
 * without anyone choosing a name.
 *
 * ELSEWHERE, Syncthing's own web page, on port 8384 of the Brick's address,
 * signed into as "chaos" with a password ChaOS makes and shows: everything
 * the Brick's screen doesn't do (ignore patterns, versioning, a folder of
 * one's own).
 *
 * PAIRING, from a computer that runs Syncthing: Add Remote Device lists the
 * Brick among the nearby devices; picked, the Brick asks, and A accepts.
 * Then the computer is offered the Brick's folders and says where each goes.
 *
 * The REST API is asked with the API key ChaOS gives Syncthing when it
 * starts (STGUIAPIKEY), sent as a bearer token to 127.0.0.1. */

#define STH_VERSION "v2.1.7"
#define STH_TARBALL "syncthing-linux-arm64-" STH_VERSION ".tar.gz"
#define STH_URL "https://github.com/syncthing/syncthing/releases/download/" STH_VERSION "/" STH_TARBALL
/* from the release's sha256sum.txt.asc, signed by the Syncthing release key */
#define STH_SHA256 "48e456690cd2b97e7d26b02b5ac35f62a3b36172267b144cfeaf02c993ab955c"
#define STH_GUI_PORT 8384
#define STH_GUI_USER "chaos"

/* ChaOS's three folders, in the order the screen lists them */
#define STH_FOLDERS 3
typedef struct {
	const char *id;      /* the same on every Brick */
	const char *label;   /* the name the other devices see */
	const char *row;     /* the Brick's own row */
	const char *sub;     /* the card's folder */
} sth_folder_def;
extern const sth_folder_def STH_FOLDER[STH_FOLDERS];

#define STH_ID_LEN 64
#define STH_NAME_LEN 64
#define STH_MAX_DEV 8

typedef struct {
	char id[STH_ID_LEN];
	char name[STH_NAME_LEN];
	bool connected;
} sth_device;

typedef struct {
	bool on;             /* in Syncthing's config */
	bool paused;
	int  percent;        /* how much of it the Brick has, 0..100 */
	char state[24];      /* "idle", "scanning", "syncing", ... */
	int  errors;
} sth_folder_state;

/* What the screen shows, from one round of the REST API */
typedef struct {
	bool ok;                         /* Syncthing answered */
	char my_id[STH_ID_LEN];
	int  ndev;                       /* paired, not this Brick */
	sth_device dev[STH_MAX_DEV];
	int  npend;                      /* asking to pair */
	sth_device pend[STH_MAX_DEV];
	sth_folder_state folder[STH_FOLDERS];
} sth_info;

/* ---- the parts that read Syncthing's answers, on their own for the check */

/* /rest/system/status: myID */
bool sth_parse_my_id(const char *json, char *out, size_t n);
/* /rest/config/devices with /rest/system/connections: the devices that
 * aren't `self`, each with its connection */
int  sth_parse_devices(const char *devices, const char *connections, const char *self,
                       sth_device *out, int max);
/* /rest/cluster/pending/devices */
int  sth_parse_pending(const char *json, sth_device *out, int max);
/* /rest/config/folders: which of ChaOS's three are there, and paused */
void sth_parse_folders(const char *json, sth_info *in);
/* /rest/db/status?folder=: its state, and how much is had */
void sth_parse_db_status(const char *json, sth_folder_state *f);

/* The folder's config, shared with every paired device (`ids`), as JSON for
 * PUT /rest/config/folders/<id> */
int  sth_folder_json(int which, const char *card, const char *const *ids, int nids,
                     char *out, size_t n);

/* "PQVYOOZ-D4QN3KW-..." to its first block, the way Syncthing shows a
 * device in short */
void sth_short_id(const char *id, char *out, size_t n);

/* ---- the process, on the device */

/* Where everything is: the userdata and the card */
void sth_init(const char *userdata, const char *card);
bool sth_installed(void);
bool sth_running(void);

/* Fetching it: begin, then step until done or failed; never waits */
typedef enum { STH_GET_DOWNLOAD, STH_GET_UNPACK, STH_GET_DONE, STH_GET_FAILED } sth_get_state;
bool sth_get_begin(void);
sth_get_state sth_get_step(void);
void sth_get_cancel(void);
long sth_get_bytes(void);
const char *sth_problem(void);

/* Started (keys and config made the first time, with the password, which
 * sth_password then answers) or stopped. Starting returns at once; the API
 * answers a few seconds later. */
bool sth_start(void);
void sth_stop(void);
const char *sth_password(void);

/* Around a game, a port or PICO-8: true stops it, false lets it go on */
void sth_hold(bool hold);

/* ---- the REST API, on the device: each asks 127.0.0.1 and waits */

/* All of sth_info, started in the background; then polled until it's in */
bool sth_poll_begin(void);
bool sth_poll_step(sth_info *out);   /* true when `out` is new */

bool sth_set_name(const char *name);
bool sth_accept(const char *id, const char *name);
bool sth_ignore(const char *id);
bool sth_forget(const char *id);
bool sth_folder_set(int which, bool on);

#endif
