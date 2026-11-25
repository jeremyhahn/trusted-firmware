# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# ST's NUCLEO-H563ZI (MB1404): an STM32H563ZIT6, a Cortex-M33 with 2 MiB of flash and 640 KiB of SRAM and no crypto accelerators, behind an on-board ST-LINK/V3E.
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := stm32h5
MCU            := stm32h563zit6

# No QEMU_MACHINE: QEMU models no STM32H5.

# TZ=1: the TrustZone-M split's secure and non-secure images (mk/arch/cortex-m-targets.mk tz-firmware), built and size-checked only; running them needs TZEN=1 and the bank 1 secure watermark, option bytes nothing in this tree writes.
TZ_MAP         := src/hal/cortex-m/tz/tz_map_nucleo_h563zi.h

# The memory map, the STM32H573I-DK's: the same two 1 MiB flash banks at 0x08000000 and 0x08100000, and SRAM1, 256 KiB at 0x20000000, of which this port uses the same 32 KiB.
FLASH_BUDGET   := 131072
RAM_BUDGET     := 32768
FLASH_TOTAL    := 2097152
RAM_TOTAL      := 262144

APP_BASE       := 0x08008000
RAM_BASE       := 0x20000000
RAM_END        := 0x2003FFFF

# The core clock, TFW_CM_CPU_HZ in board_profile.h; the provenance manifest's f_cpu_hz reads it.
F_CPU          := 64000000

# The region tfw-boot measures: the application slot.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# Tfw-boot's geometry, the STM32H573I-DK's.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x08000000
BOOT_BUDGET    := 32768

# The soft root only: the part has no crypto accelerators and the board no element.
BOARD_TIERS    := soft

# Written through the board's ST-LINK/V3E with ST's STM32CubeProgrammer CLI (UM2237), as on the STM32H573I-DK.
CM_FLASH_TOOL  := stm32cubeprog
CM_FLASH_BASE  := 0x08000000
