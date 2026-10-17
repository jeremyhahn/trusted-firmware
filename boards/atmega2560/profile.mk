# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
ARCH           := avr
MCU            := atmega2560
F_CPU          := 16000000
AVRDUDE_PART   := m2560
# The Mega's bootloader is STK500v2, not the 328P's plain arduino protocol, and
# it enumerates as an ACM device through the on-board 16U2 rather than an FTDI.
AVRDUDE_PROG   := wiring
AVRDUDE_PORT   ?= /dev/ttyACM0
AVRDUDE_BAUD   := 115200
# A quarter of the flash and a quarter of the SRAM; see board_profile.h for why
# the fractions are smaller than the 328P's and why the RAM gate is 2048 (#55).
FLASH_BUDGET   := 65536
RAM_BUDGET     := 2048
FLASH_TOTAL    := 262144
RAM_TOTAL      := 8192

# tfw-boot's geometry. The Mega's largest BOOTSZ is 4096 words, so 8192 bytes --
# twice the 328P's. The gate follows the part rather than a constant. BOOT_BASE
# is where the linker places it: the top of flash, which mk/arch/avr.mk checks.
BOOT_SECTION   := 8192
BOOT_BASE      := 0x3e000
BOOT_BUDGET    := 8192
RAM_END        := 0x21FF

# EESAVE, the fuse bit that keeps the EEPROM through a chip erase: high fuse
# bit 3 (ATmega2560 data sheet, Memory Programming, the Fuse High Byte table).
# `make boot-flash` reads it before the erase; mk/arch/avr.mk.
BOOT_EESAVE_FUSE := hfuse
BOOT_EESAVE_BIT  := 3

# The factory state `make board-restore` puts back (mk/board-restore.mk): the
# bench Mega's fuse and lock bytes read over ISP before the project's first
# write to it, and the SHA-256 of Arduino's stk500boot_v2_mega2560.hex, the
# stock bootloader its boot section held. docs/development/bench/mega2560.md.
FACTORY_LFUSE := 0xff
FACTORY_HFUSE := 0xd8
FACTORY_EFUSE := 0xfd
FACTORY_LOCK  := 0xcf
FACTORY_BOOT_HEX_SHA256 := 081bd53f385df747b03bdafe7e154c1f72589f490be8138f3c4cc5517c93253b

# The Ethernet controller an ETH=1 image drives (docs/ethernet-transport/): the
# Ethernet Shield Rev3's W5100 unless the command line says w5500 for Shield 2.
# The chip file checks the part's identity, so the wrong shield stays down.
ETH_CHIP       ?= w5100

# ETH=1 is this board's default (owner ruling, issue #55 item 48): a Mega built
# with no ETH= is the Ethernet node. The shieldless bench Mega is the explicit
# ETH=0 row below, built and budgeted on every tier like the default rows.
ETH            ?= 1

# The runtime tier (owner ruling of 2026-10-17, issue #130 criterion 1): the
# element when one answers at init, else the soft root. The element is never
# required; docs/dice-attestation/architecture.md, "The Mega's runtime tier".
CRYPTO         ?= auto
# The default is a release image like the two fixed tiers: firmware-matrix,
# provenance and the size gate build it, the shieldless row as well.
BOARD_TIERS    := soft atecc608a auto
BOARD_ROWS     := soft:ETH=0 atecc608a:ETH=0 auto:ETH=0

# The SUIT manifest reader and gate (issue #78; mk/firmware.mk): the element-tier
# image carries src/core/suit.c. The 2560 has the room; ADR decision 6.
OTA_MANIFEST   := 1

# The external store the OTA stager writes into (issue #75, docs/ota/): its
# driver is src/store/$(OTA_STORE).c, linked by mk/firmware.mk when OTA_STAGE=1.
# The part and its chip select are ADR open question 2; the select is board
# data in board_profile.h (TFW_OTA_STORE_CS_PIN).
OTA_STORE      := spi_nor

# A/B by swap-move (ADR decision 7): the primary and secondary slots are board
# data in board_profile.h (TFW_SWAP_*). tfw-boot links boot/boot_swap.c where
# an image lands in the secondary, mk/boot.mk.
BOOT_AB        := 1
