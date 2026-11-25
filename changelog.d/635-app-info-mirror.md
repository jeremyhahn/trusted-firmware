### Added

- **A scripts test pins the Go SDK's APP_INFO to the firmware** (#635).
  `scripts/test_app_info_mirror.py`, run by `make scripts-test`, fails when
  `sdk/go`'s AppState numbering, its String cases or the APP_INFO name,
  profile and total widths drift from `src/core/app_runner.h`,
  `include/tfw/app.h` and `include/tfw/protocol.h`.
