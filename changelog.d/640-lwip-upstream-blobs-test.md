### Added

- **`scripts/lwip_upstream_check.py`'s cat-file reader has a test** (#640): an empty upstream file is read as empty, not missing; multi-line and binary blobs come back whole; a path or commit upstream lacks is missing; an absent repository is an error.
