# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Arm's MPS2+ FPGA image AN505 (a Cortex-M33 in the IoT Kit subsystem), as QEMU
# models it: `qemu-system-arm -M mps2-an505`. The generic Cortex-M port's
# reference board and CI's only Cortex-M target (#161); the ADR is
# docs/cortex-m/adr-reference-part-and-emulation.md.
#
# Board id 0x40 is PROVISIONAL: the first id of a Cortex-M range, taken for this
# port's tests until docs/boards/board-id-registry.md records it. Nothing
# outside CI is built for it.
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := mps2-tz

# The machine integration-test-cortex-m starts (mk/arch/cortex-m-targets.mk).
QEMU_MACHINE   := mps2-an505

# The memory map, every address the Secure alias, which is what the core sees
# with the SAU off (QEMU, hw/arm/mps2-tz.c and hw/arm/armsse.c):
#
#   0x10000000  SSRAM-0, 4 MiB   tfw-boot's 32 KiB region at its base, where
#                                the IoT Kit's INITSVTOR0 points the reset
#   0x38000000  SSRAM-1 and -2,  the application slot at the base, 128 KiB of
#               4 MiB together   code; the platform block in the last 4 KiB
#   0x30000000  internal SRAM,   .data, .bss, the stack, the reset marker and
#               32 KiB           the handoff block at the top (RAM_END)
#
# The code regions are RAM that QEMU loads from the ELF files, so a write
# "to flash" is a store. QEMU copies the loaded images in again on every system
# reset, so only a store outside them (the platform block) survives a reset,
# and none survives a new QEMU.
FLASH_BUDGET   := 131072
RAM_BUDGET     := 16384
FLASH_TOTAL    := 4194304
RAM_TOTAL      := 32768

APP_BASE       := 0x38000000
RAM_BASE       := 0x30000000
RAM_END        := 0x30007FFF

# tfw-boot's geometry. BOOT_BASE is where it links; mk/arch/cortex-m.mk checks
# its load end against BOOT_BASE + BOOT_SECTION.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x10000000
BOOT_BUDGET    := 32768

# The soft root only: no element on this board, and P-256 in software for the
# image signature (mk/boot.mk's BOOT_SUPPORTED row).
BOARD_TIERS    := soft
