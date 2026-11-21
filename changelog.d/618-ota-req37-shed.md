### Changed

- **The OTA audit names its tests for requirements 28, 37 and 45** (row `ota`,
  #618). `scripts/test_boot_shed_ota.py` pins that the ATmega2560's soft tier
  sheds OTA and the store and keeps the verifier: `make bootloader` refuses a
  soft OTA or store row and offers `atmega2560:soft:soft:p256-soft:0:0`.
  `docs/ota/engineering.md` marks the three rows done with their tests.
