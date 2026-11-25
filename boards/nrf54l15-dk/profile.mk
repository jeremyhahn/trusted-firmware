# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Nordic's nRF54L15 DK (PCA10156): an nRF54L15, whose application core is a Cortex-M33 at 128 MHz, on the generic Cortex-M port as its first silicon board, SPEC row nrf54l15.
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := nrf54l
MCU            := nrf54l15
# The generic script with the SoC's vector tail (mk/arch/cortex-m.mk).
CM_LDSCRIPT    := mk/arch/cortex-m/nrf54l.ld

# The memory map, every address the Secure alias the core sees out of reset (nrfx MDK nrf54l15_xxaa_application_memory.h):
FLASH_BUDGET   := 131072
RAM_BUDGET     := 16384
FLASH_TOTAL    := 1560576
RAM_TOTAL      := 262144

APP_BASE       := 0x00008000
RAM_BASE       := 0x20000000
RAM_END        := 0x2003FFFF

# The core clock, TFW_CM_CPU_HZ in board_profile.h.
F_CPU          := 128000000

# The region tfw-boot measures (mk/release.mk's provenance): the application slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h, which boot/hal/cortex-m reads by address.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

BOOT_SECTION   := 32768
BOOT_BASE      := 0x00000000
BOOT_BUDGET    := 32768

# The soft root only, as on mps2-an505: the KMU-held root is the later slice.
BOARD_TIERS    := soft

# I2C on TWIM21 (src/hal/cortex-m/nrf54l/hal_bus.c), off by default: the DK has no device on the bus.
I2C               ?= 0
BOARD_DEFS         = $(if $(filter 1,$(I2C)),-DTFW_CM_I2C=1)
BOARD_FEATURES_ON := $(if $(filter 1,$(I2C)),I2C)
BOARD_ROWS        := soft:I2C=1

# Written through the DK's on-board J-Link OB with SEGGER's J-Link Commander, whose savebin, loadbin and verifybin give a raw read-out and a verified write of the RRAM; mk/arch/cortex-m.mk holds the recipes.
CM_FLASH_TOOL    := jlink
CM_PROBE_TARGET  := NRF54L15_M33
CM_FLASH_BASE    := 0x00000000
CM_EXTRA_BASE    := 0x00FFD000
CM_EXTRA_BYTES   := 4096
