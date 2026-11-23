# ChaOS's boot files, for the TrimUI Brick Pro

A test build carries these when the repository has `PLASTRON_REV`
(tools/chaos-image.sh). Everything else in the image is TortOS's, built from
[plastron](https://github.com/ericreinsmidt/plastron) by Eric Reinsmidt; these
are ChaOS's changes on top of it (plastron at b897f7c), so one card boots both
the Brick and the Brick Pro, as fast as before:

| File | What | Where it goes |
|---|---|---|
| `u-boot-sunxi-with-spl.bin` | U-Boot's SPL with `source/0011-chaos-brick-pro-detect.patch` (it tells the Pro by its sticks answering on TWI3, in about 0.3 ms, sets the Pro's DRAM timings, and leaves "PRO!" or "BRIC" at 0x47f00000), then TortOS 2.2.0's own U-Boot unchanged from byte 40960 | the card, at 128 KiB |
| `boot.scr` | `source/boot.cmd` made with mkimage: loads `brickpro.dtb` when the SPL left "PRO!", else `brick.dtb` | the boot partition |
| `brickpro.dtb` | `source/sun50iw10p1-trimui-brickpro.dts`: the Brick's tree with the Pro's screen timings, backlight pin and TWI3 | the boot partition |
| `brick-pad.ko` | the Brick's buttons, with `source/0046-...patch`: on the Pro also L3, R3 and both sticks (ABS_X/Y, ABS_RX/RY); and `source/0048-...patch`: the Pro's Menu, Select, Start, M1 and M2 send what the Brick's do, as the Pro wires them to other pins | the root, /usr/lib/brick |
| `ImagePro.lz4` | the Brick Pro's kernel: TortOS 2.2.0's (plastron b897f7c, brick_defconfig, gcc 10) with `source/0047-ChaOS-the-Brick-Pro-s-panel-a-gh7003.patch`, the Pro's panel (it reads 70 03 0a; TrimUI's Pro U-Boot calls it gh7003, and without it the Pro's screen stays white). Version 4.9.191+, so TortOS's modules load as they are | the boot partition; `boot.scr` loads it only on a Brick Pro |

On a Brick, all of it does what TortOS's does, and it boots TortOS's own kernel. GPL-2.0 (U-Boot, Linux), as
the files they change.
