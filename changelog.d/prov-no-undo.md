### Added

- **No provisioning tool can undo another's step, checked at the wire and in
  the object lists** (row device-provisioning, R5). A `tfwprov` test runs every
  wire mode (`--dry-run`, `--verify`, `--commit` on a blank, a config-locked
  and a locked part, and `--commit` refused at each step) against a recording
  element and fails on any frame whose id the firmware's command table gates on
  `TFW_FEATURE_EEPROM` and that is not a read. `tfwprov`'s device traffic is
  now one function, `provision`, so the test sees every frame the tool sends.
  `scripts/prov-absent-check.py` also asks make for the bench bake image's
  object list on every board and fails if it links a provisioning unit, a
  crypto tier's provider, the element's driver or the element's bus. No wire
  vector changed, no defect found.
