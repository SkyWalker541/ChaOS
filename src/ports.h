/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_PORTS_H
#define CHAOS_PORTS_H

#include <stdbool.h>
#include <stddef.h>

/* ChaOS: ports - PC games rebuilt for handhelds, packaged by PortMaster
 * (portmaster.games, MIT), each a folder of game files and a script beside it
 * in Roms/Ports. The Ports shelf lists the scripts; one runs with bash, after
 * PortMaster's own files have said what this device is (control.txt and
 * device_info.txt) and ChaOS's have said the rest (mod_ChaOS.txt: the pad, the
 * screen).
 *
 * The layout is PortMaster's standard one, so its own app installs here
 * unchanged: the root has /roms -> /mnt/SDCARD/Roms and a /bin/bash
 * (tools/chaos-image.sh), and the card is case-blind, so /roms/ports is
 * Roms/Ports.
 *
 * PortMaster is not on the card to begin with: it is fetched, once, the first
 * time a port is started (ports_setup_*), from PortMaster's own releases on
 * GitHub, and keeps itself up to date after that. */

/* systems.cfg's core field for the Ports shelf: no libretro core, a port */
#define PORTS_CORE "port"

/* PortMaster's latest release, the whole of it */
#define PORTS_PM_URL \
	"https://github.com/PortsMaster/PortMaster-GUI/releases/latest/download/PortMaster.zip"

/* Where things are, under `roms` (P_ROMS on the device) */
void ports_pm_dir(const char *roms, char *out, size_t n);   /* Roms/Ports/PortMaster */
/* Whether PortMaster is there to run a port with: its control.txt */
bool ports_pm_ready(const char *roms);

/* A port's script, looked at before it runs: false, with why, for one that
 * isn't a shell script at all (a stray file the shelf picked up). */
bool ports_script_ok(const char *path, char *why, size_t n);

/* ---- fetching PortMaster, a step a frame ------------------------------- */

typedef enum {
	PS_IDLE, PS_DOWNLOAD, PS_UNPACK, PS_DONE, PS_FAILED
} ports_setup_state;

/* Where from, instead of PORTS_PM_URL: for tools/ports-check.c, which hands
 * it a file:// zip */
void ports_setup_url(const char *url);
/* Starts the download into Roms/Ports. False if it couldn't start. */
bool ports_setup_begin(const char *roms);
/* One step; never waits. The state it is in after. */
ports_setup_state ports_setup_step(void);
void ports_setup_cancel(void);
/* Bytes downloaded so far, for the panel */
long ports_setup_bytes(void);
const char *ports_setup_problem(void);

#endif
