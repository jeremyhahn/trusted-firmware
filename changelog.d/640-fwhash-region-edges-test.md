### Added

- **`scripts/test_fwhash_region_edges.py`, a test of the edges `scripts/fwhash.py` accepts** (#640): a region ending exactly at 4 GiB or exactly `MAX_FLASH` long, a record on the region's last byte, and blank lines, padding, CRLF, lowercase hex and an empty data record in the HEX. `make scripts-test` runs it.
