### Fixed

- **tfw-boot's recovery session on AVR keeps its poll counter in SRAM**
  (#640), so the empty pass costs the 77 cycles `boot/hal/avr/boot_tick.h`
  counts on, whatever the rest of `serve` compiles to. After the framer's
  oversize-request refusal, avr-gcc moved the counter into registers, the
  ATmega2560 OTA image's pass fell to about 64 cycles, and the 10 s idle
  window closed at about 8.3 s, before the simavr "drip of valid OTA frames"
  case sent its frame at 8 s. The out-of-line framer call added for the same
  drift did not keep the counter out of registers and is removed: the boot
  section has no bytes to spare.
