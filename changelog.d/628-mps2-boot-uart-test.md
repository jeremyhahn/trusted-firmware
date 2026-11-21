### Added

- **A host test pins tfw-boot's polled MPS2 UART** (#628). R21, tfw-boot enables
  no interrupt, was pinned only end to end in QEMU; `test_boot_mps2_uart` now
  runs `boot/hal/cortex-m/mps2/boot_uart.c` under mps2-an505's profile over a
  register model that sees every store: init enables TX and RX and never sets an
  interrupt enable, a baud below the minimum divisor leaves the UART off, tx on
  a disabled UART writes nothing, and the hand-over waits for the transmitter
  before it clears the UART and acknowledges both interrupts. `make boot-test`
  and `make host-test` run it.
