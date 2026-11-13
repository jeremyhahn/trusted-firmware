# Cortex-M — API

> **Status: DRAFT** · Scope: trusted-firmware · Issue: #161 · Last verified: 2026-10-22

The port adds no command, no status code and no frame. A Cortex-M image speaks
the [wire protocol](../wire-protocol/) unchanged, and the host SDK talks to it
as to any other board. What this page lists is the port's half of two C
contracts: the application HAL in `include/tfw/hal.h` and tfw-boot's hardware
layer in `boot/boot_hal.h`.

## Wire

- `CAPS` reports board id `0x40` (mps2-an505) or `0x42` (mps2-an500) and
  crypto tier `1` (soft). Both are literals in `mk/arch/cortex-m-targets.mk`
  (`CM_QEMU_BOARD_ID.<board>`, `CM_QEMU_TIER_ID`), which the QEMU suite checks.
- The capabilities a board turns off in its `board_profile.h` are not in the
  image at all; `CAPS` reports the build, not the part. I2C and SPI are on
  only in the `I2C=1` and `SPI=1` rows.
- SYS_INFO's reset cause is `POWER_ON` after QEMU's reset, `SOFTWARE` after
  RESET, `PANIC` after a fault and `WATCHDOG` after the watchdog's bite
  ([architecture.md](architecture.md#boot-flow)).
- The console is UART0 at the build's `TFW_UART_BAUD`.

## Application HAL (`include/tfw/hal.h`)

`C` is the core directory `src/hal/cortex-m/`, `S` the SoC directory
`src/hal/cortex-m/mps2/`.

| Function | Where | Behaviour on the MPS2 boards |
|---|---|---|
| `tfw_hal_init` | C `hal_sys.c` | `TFW_OK`; the clocks are QEMU's at reset, nothing to program |
| `tfw_hal_reset_cause` | C `hal_sys.c` | tfw-boot's handed-off cause, else this image's latch |
| `tfw_hal_reset` | C `hal_sys.c` | `tfw_cm_system_reset`: the reset-request marker, then AIRCR.SYSRESETREQ, so tfw-boot runs first |
| `tfw_hal_irq_enable` | C `hal_sys.c` | clears PRIMASK |
| `tfw_hal_free_ram` | C `hal_sys.c` | the gap from the end of `.bss` to the stack |
| `tfw_hal_watchdog_arm`, `_pet` | C `hal_sys.c` → S `hal_soc.c` | the CMSDK watchdog at `TFW_MPS2_WDOG_BASE`, a 4 s period, interrupt and reset enabled, so the second expiry, 8 s after the last pet, resets the core; arming writes the `WATCHDOG` reset mark first |
| `tfw_hal_millis` | C `hal_tick.c` | SysTick at `TFW_CM_CPU_HZ / 1000`, a true millisecond |
| `tfw_hal_delay_us` | C `hal_tick.c` | bus bit timing only; counts SysTick's current value |
| `tfw_hal_nvm_read` | C `hal_nvm.c` | the platform block at `TFW_CM_PLATFORM_BASE`; `TFW_ERR_RANGE` past its 4 KiB |
| `tfw_hal_flash_read` | C `hal_nvm.c` | a plain copy: one address space |
| `tfw_hal_handoff_block` | C `hal_handoff.c` | the 112-byte `.handoff` section at `RAM_END - 111` |
| `tfw_hal_eeprom_read`, `_write` | C `hal_eeprom.c` | linked only with the stager (`OTA_STAGE=1`): the platform window past the block, where OTA_CONFIRM's request goes; a write into the block is refused |
| `tfw_hal_uart_init`, `_rx`, `_tx_try`, `_tx_idle` | S `hal_uart.c` | CMSDK UART0, interrupt-driven, a 128-byte receive ring and a 64-byte transmit ring; NVIC lines 32 and 33 on mps2-an505, 0 and 1 on mps2-an500 |
| `tfw_hal_gpio_mode`, `_write`, `_read` | S `hal_gpio.c` | the FPGA I/O LED register, two LEDs; output only, an input mode is `TFW_ERR_UNSUPPORTED`, another pin `TFW_ERR_BAD_PIN` |
| `tfw_hal_adc_init` | S `hal_adc.c` | `TFW_OK`: `main` calls it on every board |
| `tfw_hal_adc_set_ref` | S `hal_adc.c` | `TFW_ERR_UNSUPPORTED` |
| `tfw_hal_adc_read` | S `hal_adc.c` | `TFW_ERR_RANGE`: the boards have no channel |
| `tfw_hal_i2c_init`, `_xfer` | S `hal_bus.c` over `sbcon.c` | `I2C=1` only: a bit-banged master on the shield 0 SBCon ([README](README.md)) |
| `tfw_hal_spi_init`, `_xfer` | S `hal_bus.c` over `pl022.c` | `SPI=1` only: the shield 0 PL022, its select in the FPGA I/O MISC register ([README](README.md)) |
| `tfw_hal_ow_reset`, `_write`, `_read` | S `hal_bus.c` | `TFW_ERR_UNSUPPORTED` |

`tfw_hal_tick_init` is not in `hal.h`; `src/main.c` declares it and calls it
first, and `hal_tick.c` defines it.

### Left to a later slice

Not defined by this port. The board turns each feature off and
`mk/firmware.mk` drops the files that call them, so nothing references them
and the link needs none.

| Functions | Feature switch | Dropped |
|---|---|---|
| `tfw_hal_pwm_write`, `_stop` | `TFW_FEATURE_PWM 0` | `src/caps/pwm.c` |
| `tfw_hal_tone_start`, `_stop` | `TFW_FEATURE_TONE 0` | `src/caps/tone.c` |
| `tfw_hal_pulse_in`, `_capture` | `TFW_FEATURE_PULSE 0` | `src/caps/pulse.c` |

`TFW_FEATURE_EEPROM` is 0 as well on the silicon boards, so `src/caps/storage.c`
and the EEPROM commands are dropped and `hal_eeprom.c` serves only the stager.
The MPS2 boards turn it on: `TFW_EEPROM_SIZE` is the platform window's 4 KiB,
tfw-boot's span refused to `EEPROM_WRITE` by `hal_eeprom.c` as on the AVR. With I2C, SPI and
1-Wire all 0, the base image links `bus_none.c` instead of `src/caps/bus.c`.

## tfw-boot's hardware layer (`boot/boot_hal.h`)

`B` is `boot/hal/cortex-m/`.

| Function | Where | Behaviour |
|---|---|---|
| `tfw_boot_flash_read` | B `boot_hal_cortex-m.c` | a plain copy |
| `tfw_boot_flash_page_size` | B `boot_hal_cortex-m.c` | `TFW_OTA_PAGE_MAX` |
| `tfw_boot_flash_write_page` | B `boot_hal_cortex-m.c` | a copy into the application region only |
| `tfw_boot_nvm_read`, `_write` | B `boot_hal_cortex-m.c` | the platform block |
| `tfw_boot_handoff_write`, `_read` | B `boot_hal_cortex-m.c` | the same `.handoff` section the application names |
| `tfw_boot_reset_cause` | B `boot_hal_cortex-m.c` | the latch the shared reset handler decoded |
| `tfw_boot_jump_application` | B `boot_hal_cortex-m.c` | UART off, MSP from the slot's word 0, branch to word 1 |
| `tfw_boot_reset` | B `boot_hal_cortex-m.c` | `tfw_cm_system_reset` |
| `tfw_boot_watchdog_arm` | B `boot_hal_cortex-m.c` → `src/hal/cortex-m/mps2/hal_soc.c` | the same watchdog as the application's |
| `tfw_boot_store_open`, `_cmd`, `_close` | B `boot_ota_store_cortex-m.c` | the store rows (`…:0:1`): the memory image store, answering the SPI NOR opcodes `boot/boot_stage.c` sends ([README](README.md#the-image-store)) |
| `tfw_boot_uart_init`, `_rx`, `_tx` | B `mps2/boot_uart.c` | UART0 polled; no interrupt is enabled in tfw-boot |
| `tfw_boot_session_open` | B `recovery_halt.c` | does nothing: a row without OTA in boot has no session |
| `tfw_boot_recovery` | B `recovery_halt.c` | masks interrupts and sleeps in WFI; never returns |

The two-slot rows also link `boot/hal/cortex-m/boot_factory.c`, which copies
the factory application and platform block home once; the other rows link
`boot_factory_none.c`.

## Build interface

| Target | What it does |
|---|---|
| `make firmware BOARD=mps2-an505 CRYPTO=soft` | the application ELF (`I2C=1`, `SPI=1`, `OTA_STAGE=1`, `BOOT_AB=1` for the other rows) |
| `make bootloader BOARD=mps2-an505 BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=0` | tfw-boot, the base row; `BOOT_STORE=1` and `BOOT_AB=1` for the others |
| `make integration-test-cortex-m` | both images, signed and booted in QEMU ([user.md](user.md)) |
| `make integration-test-cortex-m-wire` | the tfwctl wire suite and the QEMU rows on both boards ([user.md](user.md)) |
| `make flash`, `make boot-flash` | exit 1: the board is emulated (`ARCH_CM_NOFLASH` in `mk/arch/cortex-m.mk`) |
