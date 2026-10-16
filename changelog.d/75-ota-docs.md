### Added

- **`docs/ota/`: engineering, API and user pages** (#75, #52) for the code on
  main: the stager and the boot rows with their object-list gating and tests,
  `OTA_BEGIN`/`OTA_DATA`/`OTA_END`/`OTA_STATUS` with their status codes and
  `sdk/go`'s `StageImage`, and staging an image on a 2560 with an external
  store. `OTA_CONFIRM`, `OTA_ACTIVATE` and `OTA_INFO` are stated as not on the
  wire. The SPEC `ota` row no longer lists the A/B swap-move as not yet done.
- **The Ethernet allow-list, as the code admits it** (#55, #75): the Ethernet
  pages and the wire ADR's decision 2 now list the four `OTA_*` ids beside
  `CAPS`, `APP_INFO` and the three attestation commands. The SPEC `ota` row
  records `tfwctl ota-stage <image>` as the name of the staging verb, not yet
  implemented.
