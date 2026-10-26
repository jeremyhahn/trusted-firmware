### Added

- **The soft tier's UDS, placed at flash time and never kept** (#7).
  `make boot-flash-uds`, on the soft row, takes 32 bytes from the host's
  CSPRNG and puts them into `uds_slot` of a temporary copy of the bench image,
  in a directory of its own under `$TMPDIR`. It then has `boot-flash` write
  that copy over ISP, with every check `boot-flash` makes. An EXIT trap deletes
  the copy whether the flash passes or fails. Only the flashed image's SHA-256
  is printed, and it is added to `uds-flashed.sha256` once the flash has
  passed; the UDS is never printed or kept. `boot-flash` takes
  `BOOT_FLASH_HEX`, which defaults to the build's image as before.
  `scripts/test_boot_flash_uds.py`, in `make scripts-test`, runs the target
  against a stand-in `avrdude` and touches no programmer. No wire vector
  changed.
