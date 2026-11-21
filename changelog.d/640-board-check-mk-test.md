### Added

- **`scripts/test_board_check_mk.py`, a test of board-check's profile.mk
  reader** (#640). `read_mk` keeps the first assignment, reads `:=`, `?=` and
  `=`, cuts a trailing comment and skips recipe, `export`, `+=` and comment
  lines; `mk_int` reads hex, octal and decimal and names a leading-zero,
  suffixed or empty value. `make scripts-test` runs it.
