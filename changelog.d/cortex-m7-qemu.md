### Added

- **A Cortex-M7 on QEMU, `boards/mps2-an500`, board id `0x42`** (row
  `cortex-m`). Arm's AN500 as `qemu-system-arm -M mps2-an500` models it, with
  the same soft-tier tfw-boot row as `mps2-an505`. The two MPS2 images share
  the drivers in `src/hal/cortex-m/mps2/` and `boot/hal/cortex-m/mps2/` and
  the linker script `mk/arch/cortex-m/mps2.ld`; each board profile states its
  block addresses and UART interrupts. `make integration-test-cortex-m` runs
  its three cases on both boards (`integration-test-cortex-m-<board>` runs
  one), and `firmware-matrix` and CI's image job build both. `tfw.BoardMPS2AN500`
  joins `sdk/go`. `0x41` stays the i.MX 95 M7's; the unknown-board example
  in the registry and the Go test is `0x7f`.
