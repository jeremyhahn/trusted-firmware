### Fixed

- **`tfwctl app` refuses an argument** (#635). The verb takes
  none (`docs/host-sdk/cli.md`), but a trailing word such as `tfwctl app info`
  was dropped and `APP_INFO` sent. It now returns `usage: app` before the wire,
  as `ota-status` does.
