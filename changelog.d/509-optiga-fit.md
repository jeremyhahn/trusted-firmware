### Changed

- **OPTIGA Trust M fit table measured** (#509). The ADR's fit table
  (`docs/multi-arch/adr-optiga-trust-m-onboarding.md`) carries the measured
  text+data and data+bss of the `optiga` rows of atmega2560 (`ETH=0`, `ETH=1`)
  and mps2-an505 (`I2C=1`) and of their soft rows, from `make firmware` per
  matrix row; every row passes the budget gate.
