### Fixed

- **The secure-boot fail-closed table names every `TFW_ERR_UNSUPPORTED`
  source** (#640). `docs/secure-boot/engineering.md` now says
  `tfw_boot_trailer_parse` refuses a non-zero byte in the trailer's reserved
  range (offsets 112-127, outside the signed span), and gives the EEPROM
  layout-version check its own row under `tfw_boot_platform_parse`, the
  function that makes it.
