# Cortex-M — engineering

> **Status: DRAFT** · Scope: trusted-firmware · Issue: #161 · Last verified: 2026-10-22

## Files

| File | Role |
|---|---|
| `mk/arch/cortex-m.mk` | The architecture: toolchain check, flags, RAM count, placement symbols, the flash refusal |
| `mk/arch/cortex-m-targets.mk` | `integration-test-cortex-m`, `integration-test-cortex-m-wire` and their QEMU runs; read on every invocation, so the tests need no `BOARD` |
| `mk/arch/cortex-m/mps2.ld` | The SoC's linker script, for both images and both boards; every address comes in as a symbol |
| `src/hal/cortex-m/cm_regs.h` | The core registers the port uses (VTOR, AIRCR, SysTick, NVIC) and the `tfw_cm_*` barrier and interrupt-mask helpers |
| `src/hal/cortex-m/hal_early.c`, `hal_early.h` | The vector table, the C reset handler and the fault handler both images link; the reset marker |
| `src/hal/cortex-m/hal_reset_decode.h`, `hal_fault_frame.h` | The marker's decode to a reset cause; the fault record's layout |
| `src/hal/cortex-m/hal_sys.c`, `hal_tick.c`, `hal_nvm.c`, `hal_handoff.c`, `hal_eeprom.c` | The core half of `include/tfw/hal.h`; `hal_eeprom.c` only with the stager |
| `src/hal/cortex-m/hal_types.h` | The architecture's HAL types |
| `src/hal/cortex-m/mps2/` | The SoC: `hal_uart.c`, `hal_gpio.c`, `hal_adc.c`, `hal_soc.c` (watchdog), `hal_bus.c` over `sbcon.c` (I2C) and `pl022.c` (SPI), `soc_regs.h` |
| `boot/hal/cortex-m/boot_hal_cortex-m.c` | tfw-boot's core half of `boot/boot_hal.h` |
| `boot/hal/cortex-m/boot_ota_store_cortex-m.c` | The store rows' memory image store |
| `boot/hal/cortex-m/boot_factory.c`, `boot_factory_none.c` | The two-slot rows' factory copy; empty on one slot |
| `boot/hal/cortex-m/recovery_halt.c` | The refusal on a row without OTA in boot: halt in WFI |
| `boot/hal/cortex-m/mps2/boot_uart.c` | tfw-boot's polled UART |
| `boards/mps2-an505/`, `boards/mps2-an500/` | `profile.mk` (map, budgets, tiers, rows), `board_profile.h` (id, features, regions), `board.c` (pin table) |
| `test/integration/cortex-m/cm_image.c` | The signer and jig: provisions tfw-boot, signs the application three ways, writes the platform block and, per row, the staged images |
| `test/integration/cortex-m/qemu_test.sh` | The three QEMU runs |
| `test/integration/cortex-m/qemu_wire_test.sh`, `qmp_pty.py`, `qmp_regs.py` | The wire suite's runs over a pty and QMP |
| `test/unit/test_board_mps2-an505.c`, `test_board_mps2-an500.c` | The pin tables on the host |
| `test/unit/test_mps2_bus.c`, `test_mps2_spi_row.c` | The bus driver over a simulated SBCon; the SPI=1 row's bus table |

## Invariants

- **No `#if` outside the four places.** The port adds none: every Cortex-M
  file is selected by `ARCH` and `SOC` in the make files, and
  `make lint-conditionals` holds it.
- **One reset path.** The vector table and the reset handler are
  `hal_early.c`, linked by both images. The reset handler touches no global
  until it has copied `.data` and zeroed `.bss`; the SoC's
  `tfw_cm_soc_early` (watchdog disarm) must touch none either.
- **Two images, one address formula.** The handoff block, the reset marker,
  the fault record and the stack top come from `RAM_END` by one formula in
  `mk/arch/cortex-m.mk`, and both links take them as symbols. Neither image's
  reset handler copies or zeroes them (`NOLOAD` sections).
