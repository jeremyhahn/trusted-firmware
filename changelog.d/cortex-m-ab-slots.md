### Added

- **Two slots on the MPS2 boards** (#161). `BOOT_AB=1` builds mps2-an505 and
  mps2-an500 with a primary and a secondary slot, swap-move as on the
  ATmega2560, in the part of the 4 MiB code bank that QEMU does not load, so a
  swap, a trial and its confirm survive QEMU's reset. QEMU loads the
  application and the platform block at factory copies, and tfw-boot copies
  each to its home once while that home is blank. `mk/boot.mk` adds an A/B row
  per board (`…:0:1:ab`) with P-256 verify. `TFW_SWAP_UNIT` (one OTA page by
  default) lets a 128 KiB slot move in 512-byte units. The layout is in
  `docs/cortex-m/README.md`, "Two slots". No wire vector, status code, command
  id or board id changed. The QEMU wire run on the A/B build is in
  `cortex-m-ab-wire.md`.
