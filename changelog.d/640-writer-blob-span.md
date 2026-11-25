### Fixed

- **A test now holds a blob write to its n bytes** (#640). Nothing read the
  byte after a blob, so a `tfw_write_blob()` in `src/core/writer.c` that
  copied one byte too many passed every host test, and at an exact fill it
  would write one byte past the reply buffer. The new host test fails if the
  copy runs past n. Tests only; no wire format, status code or EEPROM layout
  changed.
