### Added

- **A test for `lwip_upstream_check`'s file list, byte offsets and usage** (#640): `scripts/test_lwip_upstream_compare.py` pins the vendored file order, the first differing byte a FAIL names (the shorter length for a prefix), the line-endings note, the usage exit 2 and an upper-case SHA-256 in archive mode.
