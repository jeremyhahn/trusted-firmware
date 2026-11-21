### Fixed

- **docs/device-provisioning/user.md** (#640): the baked EEPROM image is two
  runs, `[0x000, 0x0C0)` and `[0x140, 0x160)`, not `[0x000, 0x0E0)`;
  `PROVISION=1` compiles in seven provisioning handlers, not six; and
  `make lock-check` no longer claims to refuse the ATmega328P and ATmega32U4,
  whose profiles name their lock bits.
