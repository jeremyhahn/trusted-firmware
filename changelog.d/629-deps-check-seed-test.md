### Added

- **`scripts/test_deps_check_seed.py`, a test of deps-check's module cache
  seed** (#629). Unset it does nothing, a seed without cache/download is
  skipped, only @v go.mod and .info files are copied and a file already in the
  run's cache is kept. `make scripts-test` runs it.
