### Fixed

- **test_boot_rows_2560 is rebuilt when BOOT_SUPPORTED changes** (row #640).
  The test compiles mk/boot.mk's BOOT_SUPPORTED in, but did not depend on it,
  so after an edited or overridden matrix `make host-test` and `make
  boot-test` ran the old binary, and a forbidden row could pass the 2560's
  shed-OTA check. The binary now depends on a stamp holding the matrix, which
  is replaced only when the value changes: a changed matrix rebuilds it, an
  unchanged one rebuilds nothing.
