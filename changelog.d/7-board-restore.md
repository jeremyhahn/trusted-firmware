### Added

- **`make board-restore`** (#7) puts a bench board back as it left the
  factory: the stock bootloader, then the fuse and lock bytes recorded on its
  page under `docs/development/bench/`, read back and compared. The hex is
  named by `BOARD_RESTORE_HEX`, since the stock bootloaders are GPL, and one
  whose SHA-256 is not the profile's `FACTORY_BOOT_HEX_SHA256` is refused. The
  flash write goes through `boot-flash`'s `EESAVE` guard. The Mega 2560's
  factory values are in its profile.

### Changed

- **The `EESAVE` refusal names the target that refused** and, for
  `boot-flash`, the row it was asked for, so the rerun it prints is the
  command to run.
