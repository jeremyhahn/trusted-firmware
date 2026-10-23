### Added

- **`make boot-flash BOOT_FIRST_FLASH=1`: the first boot flash programs
  EESAVE, together with the factory record** (row device-provisioning, R7 and
  R8). Before it writes anything it reads every fuse byte, the lock byte, the
  EEPROM and the flash, and writes `build/factory-<board>-<UTC>.mk`, the
  `FACTORY_*` lines `make board-restore` reads, beside the two raw reads. Then
  it writes the profile's `BOOT_EESAVE_FUSE` with `BOOT_EESAVE_BIT` cleared and
  nothing else changed, reads it back, and refuses before the chip erase
  unless it holds. Without the flag `boot-flash` still refuses the erase while
  EESAVE is unprogrammed, and now names this route too. `make board-restore`
  refuses the flag. `scripts/test_boot_flash_eesave.py` runs each case against
  a stand-in `avrdude` on every board whose profile names EESAVE, under `make
  scripts-test`. No wire vector changed.
