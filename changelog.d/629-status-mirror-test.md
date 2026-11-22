### Added

- **`scripts/test_status_mirror.py`, a test that `sdk/go/status.go` mirrors `include/tfw/status.h`** (#629). Every `tfw_status_t` value needs a Go `Status` constant with the same number and a `statusNames` entry, and every Go constant needs its C status, so a status added or renumbered on one side alone no longer ships as "unrecognised status N". `make scripts-test` runs it.
