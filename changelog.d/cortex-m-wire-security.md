### Added

- **The dice, auth and attestcsr rows run on both QEMU Cortex-M boards** (row
  `cortex-m`, #161, #163). On a board, each of these rows asks CAPS for the one
  feature it needs. When the bit is clear, the row checks that the board refuses
  the command, instead of failing on the tier. On the simulator the row still
  requires its tier, so it never passes without running. A board without an
  alias certificate now takes its serial and firmware hash from the ATTEST
  evidence frame. `cm_image.c` also writes `boot-unprovisioned.bin`, the same
  boot with its UDS slot left erased. `qemu_wire_test.sh` boots each board a
  second time on it, for `TestFirmwareAttestRefusedUnprovisioned`.
  `docs/cortex-m/README.md` tables every suite row per board.
