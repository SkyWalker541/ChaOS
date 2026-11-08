/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_UPDATE_H
#define CHAOS_UPDATE_H

/* ChaOS: updates over Wi-Fi, from MENU > Updates.
 *
 * HOW AN UPDATE GOES ON. plastron's boot already knows how (its rootswitch,
 * in the initramfs on the boot partition): a new root at
 * .dev/root-update.squashfs on the games partition, and a file named
 * root-update on the boot partition, and the next boot writes the new root
 * over the root partition before anything has mounted it, reads it back, and
 * removes both. A write cut short leaves both, so the boot after tries again.
 * Then init.d/tortos sees a newer ChaOS in the new root and copies it onto the
 * card, as it does after ChaOS Installer's Update. This file gets the new root
 * there and asks for that boot; nothing here writes the system itself.
 *
 * WHAT IS FETCHED. Each release carries ChaOS-ota.txt, at the fixed address
 * of the latest release's downloads:
 *
 *     version = 0.8.0
 *     base    = 2.1.1          the TortOS release its boot partition is
 *     root    = ChaOS-v0.8.0-brick-root.squashfs.xz
 *     sha256  = <its SHA-256>
 *     size    = <its bytes>
 *     notes   = <one line>
 *
 * A release on another base may need a new boot partition (kernel, its
 * initramfs, the boot loader), which only ChaOS Installer writes, so it is
 * named and not offered. The download is checked against sha256 and then
 * unpacked (xz checks its own contents too); a mismatch removes it.
 *
 * NOTHING IS AUTOMATIC BUT THE QUESTION. Once a boot, when Wi-Fi is up, the
 * launcher asks whether there is a newer ChaOS and, if so, says so once on the
 * shelf and on the menu's row. Downloading and installing are the player's,
 * from the menu; a finished download waits on the card until they come back
 * to install it or remove it.
 *
 * No SDL here: tools/update-check.c links it. */

#include <stdbool.h>

typedef struct {
	char version[32];
	char base[32];
	char root[160];
	char sha256[65];
	long long size;
	char notes[160];
} ota_info;

/* Parse ChaOS-ota.txt. False when a field it needs is missing or wrong. */
bool ota_parse(const char *text, ota_info *o);

/* Compare dotted versions, "0.10.0" after "0.9.2": <0, 0, >0. */
int ota_vercmp(const char *a, const char *b);

typedef enum {
	UPD_IDLE,         /* not asked yet */
	UPD_CHECKING,
	UPD_CURRENT,      /* asked: this is the latest */
	UPD_AVAILABLE,    /* a newer one, on this base */
	UPD_INSTALLER,    /* a newer one, on another base: ChaOS Installer */
	UPD_DOWNLOADING,
	UPD_UNPACKING,    /* checking and unpacking the download */
	UPD_READY,        /* on the card, waiting to be installed */
	UPD_FAILED        /* the last step failed: upd_error says why */
} upd_state;

/* Where things go. card is P_CARD (the games partition's mount), root the
 * ChaOS folder, version the running one (CHAOS_VERSION), base the TortOS
 * release built on when ChaOS/BASE does not say. Once, at start. */
void upd_init(const char *card, const char *root, const char *version,
              const char *base);

/* Moves things along: reaps what finished, starts the next step. Called from
 * every loop that runs for long (the shelf, the Updates screen). `online`
 * starts the once-a-boot check. Returns true when the state changed. */
bool upd_tick(bool online);

upd_state   upd_get(void);
const ota_info *upd_latest(void);   /* valid from UPD_AVAILABLE on */
const char *upd_error(void);
const char *upd_base(void);         /* the base this card is on */
/* 0..100 while downloading or unpacking */
int         upd_percent(void);

/* What the player asks for. Each returns false, with upd_error set, when it
 * cannot be done now. */
bool upd_check(void);
bool upd_download(void);
void upd_cancel(void);               /* a download or unpack in progress */
bool upd_remove(void);               /* a finished download, unwanted */
/* The boot partition told to install it at the next boot. The caller then
 * restarts the device (launch.sh's /tmp/chaos_reboot). */
bool upd_arm(void);

/* Asked to check or download with no network: says which - the radio off,
 * or on and not connected - the way any other failure is said. */
void upd_offline(bool radio_off);

/* Once per newer version found by the boot's check: true the first time it
 * is asked, so the shelf says so once. */
bool upd_announce(char *out, int outn);

/* For screenshots (--updates): the screen in a state, with made-up facts. */
void upd_demo(upd_state s, const char *version, const char *error);

/* For the menu row: "0.8.0 available", "0.8.0 ready", "downloading 40%", or
 * NULL when there is nothing to say. */
const char *upd_row_value(void);

#endif
