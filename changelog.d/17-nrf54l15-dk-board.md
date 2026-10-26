### Added

- **The nRF54L15-DK on the Cortex-M port** (#17). `boards/nrf54l15-dk` and
  the SoC in `src/hal/cortex-m/nrf54l/`, with the linker script
  `mk/arch/cortex-m/nrf54l.ld`: the vendor's reset-time setup less TrustZone
  and the KMU, the CPU at 128 MHz, WDT30, and the console on UARTE20 by
  one-byte EasyDMA. Application only, soft tier, board id `0x45`. `firmware-matrix` builds it, and the CI image job builds it
  in the toolchain image. It has not run on the board: tfw-boot has no nRF54L
  row yet. Host tests check the pin table and the baud, pin, PSEL and
  revision tables against the vendor's literals. No wire command or vector
  changed.
