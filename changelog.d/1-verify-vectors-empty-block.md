### Fixed

- **`scripts/verify-vectors.sh` refuses an empty CRC, wire-frame or SYS_INFO
  block** (#1). When a pattern found no vector on either side, the script
  passed and printed a count of 1. It now fails with `no ... vectors found in
  testdata/vectors.h`, as its CAPS, APP_INFO, evidence and trailer blocks do.
