### Changed

- **RP2350 checked against its datasheet** (row rp2350). Every SoC value in
  `src/hal/cortex-m/rp2350/` and `boot/hal/cortex-m/rp2350/` is checked
  against RP2350 datasheet release 8 (RP-008373-DS), section by section, in
  `docs/boards/rp2350-pico2w.md`. The Pico 2 W's flash is settled at 4 MiB
  from pico-sdk 2.1.1 `pico2_w.h` and the Pico 2 W datasheet. The platform
  block stays in the last sector at `0x103FF000`, and
  `test_boot_rp2350_map` pins the map by literal. `RT_FLAG_FUNC_ARM_SEC` is
  cited from pico-sdk. The XOSC x64 startup margin and leaving SRAM8-9 unused
  have no primary source; the page records why each is safe. tfw-boot's
  polled UART0 now cites the datasheet's UART chapter, and
  `test_boot_rp2350_uart` runs it over a register model
  (`boot/hal/cortex-m/rp2350/test/`). tfw-boot's sector write through the
  boot ROM now refuses a misaligned offset or one past the 4 MiB before it
  leaves XIP, and `test_boot_rp2350_rom` runs it over a model of the ROM's
  function table: the six lookups by code, the erase and program order, the
  refusals, and XIP back on after every path. Nothing has run on the board
  yet. No wire command, vector or board id changed.
