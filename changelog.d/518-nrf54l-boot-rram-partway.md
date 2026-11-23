### Added

- **tfw-boot's nRF54L15 RRAM write is tested for a READYNEXT that drops
  partway, a closing READY that never comes, a word-then-bytes tail and an
  empty write** (#518). A READYNEXT lost after the first word stops the write
  with only that word stored, a closing READY that never comes fails a write
  whose words were all stored, a 7-byte aligned write stores a word then three
  bytes at their own addresses, and an empty write stores nothing and leaves
  WEN clear.