- **The reset marker outlives one reset only.** Whoever causes a reset writes
  it first (`TFW_CM_MARK_WATCHDOG` when the watchdog is armed,
  `TFW_CM_MARK_RESETREQ` before AIRCR.SYSRESETREQ, `TFW_CM_MARK_PANIC` from
  the fault handler); the next reset handler reads and clears it, and
  `hal_reset_decode.h` maps it to `WATCHDOG`, `SOFTWARE` or `PANIC`. Anything
  else reads as power-on.
- **tfw-boot enables no interrupt.** Its UART polls and it starts no timer,
  so its vector table has no live entry the application could inherit, and
  the jump has only the UART to turn off.
- **The UART's first byte is written by `tfw_hal_uart_tx_try`.** The CMSDK
  UART interrupts when a byte leaves its one-byte buffer, not while it is
  empty, so `tx_busy` and the ring are read under the one critical section in
  the file. A full receive ring drops the byte; the framer resynchronises.
- **No `delay()`.** `tfw_hal_delay_us` counts SysTick's current value across
  reloads and is for bus bit timing only.
- **No allocator.** `tfw_hal_free_ram` measures from `__heap_start` to the
  stack.

## The toolchain

`docker/packages.txt` lists the Debian packages the devcontainer and the CI
image install:

| Package | For |
|---|---|
| `gcc-arm-none-eabi` | the compiler |
| `binutils-arm-none-eabi` | assembler, linker, `objcopy`, `nm`, `size` |
| `libnewlib-arm-none-eabi` | newlib-nano (`-specs=nano.specs -specs=nosys.specs`) |
| `qemu-system-arm` | the `mps2-an505` and `mps2-an500` machines |

`mk/arch/cortex-m.mk` checks for each tool only when a recipe needs it, so
`make board-tiers` and `make firmware-srcs` still answer for every board on a
host without the toolchain. A build that needs it stops with an error that
names the packages.

Flags: `-mcpu=$(CPU) -mthumb -mfloat-abi=soft`, `-Os -flto` with section GC,
`-nostartfiles` (the HAL is the startup code).

## `lint-arch-leak`

`mk/host.mk`'s `lint-arch-leak` keeps each architecture in its own
directories. Every `.c` and `.h` under `src`, `boot`, `tools`, `include` and
`test` is stripped of comments by the preprocessor (`-fpreprocessed -dD`) and
matched against two patterns:

- `ARCH_LEAK_PAT`, the AVR's: `<avr/`, `F_CPU`, `MCUSR`, a bare `PROGMEM`.
  Exempt: `src/hal/avr/`, `boot/hal/avr/`.
- `ARCH_LEAK_CM_PAT`, the Cortex-M's: `cm_regs.h`; CMSIS headers
  (`core_cm*.h`, `cmsis_*.h`); the `tfw_cm_*` helpers; `TFW_CM_REG`,
  `TFW_CM_VTOR`, `TFW_CM_AIRCR*`, `TFW_CM_SYST_*`, `TFW_CM_NVIC_*`; `SCB`,
  `NVIC`, `SysTick*`; `__DSB`, `__ISB`, `__DMB`, `__WFI`, `__WFE`,
  `__disable_irq`, `__enable_irq`; and an asm string beginning `dsb`, `isb`,
  `dmb`, `wfi`, `wfe`, `cpsie`, `cpsid`, `msr` or `mrs`. Exempt:
  `src/hal/cortex-m/`, `boot/hal/cortex-m/`.

`include/tfw/progmem.h` is exempt from both. A board constant such as
`TFW_CM_PLATFORM_BASE` is board data and is not matched. `lint-arch-leak-selftest`
plants each leak in a temporary tree and checks the gate fails on it, and that
prose in a comment and the exempt directories pass.

## `progmem.h` and far reads

`include/tfw/progmem.h` is the one toolchain shim. Off AVR, `TFW_PROGMEM` is
empty and the `tfw_pgm_*` reads are plain dereferences. The far reads,
`tfw_pgm_far_u8` and `tfw_pgm_far_u32`, are `volatile` dereferences. Two
tables tfw-boot holds, the P-256 key slot (`boot/verify/p256_key_slot.c`) and
the UDS (`boot/tier/cdi_soft.c`), are built all `0xFF` and written into the
image after the link. Under `-Os -flto` a plain read of a `const` table with a
visible initialiser folds to the initialiser, so the "erased" check would
become a constant and the provisioned bytes would never be read. AVR's ELPM
never had the problem.

