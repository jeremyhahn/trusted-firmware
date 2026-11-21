### Added

- **`scripts/test_lwip_archive_blobs.py`, a test of the lwIP archive reader** (#640): `archive_blobs` in `scripts/lwip_upstream_check.py` keys a gzipped tar's or a zip's files by their path under the top directory however deep, and gives None for a top-level entry, a directory, a tar symlink and a missing path. `make scripts-test` runs it.
