### Added

- **A host test pins the MPS2 LED write's interrupt mask** (#628): `test/unit/test_mps2_gpio.c`, run by `make host-test`, adds `test_write_touches_the_led_word_only_with_interrupts_masked`, `test_write_restores_an_unmasked_caller` and `test_write_keeps_a_masked_caller_masked`, so removing the mask around the FPGAIO LED read-modify-write in `src/hal/cortex-m/mps2/hal_gpio.c` (where the tone's TIMER0 edge would be lost) fails.
