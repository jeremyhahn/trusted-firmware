### Fixed

- **`make reproducible-check` keeps the caller's command-line variables**
  (#473). `scripts/repro_build.py` passes them to both clean builds, so
  `IMX_BOOT=1 BUILD_DIR=build/imx95-imxboot` checks the i.MX 95 boot-container
  image instead of failing, and a `BUILD_DIR` outside the copy is refused. A `SOURCE_DATE_EPOCH` the
  calling make passes does not replace the commit's epoch.
