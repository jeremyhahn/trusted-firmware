### Changed

- **The ATmega328P's tfw-boot store row drops the `PREV` restore** (#52, row
  `ota`; owner queue 176, option (a)). The 328P keeps signature verification
  and activation and has no boot-time rollback: a failed activation is
  recovered by reflash. Rollback stays on the 2560 and 32U4 rows where it fits.
  `boards/atmega328p/profile.mk` sets `BOOT_PREV ?= 0`, `mk/boot.mk` links
  `boot_swap_none.c` in place of `boot_prev.c` for it, and
  `atmega328p:atecc608a:atecc608a:atecc608a:0:1` stays out of `BOOT_SUPPORTED`:
  it measured 1498 bytes over its 4096-byte boot section, and activation on the
  328P goes back to the owner. Recorded as a row limit in `docs/SPEC.md`. Pinned by
  `scripts/test_boot_prev_row.py`.
