### Fixed

- **tfw-boot's session empty pass on the ATmega2560 OTA image is 77 cycles
  again** (#640), so the idle window is 10 s, not about 8 s. The recovery
  session's poll counter is now volatile and lives in SRAM, so the pass costs
  the 77 cycles `boot/hal/avr/boot_tick.h` counts on whatever the rest of
  `serve` compiles to. Before, avr-gcc kept the counter in registers, the pass
  fell to about 64 cycles and the window closed at about 8.3 s, before the
  simavr "drip of valid OTA frames" case sent its frame at 8 s.
