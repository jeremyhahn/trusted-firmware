### Added

- **The MPS2 bus test covers the SPI answer timeout** (row #628). A byte that
  goes out but whose answer never arrives is `TFW_ERR_TIMEOUT` after 2000
  polls, with the shield select released and the other MISC bits kept.
