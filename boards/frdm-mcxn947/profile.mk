# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# NXP FRDM-MCXN947: the MCXN947, a dual Cortex-M33 with the EdgeLock secure subsystem, of which this port runs core 0 only and drives no EdgeLock function (SPEC row mcxn947, docs/boards/frdm-mcxn947.md).
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := mcxn94x
# The part board-check reads the totals against (scripts/board-check.py PARTS).
MCU            := mcxn947

# No QEMU_MACHINE: QEMU models no MCX part, so integration-test-cortex-m does not run this board; `make flash` writes it through its probe (below).

# The memory map, every address the Secure alias, which is what core 0 sees out of reset with the SAU off (MCXNx4x Reference Manual, memory map chapter, section to confirm; the SDK's MCXN947_cm33_core0.h and its flash linker script, docs/reference/README.md):
FLASH_BUDGET   := 131072
RAM_BUDGET     := 16384
FLASH_TOTAL    := 2097152
RAM_TOTAL      := 319488

APP_BASE       := 0x10008000
RAM_BASE       := 0x30000000
RAM_END        := 0x3004DFFF

# The core clock, TFW_CM_CPU_HZ in board_profile.h.
F_CPU          := 12000000

# The region tfw-boot measures (mk/release.mk's provenance): the application slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h, which boot/hal/cortex-m reads by address.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# Tfw-boot's geometry.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x10000000
BOOT_BUDGET    := 32768

# The soft root only: the EdgeLock subsystem is not driven, and no element is fitted (SPEC row mcxn947).
BOARD_TIERS    := soft

# I2C on LP_FLEXCOMM2 as LPI2C2 (src/hal/cortex-m/mcxn94x/hal_bus.c), off by default: no element is fitted.
I2C               ?= 0
BOARD_DEFS         = $(if $(filter 1,$(I2C)),-DTFW_CM_I2C=1)
BOARD_FEATURES_ON := $(if $(filter 1,$(I2C)),I2C)
BOARD_ROWS        := soft:I2C=1

# Written through the board's on-board MCU-Link, a CMSIS-DAP probe, with pyOCD and NXP's CMSIS pack for the part (pyocd pack install mcxn947vdf); mk/arch/cortex-m.mk holds the recipes.
CM_FLASH_TOOL    := pyocd
CM_PROBE_TARGET  := mcxn947vdf
CM_FLASH_BASE    := 0x10000000
CM_NOEXTRA_WHY   := no protected-flash read: its place in the MCXN947 map is not cited yet
