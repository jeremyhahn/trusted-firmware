### Added

- **`scripts/test_deps_check_pins.py`, a test of deps-check's pins, tag choice
  and exit codes** (#629). A pin's one pair of double quotes is stripped and
  other lines are skipped, the highest tag is numeric and never a pre-release,
  an official image is asked for under `library/`, and usage exits 2, a missing
  git or a failed or behind check exits 1. `make scripts-test` runs it.
