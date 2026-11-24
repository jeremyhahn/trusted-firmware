### Added

- **The nRF54L TWIM test pins a disabled re-init and a hung write's TX count**
  (#518). The model records ENABLE at each PSEL and FREQUENCY write: a re-init
  of an enabled TWIM must disable it before the pins and rate are written, and
  a write whose STOP never takes must clear TX.MAXCNT as well as RX.MAXCNT.
