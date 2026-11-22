### Fixed

- **Build-system comments name what exists on main** (row `host-sdk`, #629).
  Makefile and mk/ comments pointed at CI jobs and a workflow that no longer
  exist (`image-security`, `image-toolchain`, `nightly.yml`), a Go test file
  and field a reader would not find, the ESP32 app image as a map file, and
  three mk/arch facts that had moved; they now name scans.yml, ci.yml's `image`
  job, `e2e_pins_test.go`, `boardImage.drop`, the ESP-IDF ELF, mk/boot.mk's
  `boot-flash` and the FRDM-MCXN947's tfw-boot SoC directory. Comments only.
