# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# ST's NUCLEO-H563ZI (MB1404): an STM32H563ZIT6, a Cortex-M33 with 2 MiB of
# flash and 640 KiB of SRAM and no crypto accelerators, behind an on-board
# ST-LINK/V3E (#14). SPEC row stm32h5's second target, data on the same
# stm32h5 SoC as boards/stm32h573i-dk; the board page is
# docs/boards/nucleo-h563zi.md, which marks every value not yet checked
# against UM3115 as TO CONFIRM.
#
# Board id 0x47, recorded in docs/boards/board-id-registry.md.
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := stm32h5
MCU            := stm32h563zit6

# No QEMU_MACHINE: QEMU models no STM32H5. CI builds it (the firmware matrix)
# and the host tests check its tables.

# The memory map, the STM32H573I-DK's: the same two 1 MiB flash banks at
# 0x08000000 and 0x08100000, and SRAM1, 256 KiB at 0x20000000, of which this
# port uses the same 32 KiB. tfw-boot's 32 KiB at flash's base, the
# application slot after it, the platform block in bank 1's last 8 KiB sector.
FLASH_BUDGET   := 131072
RAM_BUDGET     := 32768
FLASH_TOTAL    := 2097152
RAM_TOTAL      := 262144

APP_BASE       := 0x08008000
RAM_BASE       := 0x20000000
RAM_END        := 0x2003FFFF

# The core clock, TFW_CM_CPU_HZ in board_profile.h; the provenance manifest's
# f_cpu_hz reads it.
F_CPU          := 64000000

# The region tfw-boot measures: the application slot.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# tfw-boot's geometry, the STM32H573I-DK's. This board has no tfw-boot row
# yet (mk/boot.mk): the application only.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x08000000
BOOT_BUDGET    := 32768

# The soft root only: the part has no crypto accelerators and the board no
# element.
BOARD_TIERS    := soft

# Written through the board's ST-LINK/V3E with ST's STM32CubeProgrammer CLI
# (UM2237), as on the STM32H573I-DK.
CM_FLASH_TOOL  := stm32cubeprog
CM_FLASH_BASE  := 0x08000000
