# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# ST's STM32H573I-DK Discovery kit: an STM32H573IIK3Q, a Cortex-M33 with 2 MB
# of flash and 640 KB of SRAM, behind an on-board ST-LINK/V3E (#14). The
# generic Cortex-M port's first real part; the board page is
# docs/boards/stm32h573i-dk.md, and every value below is cited there.
#
# Board id 0x43, recorded in docs/boards/board-id-registry.md.
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := stm32h5
MCU            := stm32h573iik3q

# No QEMU_MACHINE: QEMU models no STM32H5, so integration-test-cortex-m does
# not run this board. CI builds it (the firmware matrix) and the host tests
# check its tables and the SoC's arithmetic.

# The memory map, every address the Non-secure alias, which is what the core
# sees with TrustZone off, the part's factory state (TZEN in the option bytes;
# ST's stm32h573xx.h, FLASH_BASE_NS and SRAM1_BASE_NS):
#
#   0x08000000  flash bank 1,   tfw-boot's 32 KiB at the base, where the boot
#               1 MiB           address of a part with TrustZone off points
#                               the reset; the application slot after it,
#                               128 KiB of code; the platform block in the
#                               bank's last 8 KiB sector
#   0x08100000  flash bank 2,   the application's EEPROM in the bank's last
#               1 MiB           8 KiB sector (board_profile.h), erased while
#                               the code runs from bank 1
#   0x20000000  SRAM1, 256 KiB  .data, .bss, the stack, the reset marker and
#                               the handoff block at the top (RAM_END)
#
# Unlike mps2-an505's, the code regions are flash: a store to them is not a
# write. tfw-boot's writes to the slot and the platform block need the flash
# controller, which this slice does not drive (docs/boards/stm32h573i-dk.md).
FLASH_BUDGET   := 131072
RAM_BUDGET     := 32768
FLASH_TOTAL    := 2097152
RAM_TOTAL      := 262144

APP_BASE       := 0x08008000
RAM_BASE       := 0x20000000
RAM_END        := 0x2003FFFF

# The core clock, TFW_CM_CPU_HZ in board_profile.h. The build does not read it
# (the clock is the header's); the provenance manifest's f_cpu_hz does.
F_CPU          := 64000000

# The region tfw-boot measures (mk/release.mk's provenance): the application
# slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h, which
# boot/hal/cortex-m reads by address. Not flash 0, which is tfw-boot's region.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# tfw-boot's geometry. BOOT_BASE is where it links; mk/arch/cortex-m.mk checks
# its load end against BOOT_BASE + BOOT_SECTION. 32 KiB is four of the part's
# 8 KiB sectors, so the slot starts on a sector.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x08000000
BOOT_BUDGET    := 32768

# The soft root, as on mps2-an505: no element on the kit, and P-256 in
# software for the image signature (mk/boot.mk's BOOT_SUPPORTED row).
# CRYPTO=stm32h5 is the same root with the part's HASH and PKA under it (#544):
# src/crypto/stm32h5/provider.c wraps the soft provider, renamed as auto's is,
# and falls back to it wherever the hardware does not answer. Recursive, as
# SOFT_ID_SRCS is set after this file is read. Soft stays the default until
# the board slice confirms the register facts (docs/plans/544.md).
BOARD_TIERS    := soft stm32h5
ARCH_CRYPTO_TIERS      := stm32h5
ARCH_CRYPTO_WRAPS_SOFT := stm32h5
ARCH_CRYPTO_SRCS_stm32h5 = src/crypto/sha256.c src/crypto/soft/provider.c \
                           src/crypto/soft/uds_flash.c $(SOFT_ID_SRCS) \
                           src/crypto/stm32h5/h5_hash.c src/crypto/stm32h5/h5_pka.c \
                           src/crypto/stm32h5/provider.c

# I2C4 on the kit's PB8 SCL and PB9 SDA, the instance ST's BSP drives
# (src/hal/cortex-m/stm32h5/hal_bus.c), off by default: no element is fitted.
# I2C=1 turns TFW_FEATURE_I2C on (TFW_CM_I2C in board_profile.h) and links the
# bus; the soft:I2C=1 row builds that image and size-check measures it. Built
# and host-tested against a register model (test/unit/test_stm32h5_i2c.c), not
# run on the kit.
I2C               ?= 0
BOARD_DEFS         = $(if $(filter 1,$(I2C)),-DTFW_CM_I2C=1)
BOARD_FEATURES_ON := $(if $(filter 1,$(I2C)),I2C)
BOARD_ROWS        := soft:I2C=1

# Written through the kit's ST-LINK/V3E with ST's STM32CubeProgrammer CLI
# (UM2237), which reads and writes the STM32H5's flash and prints its option
# bytes; mk/arch/cortex-m.mk holds the recipes. The record is the whole 2 MiB
# from bank 1's base and the option bytes as printed.
CM_FLASH_TOOL  := stm32cubeprog
CM_FLASH_BASE  := 0x08000000
