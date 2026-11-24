### Added

- **tfw-boot's nRF54L15 RRAM write is tested for a controller that is not
  ready, for the CONFIG it leaves and for the RRAM's end** (#518). A controller
  never READY at the start fails the write with nothing stored, RRAMC CONFIG
  gets back the value it had before the write both on success and on a
  timeout, and a span past the RRAM's end is refused with nothing stored
  while the last word is still taken.
