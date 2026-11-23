### Fixed

- **`scripts/tfm-measure.sh` takes a relative work directory from where it is run** (#640).
  The work directory is made absolute before the clone, toolchain-file,
  `CONFIG_SPE_PATH` and log paths are built from it, so `../tfm-measure` no
  longer hands CMake paths it resolves against its own source or build
  directory.
