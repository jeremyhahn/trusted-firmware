### Added

- **tfw-boot's own `main()` under the mock HAL** (#77).
  `test/unit/boot/test_boot_main.c` links `boot/boot_main.c` with `main`
  renamed, on the host board's in-place row. It tests the trial, confirm,
  rollback and floor decisions as `main()` composes them:
  - a new image boots on trial, with the block written before the watchdog and
    the jump;
  - an unconfirmed trial fails at the next reset;
  - a confirmed image settles and raises the floor;
  - an image under the floor is refused;
  - the floor is never lowered;
  - a reset before each write still leaves a bootable image.

  No tfw-boot byte changed on any row. `docs/ota/engineering.md` now records
  two things: the open question of `OTA_ACTIVATE` and `OTA_CONFIRM` over TCP,
  and what the simavr harness lacks before it can run the 328P and 32U4 restore.
