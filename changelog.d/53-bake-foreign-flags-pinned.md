### Fixed

- tfwprov's refusal of `--config`, `--yes-i-mean-it`, `--baud` and `--timeout` is now tested for each flag with `--bake-eeprom`, with `--bake-netblock`, and with both modes together, and a test checks that a refused `--bake-eeprom --record` leaves no image and no record file behind (#53).
