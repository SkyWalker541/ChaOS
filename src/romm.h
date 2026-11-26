/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_ROMM_H
#define CHAOS_ROMM_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* ChaOS: RomM (romm.app), a self-hosted library of games, from the Brick.
 *
 * Three things, all over RomM's own API (as its first-party handheld client,
 * Grout, uses it - github.com/rommapp/grout, MIT):
 *
 *   games   its library by system, a game downloaded onto its ChaOS shelf
 *           with its cover as the box art
 *   saves   RomM's device sync: what this Brick has, what the server has,
 *           and the server's plan of what to send which way
 *   BIOS    the firmware RomM keeps for the systems ChaOS has, into Bios/
 *
 * Signing in is RomM's own pairing: RomM 5 and later show a short code on
 * the Brick to approve in RomM's web page (the device authorization flow);
 * RomM 4 has a pairing code made in its web page and typed here. Either way
 * the Brick ends with a client token, kept in .userdata (0600), never a
 * password.
 *
 * No SDL here, and every request is blocking (the launcher runs them behind
 * a panel): `make check-romm` runs all of it against a stand-in server. */

#define ROMM_STR 256

typedef struct {
	char url[ROMM_STR];        /* the server: "http://nas.local:8080", no trailing / */
	char token[600];           /* the client token, "" when not signed in */
	char device_id[80];        /* this Brick as RomM knows it */
	char ident[80];            /* this Brick as it names itself to RomM: random,
	                            * kept across sign-outs and servers */
	char user[64];             /* whose library */
} romm_conn;

typedef struct {
	int  id;
	char slug[48];
	char fs_slug[48];
	char name[96];
	int  roms;
	int  firmware;
	const char *tag;           /* the ChaOS system it goes to (systems.cfg's), or NULL */
} romm_platform;

typedef struct {
	int  id;
	int  platform_id;
	char name[160];            /* to show */
	char fs_name[ROMM_STR];    /* its file (or folder) on the server */
	long size;
	bool multi;                /* several files: comes as one zip, unpacked to a folder */
	int  file_id;              /* the single file's, 0 for a multi-file game */
	char file_name[ROMM_STR];  /* that file's name, what lands on the card */
	char cover[ROMM_STR];      /* path_cover_small, or url_cover */
	int  nplay;                /* its files that are the game (not a manual): >1, a choice */
} romm_rom;

/* One of a game's files, for choosing which to download */
#define ROMM_FILES_MAX 32
typedef struct {
	int  id;
	long size;
	char name[ROMM_STR];
} romm_file;

typedef struct {
	int  id;
	char file_name[160];
	long size;
	char md5[40];
} romm_firmware;

/* Where it all is: `dir` is .userdata/<device>/romm */
/* A new identifier for this Brick to give RomM: random, nothing about it */
void romm_new_ident(char *out, size_t n);

bool romm_load(romm_conn *c, const char *dir);
bool romm_save(const romm_conn *c, const char *dir);
/* Signed out: the token and the device forgotten, the server kept */
void romm_forget(romm_conn *c, const char *dir);
/* "nas.local:8080" as "http://nas.local:8080", no trailing slash; false if
 * it can't be a server's address */
bool romm_set_url(romm_conn *c, const char *typed);

/* ---- signing in -------------------------------------------------------- */

typedef struct {
	char device_code[200];
	char user_code[32];
	char verify[ROMM_STR];     /* the page to approve it on, the code in it */
	int  interval, expires;    /* seconds */
} romm_pair;

enum { ROMM_PAIR_WAIT, ROMM_PAIR_SLOW, ROMM_PAIR_OK, ROMM_PAIR_DENIED,
       ROMM_PAIR_EXPIRED, ROMM_PAIR_ERROR };

/* RomM 5's pairing: 1 begun, 0 the server hasn't it (RomM 4: a pairing code
 * instead), -1 couldn't reach it (why says) */
int  romm_pair_begin(romm_conn *c, const char *device_name, romm_pair *p, char *why, size_t wn);
/* Once a poll interval: ROMM_PAIR_OK puts the token in c */
int  romm_pair_poll(romm_conn *c, const romm_pair *p, char *why, size_t wn);
/* RomM 4's: the code made in its web page */
bool romm_exchange(romm_conn *c, const char *code, char *why, size_t wn);
/* Who the token is (users/me), and this Brick registered as a device for
 * save sync if it isn't yet */
bool romm_whoami(romm_conn *c, char *why, size_t wn);
bool romm_register(romm_conn *c, const char *device_name, char *why, size_t wn);

/* ---- the library ------------------------------------------------------- */

/* Which ChaOS system (systems.cfg's tag) a RomM platform's games go on */
const char *romm_tag_for(const char *slug, const char *fs_slug);

