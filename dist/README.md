# What a ChaOS release is made from

`.github/workflows/release.yml` makes the release from this folder, TortOS's
release it is built on (`TORTOS_BASE`), and this repository, when a version in
`CHAOS_VERSION` is pushed that has no release yet.

`brick/` holds what is built outside GitHub, for the TrimUI Brick:

| File | What it is |
| :--- | :--- |
| `tortos.elf` | ChaOS's launcher, built from this commit's `src/` (`make -f mk/cross.mk build/tortos.elf DEVICE=brick VERSION=$(cat TORTOS_BASE)`, aarch64, against the libraries in TortOS's image) |
| `tortos-update` | The updater the image's root runs for an update, built from this commit's `tools/tortos-update/` to take ChaOS's images (`make -f mk/cross.mk build/tortos-update DEVICE=brick`, static, aarch64); `tools/chaos-image.sh` puts it in place of TortOS's |
| `diatom` | ChaOS-diatom, the emulator, built from `ChaOS-diatom-source.tar.gz` (`make PORT=brick`) |
| `ChaOS-diatom-source.tar.gz` | ChaOS-diatom's source at the commit `diatom` was built from (MPL-2.0) |
| `ChaOS-diatom-THIRD-PARTY.md` | What ChaOS-diatom is built from, and their licenses |

The release carries:

- `ChaOS-v<version>-brick.img.xz`: TortOS's image with ChaOS in it
  (`tools/chaos-image.sh`), and `SHA256SUMS`.
- `ChaOS-v<version>-brick-sources.tar`: the source of everything in the image
  (`tools/chaos-sources.sh`): TortOS's own sources archive for its release
  (plastron, Linux, Buildroot's packages and TortOS), unchanged, with this
  repository's source and ChaOS-diatom's beside it. Licenses in the image are
  in its `Licenses/` folder.
