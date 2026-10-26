### Added

- **The STM32H573I-DK on the Cortex-M port** (#14). `boards/stm32h573i-dk/`
  (board id `0x43`, the four user LEDs) and the `stm32h5` SoC under
  `src/hal/cortex-m/stm32h5/` and `boot/hal/cortex-m/stm32h5/`: the HSI at
  64 MHz with three flash wait states, the independent watchdog, GPIO, and the
  ST-LINK console on USART1 (PA9/PA10), interrupt-driven in the application and
  polled in tfw-boot. CI builds the firmware and its one tfw-boot row
  (`stm32h573i-dk:soft:soft:p256-soft:0:0`) as it builds `mps2-an505`'s. The
  arithmetic before every register write is tested on the host
  (`test/unit/test_stm32h5_calc.c`). Nothing has run on the board: `make
  flash` refuses it. Values are cited to ST's source repositories at one commit each
  (`docs/reference/README.md`, "Board sources").
- **tfw-boot programs the STM32H573I-DK's flash** (#14):
  `boot/hal/cortex-m/stm32h5/h5_flash.c` drives the flash interface. It unlocks
  with the keys, programs quad-words, erases 8 KiB sectors in either bank and
  maps the error flags to status codes. A matching write is skipped; an erased
  target is programmed only; anything else rewrites its sectors through a RAM
  copy. The slot and platform-block writes in `boot_hal_cortex-m.c` now call
  the SoC's `tfw_cm_boot_flash_write`. mps2-an505 keeps its copy. Host-tested
  against a modelled flash interface (`test/unit/boot/test_boot_stm32h5_flash.c`);
  not yet run on the board.

### Changed

- **The Cortex-M vector table holds 64 external interrupts** (#14), up from 48,
  for USART1's line 58. 80 words need VTOR's table on a 512-byte boundary,
  which every board's region base is.
- **`make lint` analyses each Cortex-M SoC under its own board's header** (#14):
  a `LINT_ARCH_RUNS` row is now `hal-dir:board-dir:soc`.
