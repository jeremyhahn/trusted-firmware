### Added

- **No production image links provisioning, checked** (row
  device-provisioning). `scripts/prov-absent-check.py` reads the object list
  and feature defines of every board, tier, Ethernet variant, matrix row and the
  example application from make itself, with `PROVISION=0` and `PROVISION=1`. It
  fails if a production image links `src/caps/provision*.c` or
  `src/hal/*/prov_*.c` or defines `TFW_FEATURE_PROVISION`, if a jig image no
  longer links the wire (the check would be vacuous), or if a boot image links a
  provisioning unit. `make scripts-test` runs it. Nothing is compiled, no wire
  vector changed, no defect found.