int  romm_platforms(const romm_conn *c, romm_platform *out, int max, char *why, size_t wn);
/* A page of a platform's games, by name. *total, the platform's count, or
 * -1 when the server didn't say. ROMM_PAGE_TOO_BIG when the page was too
 * long to read whole: ask for fewer. */
#define ROMM_PAGE_TOO_BIG (-2)
int  romm_roms(const romm_conn *c, int platform_id, int offset, int limit, romm_rom *out,
               int max, int *total, char *why, size_t wn);
/* A platform's whole list, a page at a time: smaller pages when one is too
 * long to read, on to the server's end, and a page with no total taken as
 * the last only when it is short. `expect` is the platform's rom_count, for
 * the progress line; `progress` is told how many have come and returns false
 * to stop. 0 for the whole list; ROMM_LIST_PARTIAL, with `why`, for part of
 * it (or stopped: `why` untouched); -1 with `why` for none. *out is the
 * caller's to free in every case. */
#define ROMM_LIST_PARTIAL 1
int  romm_roms_all(const romm_conn *c, int platform_id, int expect, romm_rom **out, int *n,
                   bool (*progress)(void *ctx, int have, int total), void *ctx,
                   char *why, size_t wn);
/* ChaOS: how far this Brick's clock is from the RomM server's, in seconds
 * (positive: the Brick is ahead), from the Date its answer carries; 0 when
 * found, -1 when the server couldn't be asked or gave no date. Save sync
 * decides which save is newer by time, so a clock far off could keep an old
 * save over a new one. */
int  romm_clock_skew(const romm_conn *c, long *skew);
time_t romm_http_date(const char *s);

/* A game's files that are the game (not its manual), in RomM's order */
int  romm_rom_files(const romm_conn *c, int rom_id, romm_file *out, int max, char *why, size_t wn);
int  romm_firmware_list(const romm_conn *c, int platform_id, romm_firmware *out, int max,
                   char *why, size_t wn);

/* Where to fetch each */
void romm_rom_url(const romm_conn *c, const romm_rom *r, char *out, size_t n);
void romm_cover_url(const romm_conn *c, const romm_rom *r, char *out, size_t n);
void romm_firmware_url(const romm_conn *c, const romm_firmware *f, char *out, size_t n);

/* ---- what came from RomM (games.tsv: id, system folder, file) ----------- */

bool romm_map_add(const char *dir, int rom_id, const char *folder, const char *file);
/* Which of a platform's games `file` (a name on the card: a file, or a
 * multi-file game's folder) is: RomM's own name for it, else the same name
 * without extension, case or punctuation when only one game has it. -1 for
 * none. For games put on the card without RomM, so their saves can sync. */
int  romm_match_file(const romm_rom *g, int n, const char *file);
/* The RomM id of a file on a shelf, 0 if it didn't come from RomM */
int  romm_map_find(const char *dir, const char *folder, const char *file);
typedef bool (*romm_map_fn)(int rom_id, const char *folder, const char *file, void *ctx);
void romm_map_each(const char *dir, romm_map_fn fn, void *ctx);

/* ---- saves ------------------------------------------------------------- */

typedef struct {
	int  rom_id;
	char file_name[ROMM_STR];  /* the save's own name: "Zelda.srm" */
	char md5[40];
	time_t mtime;
	long size;
} romm_local_save;

typedef struct {
	char action[12];           /* upload, download, conflict, no_op */
	int  rom_id;
	int  save_id;              /* 0 when none */
	char file_name[ROMM_STR];
} romm_sync_op;

/* The sync's plan, for these saves: its session, and up to `max` ops */
int  romm_negotiate(const romm_conn *c, const romm_local_save *s, int n, int *session,
                    romm_sync_op *ops, int max, char *why, size_t wn);
bool romm_save_upload(const romm_conn *c, int rom_id, const char *path, char *why, size_t wn);
/* A save's content into `path` (through path.part), and marked downloaded */
bool romm_save_download(const romm_conn *c, int save_id, const char *path, char *why, size_t wn);
bool romm_sync_complete(const romm_conn *c, int session, int done, int failed);

/* ---- save states (ChaOS 2.7.3) ------------------------------------------ */

typedef struct {
	int    id;
	char   file_name[ROMM_STR];  /* "Zelda.gba.1.state" */
	time_t updated;              /* the server's updated_at */
	long   size;
} romm_state;

/* A game's states on the server, up to `max`; -1 on failure */
int  romm_states(const romm_conn *c, int rom_id, romm_state *out, int max, char *why, size_t wn);
/* Up as a new state (id 0) or over the server's state `id` */
bool romm_state_upload(const romm_conn *c, int rom_id, int id, const char *path, char *why, size_t wn);
/* A state's content into `path`, through path.part */
bool romm_state_download(const romm_conn *c, int id, const char *path, char *why, size_t wn);

#endif
