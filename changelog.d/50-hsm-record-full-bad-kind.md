### Added

- **A test pins that the A6 record refuses an undefined kind before it is
  full-checked** (#50, `docs/hsm-self-defence/`). An undefined kind appended to
  a full record answers `TFW_ERR_UNSUPPORTED` and leaves the dropped count, the
  entries refused for want of room, unchanged; moving the full check above the
  kind check in `src/core/hsm_record.c` now fails a test.
