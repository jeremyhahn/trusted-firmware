### Added

- **`tfwsim-manifest`, the simulator with OTA_ACTIVATE's SUIT manifest gate**
  (#52). `make tfwsim-manifest` builds it with `suit-emit`, and `make
  sdk-test` builds both first. The SDK's end-to-end tests for SPEC row ota
  drive it: an image the manifest names activates, one it does not name or
  one with no manifest is refused. When either binary is not built the tests
  fail, naming it and make tfwsim-manifest, as does every other launch or run
  error.
