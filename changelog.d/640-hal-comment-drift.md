### Fixed

- **Eleven comments in src/hal/ match the code again** (#640): the AVR PWM is
  phase-correct, not fast; the soft UART's late-edge fault samples 56 ticks
  into the bit instead of 104, and its two ring-size defaults are its only
  conditionals; the Makefile paths and variables are mk/firmware.mk's FW_SRCS
  and mk/arch/avr.mk's ARCH_HANDOFF_AT and ARCH_BOOT_HANDOFF_AT; hal_early.c
  is linked by both images and called only by tfw-boot; the nRF54L TWIM error
  helper neither stops nor waits; the i.MX95 ELE header lists every command it
  declares; the STM32H5 tone refuses rates past a third of the clock; and the
  MPS2 SPI select is named by the board table's pin 2.
