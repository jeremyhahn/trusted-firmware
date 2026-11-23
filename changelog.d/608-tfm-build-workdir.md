### Fixed

- `scripts/tfm-build.sh` takes a relative work directory from the invocation
  directory, so `make tfm-image TFM_WORK=../x` configures (#608).
