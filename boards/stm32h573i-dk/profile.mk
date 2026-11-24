# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# ST's STM32H573I-DK Discovery kit: an STM32H573IIK3Q, a Cortex-M33 with 2 MB of flash and 640 KB of SRAM, behind an on-board ST-LINK/V3E.
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := stm32h5
MCU            := stm32h573iik3q

# No QEMU_MACHINE: QEMU models no STM32H5, so integration-test-cortex-m does not run this board.

# The memory map, every address the Non-secure alias, which is what the core sees with TrustZone off, the part's factory state (TZEN in the option bytes; ST's stm32h573xx.h, FLASH_BASE_NS and SRAM1_BASE_NS):
FLASH_BUDGET   := 131072
# ETH_MAC=1 adds the MAC's rings (12 KiB of 1536-byte buffers) and lwIP's heap and pbuf pool: 64 KiB of the 256 KiB bank.
RAM_BUDGET     := 32768
ifeq ($(ETH_MAC),1)
  RAM_BUDGET   := 65536
endif
FLASH_TOTAL    := 2097152
RAM_TOTAL      := 262144

APP_BASE       := 0x08008000
RAM_BASE       := 0x20000000
RAM_END        := 0x2003FFFF

# The core clock, TFW_CM_CPU_HZ in board_profile.h.
F_CPU          := 64000000

# The region tfw-boot measures (mk/release.mk's provenance): the application slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h, which boot/hal/cortex-m reads by address.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# Tfw-boot's geometry.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x08000000
BOOT_BUDGET    := 32768

# The soft root, as on mps2-an505: no element on the kit, and P-256 in software for the image signature (mk/boot.mk's BOOT_SUPPORTED row).
BOARD_TIERS    := soft stm32h5
ARCH_CRYPTO_TIERS      := stm32h5
ARCH_CRYPTO_WRAPS_SOFT := stm32h5
ARCH_CRYPTO_SRCS_stm32h5 = src/crypto/sha256.c src/crypto/soft/provider.c \
                           src/crypto/soft/uds_flash.c \
                           $(filter-out src/third_party/micro-ecc/uECC.c,$(SOFT_ID_SRCS)) \
                           src/crypto/soft/verify_p256.c src/third_party/micro-ecc/uECC.c \
                           src/crypto/stm32h5/h5_hash.c src/crypto/stm32h5/h5_pka.c \
                           src/crypto/stm32h5/provider.c

# I2C4 on the kit's PB8 SCL and PB9 SDA, the instance ST's BSP drives (src/hal/cortex-m/stm32h5/hal_bus.c), off by default: no element is fitted.
I2C               ?= 0
# ETH_PROBE=1 prints one console line at boot, "eth: phy addr=N id=XXXX:XXXX link=up|down", from an MDIO scan of the kit's Ethernet PHY.
ETH_PROBE         ?= 0
BOARD_DEFS         = $(if $(filter 1,$(I2C)),-DTFW_CM_I2C=1) \
                     $(if $(filter 1,$(ETH_PROBE)),-DTFW_CM_ETH_PROBE=1)
# Each -D above names the image directory too (mk/firmware.mk's ROW_FEATURE_SUFFIX: -i2c, -eth_probe), so switching ETH_PROBE without a clean rebuilds hal_uart.o and the objects that test the -D.
BOARD_FEATURES_ON := $(if $(filter 1,$(I2C)),I2C) \
                     $(if $(filter 1,$(ETH_PROBE)),ETH_PROBE)
BOARD_ROWS        := soft:I2C=1 soft:ETH_MAC=1

# Written through the kit's ST-LINK/V3E with ST's STM32CubeProgrammer CLI (UM2237), which reads and writes the STM32H5's flash and prints its option bytes; mk/arch/cortex-m.mk holds the recipes.
CM_FLASH_TOOL  := stm32cubeprog
CM_FLASH_BASE  := 0x08000000
