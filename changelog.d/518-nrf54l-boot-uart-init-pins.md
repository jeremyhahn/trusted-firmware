### Added

- **Host tests for what tfw-boot's nRF54L15 uart init programs** (#518). Over
  the nRF54L register model, `make host-test` now checks that init drives the
  console TX pin (P1.4) high as an output and makes RX (P1.5) an input with
  pull-up, routes TXD and RXD to those pins with CTS and RTS disconnected, sets
  the 115200 rate, an 8N1 frame and ENABLE, and that a send or an off on a
  UARTE that init left off starts no task.
