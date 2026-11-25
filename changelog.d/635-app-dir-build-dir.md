### Added

- **`scripts/test_build_dirs.py` pins that APP_DIR names the firmware build
  directory** (#635). An image with an application builds in
  `<tier>-app-<name>` on every board, apart from the image without one, so
  the two runners' objects never mix; a trailing slash on APP_DIR names the
  same directory, and an empty APP_DIR keeps the default path.
  `make scripts-test` runs it.
