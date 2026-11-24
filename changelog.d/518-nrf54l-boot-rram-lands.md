### Added

- **tfw-boot's nRF54L15 RRAM write is tested for what it stores and where**
  (#518). The boot test now records each store: an unaligned start is single
  bytes at their own addresses up to the word boundary and then aligned
  words, a source that is not word-aligned is stored as one whole word, and
  a write starting exactly at tfw-boot's region end is taken while one byte
  below it is refused with nothing stored.
