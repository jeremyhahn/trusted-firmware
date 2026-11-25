### Added

- **The nRF54L TWIM test checks that a data NACK sends RESUME before STOP**
  (#518). The model records the order of the STOP and RESUME task writes, so a
  driver that sends STOP before RESUME after a data NACK now fails the test.
