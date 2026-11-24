### Added

- **Host tests for a byte tfw-boot's nRF54L15 uart hands out with a line
  error, an off whose RXTO never comes, and a second init after off** (#518).
  They check that rx still hands out a byte that came with a line error, clears
  the error and arms the receive again; that off turns the UARTE off and
  restores the reset values when RXTO never comes, after which rx and tx do
  nothing; and that a second init after off enables the UARTE, arms the
  receive and lets rx and tx run again.
