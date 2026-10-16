### Added

- **`tfwprov --bake-netblock`** (#7). Writes the Ethernet network block as an
  `.eep` image at `[0x0C0, 0x0E0)` from `--mac`, `--ip` (with a `/prefix` or
  `--mask`), `--gateway` and `--endpoint`, encoded by `NetConfig.MarshalBinary`.
  Given with `--bake-eeprom`, one image of both blocks, `[0x000, 0x0E0)`.

### Changed

- **`make boot-flash` never erases the EEPROM blind** (#7). It reads the fuse
  holding `EESAVE` (`BOOT_EESAVE_FUSE`/`BOOT_EESAVE_BIT` in each board's
  `profile.mk`) before the chip erase. Programmed, it flashes as before. Not
  programmed, it refuses and prints the `hfuse` write that programs it, or,
  with `BOOT_EEPROM_RESTORE=1`, saves the EEPROM under `build/`, flashes,
  writes it back with `-D` and compares the read-back.
