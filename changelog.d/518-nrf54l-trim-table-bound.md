### Added

- **Host test for the nRF54L15's full FICR trim table** (#518). Over the
  nRF54L register model, `make host-test` now checks that early init copies all
  64 FICR TRIMCNF pairs and reads none past them; the test register file grows
  from 32 to 256 words to hold the table.
