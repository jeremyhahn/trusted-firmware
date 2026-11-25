### Fixed

- **`docs/wire-protocol/api.md` follows `commands.def` again** (#629). The
  command table lists `OTA_INFO`, `OTA_ACTIVATE`, `OTA_CONFIRM` and `OTA_AUTH`
  (0x6C-0x6F), says an application built with `OTA_STAGE` answers `OTA_BEGIN`
  to `OTA_STATUS`, and gives `EEPROMReservedEnd` as 0x140, not 0xC0.
