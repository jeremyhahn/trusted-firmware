### Added

- test(imx95): the LPI2C master's read side is pinned by host tests: a
  target that stops sending times out with the missing bytes untouched, a
  read byte lost to another master answers BUSY with no STOP of its own, and
  a stale RDF over an empty receive FIFO is never taken as a byte
  (`test/unit/test_imx95_lpi2c.c`) (Refs #474).
