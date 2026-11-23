### Added

- **A host test pins the RP2350's clock bring-up and watchdog** (#621).
  `test_rp2350_hal_soc` runs `src/hal/cortex-m/rp2350/hal_soc.c`, which both
  images link, over a register model under `make boot-test` and `make
  host-test`: each clock and reset poll waits on its own status bit in order,
  counted rather than timed; clk_sys leaves before clk_ref moves onto the
  crystal; a second bring-up changes nothing; the watchdog arms for 8 s with
  the oscillators left running; and the reset cause hands its registers and
  the RESETREQ flag on.
