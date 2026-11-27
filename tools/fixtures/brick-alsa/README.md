# The Brick's ALSA, for checks

ALSA as it is on the Brick, from TortOS 2.2.0's system image (the base ChaOS
runs on), for `make check-portaudio`:

- `lib/libasound.so.2`: alsa-lib 1.2.15.3, arm64, as the image has it
  (`/usr/lib/libasound.so.2.0.0`).
- `share/`: its configuration (`/usr/share/alsa`).
- `etc/asound.conf`: the Brick's own (plastron's
  `buildroot/board/brick/rootfs-overlay/etc/asound.conf`), the shared mixer
  everything plays through.

alsa-lib is LGPL-2.1-or-later; its source is at
https://github.com/alsa-project/alsa-lib (tag v1.2.15.3). Nothing here ships:
it is only run, under qemu-user, by the checks. When plastron's ALSA or its
`asound.conf` changes, copy the new ones here.
