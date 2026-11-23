### Fixed

- **The DICE dispatcher refuses an out-vector no operation writes** (#608).
  The key and signature operations write out-vector 0 only and no operation
  writes a third or fourth: a message that passes one now answers
  `TFW_ERR_BAD_ARGC`, and nothing is written, instead of `TFW_OK` with that
  vector left empty.
