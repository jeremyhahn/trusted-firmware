### Added

- **tfw-boot's nRF54L15 RRAM write is tested with a length whose end wraps
  past 4 GiB** (#518). A write from the region end, or from four bytes below
  the RRAM end, whose length would carry the span past 2^32 is refused as a
  protected region, so nothing is stored and WEN is never set.
