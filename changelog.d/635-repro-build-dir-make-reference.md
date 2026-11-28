### Fixed

- **`make reproducible-check` refuses a forwarded `BUILD_DIR` holding a make
  reference** (#635, `scripts/repro_build.py`). A value such as `$(OUT)` could
  expand to one directory for both clean builds, so it is refused before
  either copy is made.
