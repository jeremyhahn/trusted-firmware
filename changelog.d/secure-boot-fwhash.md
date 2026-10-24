### Added

- **The expected firmware hash, from source** (row `secure-boot`).
  `scripts/fwhash.py` takes the measurement tfw-boot takes of an application,
  SHA-256 over `[0, image_len)` of the built HEX with a gap read as erased
  flash (`0xFF`). One fixed image is pinned to the same literal in
  `scripts/test_fwhash.py` and in `test_boot_measure.c` through
  `tfw_boot_measure`. `scripts/test_reproducible.py`, in `make scripts-test`,
  builds the `atmega2560` soft row's application and tfw-boot twice from two
  clean copies of the tree with `SOURCE_DATE_EPOCH` set, and fails unless they
  are byte-identical. `docs/secure-boot/engineering.md` gains the row's
  requirements, met or unmet, with the file and test for each.
