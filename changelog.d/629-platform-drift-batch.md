### Fixed

- **`BuildPlatformBlock`'s zero-field list is complete** (#629): its comment in `sdk/go/platform.go` now also names flags, operator_bind and both reserved spans (bytes 6-7 and 164-189), the fields `include/tfw/boot.h` lays out and the function leaves zero.
- **`sdk/go/platform.go` grammar** (#629): "an tfw-boot" is now "a tfw-boot".
- **`tfw_boot_platform_serialise`'s write-back comment** (#629): `boot/boot_platform.h` no longer says the device serial is preserved. The serial is in the modelled span and is written from `in`; what survives is the flags byte, the reserved bytes and operator_bind.
- **The ATtiny85 soft-UART oscillator term** (#629): `docs/boards/attiny85.md` now uses ±10% × 9.5 = 95% of a bit, the figure in `boards/attiny85/board_profile.h`, not ±2%.
- **The ATtiny85 soft-UART total** (#629): the error budget now sums to ~105%, not ~30%.
- **The ATtiny85 soft-UART conclusion** (#629): the page now says what the profile says. At ±10% the link does not work at any baud, because the error is relative.
- **The boards engineering table's tier row** (#629): `docs/boards/engineering.md` now says atmega328p states `atecc608a` and refuses `soft`, and that only atmega32u4 takes `mk/firmware.mk`'s default.
- **The mps2-an505 registration** (#629): `docs/boards/engineering.md` drops the claim that 0x40 has no registry row, boardid.go constant or `PARTS` row. All three exist.
- **The nRF54L15 DK pin table** (#629): `docs/boards/nrf54l15-dk.md` now lists pins 8-11, AIN2-AIN5 on P1.6, P1.7, P1.11 and P1.12.
