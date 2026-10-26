### Added

- **The OTA and app rows run on both QEMU Cortex-M boards** (row `cortex-m`,
  #161, #163, #52). CAPS has no bit for the OTA stager or for an application.
  On a board, each OTA row asks OTA_STATUS first. If the board answers
  `ERR_UNKNOWN_CMD`, the row checks that every OTA command is refused the same
  way. `OTAActivate` and `OTAConfirm` first check that a board without the
  operator session (CAPS bit 14) refuses AUTH_CHALLENGE. `AppReportsItsApplication`
  checks that APP_INFO is refused on an image with no application. On the
  simulator the rows still run in full. The QEMU image is unchanged. Neither
  MPS2 profile names an OTA store or two slots, and `BOOT_OTA=1` is not a
  supported tfw-boot row on either board. `docs/cortex-m/README.md` says why.
