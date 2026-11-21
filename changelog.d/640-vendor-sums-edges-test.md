### Added

- **`scripts/test_vendor_sums_edges.py`, a test of the edges of `make lwip-check`** (#640): VERSION, README.tfw.md and SHA256SUMS are ours only at the top of the tree, a `./` name and blank lines in the list are read, and `scripts/vendor_sums.py` exits 2 on bad usage, 1 on a missing list or a changed byte and 0 with its match line. `make scripts-test` runs it.
