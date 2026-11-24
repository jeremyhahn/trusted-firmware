### Changed

- **The ESP32-P4 soft tier's OTA trial, confirm and roll back rows are not
  applicable** (#563, owner ruling 1243, option (c)). The part has no secure
  element and the operator session links only on element tiers, so
  `TestFirmwareOTAActivate` and `TestFirmwareOTAConfirm` do not run there; the
  SPEC `ota` row, the board page and plan 552 say so, and the by-hand run of
  2026-11-19 stays the record.
- **The imx95 M7 console is LPUART7** (#474, owner ruling 1243, option (a)).
  The SPEC `imx95` row, the board page and the design's question 13 name
  LPUART7 on GPIO_IO36/37 at ALT2, the FRDM-IMX95-PRO's M7 console port; the
  image still drives LPUART3 until #474's firmware change lands.
