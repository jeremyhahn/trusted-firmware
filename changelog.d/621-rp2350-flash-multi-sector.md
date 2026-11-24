### Added

- **A host test of the RP2350 tfw-boot flash helper over a three-sector span**
  (#621). A write whose middle sector already holds its bytes writes the first
  and third sectors only, in order, and every byte of the span lands.
