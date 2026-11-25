### Added

- **A host test that an application built against an older ABI is refused**
  (#635). `test/unit/test_app_abi.c` sets the descriptor's `abi` one below
  `TFW_APP_ABI` and checks the runner refuses it before `tfw_app_init`, and
  that the current ABI starts.