## Tests

| Test | Target | What it pins |
|---|---|---|
| `test/unit/test_board_mps2-an505.c`, `test_board_mps2-an500.c` | `make board-sweep` | each board's id and name; two pins, both LEDs on port 0, bits 0 and 1; no ADC or PWM on any pin; a pin past the table is `TFW_ERR_BAD_PIN` |
| `test/unit/test_mps2_bus.c`, `test_mps2_spi_row.c` | `make host-test` | the I2C driver over a simulated SBCon; the SPI=1 row's bus table |
| `test/integration/cortex-m/qemu_test.sh` | `make integration-test-cortex-m` | genuine image answers `CAPS` across a soft reset; tampered and other-key images stay silent with QEMU still up |
| `test/integration/cortex-m/qemu_wire_test.sh` | `make integration-test-cortex-m-wire` | the tfwctl wire suite and the QEMU rows per board ([README](README.md)) |
| `scripts/test_qemu_wire_args.py` | `make scripts-test` | the modes `qemu_wire_test.sh` accepts |
| `firmware-matrix` | CI | both boards' rows build with every other board |

How the boot test works:

1. `cm_image` is built for the host with the board's profile, linking
   tfw-boot's own `boot_image.c`, `boot_platform.c`, SHA-256 and micro-ecc.
2. `cortex-m-qemu-image` converts both ELF files to raw images with `objcopy`
   and lists tfw-boot's symbols with `nm`. QEMU loads raw blobs because the
   signer edits both images.
3. `cm_image` makes a fresh P-256 key pair and UDS on every run (no private
   key or device secret is in the tree), writes them into tfw-boot's slots
   found by symbol, signs the application as the simavr harness does, and
   writes `app.bin`, `app-tampered.bin`, `app-otherkey.bin`, `platform.bin`
   and `addrs.env`.
4. `qemu_test.sh` starts each QEMU daemonized with the UART on a Unix socket
   and the blobs loaded at the addresses in `addrs.env`. `wirecheck`
   (`test/integration/esp32/wirecheck`) dials it with a bounded wait. Nothing
   sleeps.

CI runs both boards in the toolchain image, as their own steps:
`make firmware` and `make bootloader` for each board, then
`make integration-test-cortex-m (toolchain image)` and
`make integration-test-cortex-m-wire (toolchain image)`. Neither is in
`make ci`, so a red Cortex-M boot shows as its own step.

## Standards

The port implements Arm's architecture and calling convention and designs
nothing in their place. Both are in [`../reference/README.md`](../reference/README.md):
the AAPCS copied (`aapcs32-2025Q4.rst`), the Armv8-M manual listed under
"Cortex-M sources", as Arm's licence allows:

- *Armv8-M Architecture Reference Manual* (Arm DDI 0553): the vector table,
  VTOR, AIRCR, SysTick, NVIC, PRIMASK.
- *Procedure Call Standard for the Arm Architecture* (AAPCS, Arm IHI 0042):
  the 8-byte stack alignment at a call, the soft-float ABI.

The peripherals are as QEMU models them (`hw/arm/mps2-tz.c`, `hw/arm/mps2.c`,
`hw/arm/armsse.c`, cited in the boards' `profile.mk`).

## Open questions for the owner

From #161 and the reference-part ADR's open questions. Each is open; the code
takes the reversible option.

1. **Board ids.** `0x40` and `0x42` are recorded in
   [`../boards/board-id-registry.md`](../boards/board-id-registry.md), in the
   `0x40`–`0x7F` Cortex-M range. Which id does the first physical Cortex-M
   board take?
2. **MPU guard regions.** The port programs no MPU. Should stack-overflow
   and null-pointer guard regions, and a region that hides tfw-boot from the
   application, be in the next slice or follow the trust-model ADR?
3. **Soft-float ABI.** Every image builds `-mfloat-abi=soft`
   (`mk/arch/cortex-m.mk`). It costs one flag to change. Does it stand for the
   Cortex-M7 and Cortex-M33 parts with an FPU?
