### Changed

- **A host test pins the Cortex-M boot HAL** (#628). `make boot-test` now
  builds boot/hal/cortex-m/boot_hal_cortex-m.c under the MPS2 AN505's profile
  and checks that a page write below the application region, past its write
  end or misaligned is refused with no flash write, that an in-range page is
  written once, that the platform block's bounds hold and its writes land at
  its base, that the handoff block round trips, and that the factory copy runs
  before the reset cause is read. The jump to the application, the one
  function with Arm instructions in it, moves unchanged to
  boot/hal/cortex-m/boot_jump.c so the rest compiles on the host.
