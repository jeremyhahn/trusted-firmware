### Added

- **The nRF54L TWIM test pins both wait bounds to the poll** (#518). STOPPED
  on the transfer wait's last poll is taken with no STOP sent, one poll later
  is a timeout; STOPPED on the STOP wait's last poll keeps the TWIM enabled,
  one poll later disables it and clears RX.MAXCNT.
