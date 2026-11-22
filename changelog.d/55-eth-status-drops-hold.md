### Added

- **Host tests that an Ethernet request in between drops a serial OTA_AUTH hold** (#55).
  `test_ota_hold_link` shows that an OTA_STATUS the Ethernet list admits and the
  device dispatches, and an OTA_AUTH refused off that list, each leave a serial
  OTA_ACTIVATE that follows at `TFW_ERR_AUTH_REQUIRED` with the image unmarked.
