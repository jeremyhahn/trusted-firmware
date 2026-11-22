### Added

- **The DICE dispatcher's input byte-count check has a test** (#608). A
  read callback that copies one byte fewer than the input vector's size
  answers `TFW_ERR_BAD_ARGC` for the alias signature and the alias
  certificate's nonce and offset, and nothing is written.
