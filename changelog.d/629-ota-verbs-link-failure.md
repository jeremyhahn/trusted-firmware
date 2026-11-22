### Added

- **`tfwctl`'s OTA verbs and `attest-csr` have a dead-link test** (row
  `host-sdk`). `ota-stage` (with and without `--activate`), `ota-activate`,
  `ota-confirm`, `ota-info` and `attest-csr` are each pinned to return the
  SDK's `LinkError` over a link that has gone away, as every other wire verb
  already is.
