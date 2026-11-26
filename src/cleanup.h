/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CHAOS_CLEANUP_H
#define CHAOS_CLEANUP_H

#include <stdbool.h>

/* ChaOS: what older releases left on a card that nothing uses any more,
 * removed once per version (the first start after an update), and only what
 * is known to be ChaOS's own leftovers - never a save, a setting, a game, a
 * port or anything a player put there:
 *
 *   .userdata/<dev>/ports/ports.json      the old PortMaster screens' catalog
 *   .userdata/<dev>/ports/installed/      their install manifests, once their
 *                                         pictures were retired (ports/
 *                                         .img-retired, or no ports/img)
 *   .userdata/<dev>/ports/python.new      a Python unpack cut short
 *   .userdata/<dev>/ports/lib/libudev.so.1  a test copy: ChaOS ships its own
 *                                         for Weston ports, and one here was
 *                                         on every port's path
 *   Roms/Ports/.chaos-*.zip               the old screens' downloads, cut short
 *   Roms/Ports/PortMaster/libs/.chaos/X   an unpacked runtime whose image
 *                                         (libs/X.squashfs) is gone, and any
 *                                         unpack cut short (X.new)
 *
 * `userdata` is .userdata/<dev>, `roms` the card's Roms. The version done is
 * kept in userdata/cleanup; a later release adds to the list and runs again.
 * Returns how many things it removed, -1 if it had already run for
 * `version`. Logs each to stderr. */
int cleanup_run(const char *userdata, const char *roms, const char *version);

#endif
