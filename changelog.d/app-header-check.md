### Added

- **The SDK's public headers compile on their own** (row `application-sdk`).
  `make app-header-check`, run by `host-test`, compiles every header in
  `include/tfw/` as the whole of a translation unit, included twice, against
  every board under `boards/`, with an application's flags and warning set and
  no `src/` on the path. Two plants show it can fail: a header that needs
  `<stdint.h>` from its includer, and a unit that includes
  `src/core/app_runner.h`. `make app-header-check-selftest`, in the `security`
  stage, plants a header that is not self-contained in a copy and checks the
  gate fails and names it. No header needed a change; no wire vector changed.
