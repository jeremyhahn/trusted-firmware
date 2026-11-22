### Fixed

- **docs/secure-boot: `tfwctl ota-confirm` and `make bootloader-reproducible`
  described as main has them** (#640). user.md names `tfwctl ota-confirm`
  (it sends `OTA_AUTH`, then `OTA_CONFIRM`) instead of saying there is no
  verb; cli.md gives `BOOT_TIER`'s default as for `bootloader` (`atecc608a` on
  a 4 KB boot section, else `soft`), says each build is in a `git archive
  HEAD` copy without `build/`, and quotes the failure line `NOT REPRODUCIBLE:
  <image> differs between <dir-a> and <dir-b>`.
