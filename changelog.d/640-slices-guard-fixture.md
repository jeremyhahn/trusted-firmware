### Fixed

- **The remaining-slices guard no longer depends on planned test files** (#640).
  `scripts/test_board_remaining_slices.py` ran its matcher cases on this
  repository's tree, so a board slice that adds the test file it names (rp2350
  slice 5's `test/unit/boot/test_boot_rp2350_otp.c`) failed `make scripts-test`
  even with its page marked done. The cases now run on a scratch tree, and
  `proving_tests` reads the root `stale_items` is given.
