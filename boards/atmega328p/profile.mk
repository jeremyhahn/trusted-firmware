# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
ARCH           := avr
MCU            := atmega328p
F_CPU          := 16000000
AVRDUDE_PART   := m328p
AVRDUDE_PROG   := arduino
AVRDUDE_PORT   ?= /dev/ttyUSB0
AVRDUDE_BAUD   := 115200
FLASH_BUDGET   := 16384
RAM_BUDGET     := 1024
FLASH_TOTAL    := 32768
RAM_TOTAL      := 2048

# tfw-boot's geometry. BOOT_SECTION is BOOTSZ in BYTES (2048 words); BOOT_BUDGET
# is the gate mk/boot.mk enforces, and it is the section size because there is
# nowhere else for a bootloader to go. BOOT_BASE is where the linker places it --
# TFW_BOOT_REGION_BASE in board_profile.h, the top of flash, which
# mk/arch/avr.mk checks. RAM_END is the last SRAM address, used to place the CDI
# handoff block at the top of SRAM and the bootloader's stack below it.
BOOT_SECTION   := 4096
BOOT_BASE      := 0x7000
BOOT_BUDGET    := 4096
RAM_END        := 0x08FF

# EESAVE, the fuse bit that keeps the EEPROM through a chip erase: high fuse
# bit 3 (ATmega328P data sheet, Memory Programming, the Fuse High Byte table).
# `make boot-flash` reads it before the erase; mk/arch/avr.mk.
BOOT_EESAVE_FUSE := hfuse
BOOT_EESAVE_BIT  := 3

# The Ethernet controller an ETH=1 image drives (docs/ethernet-transport/): the
# Ethernet Shield Rev3's W5100 unless the command line says w5500 for Shield 2.
# The chip file checks the part's identity, so the wrong shield stays down.
ETH_CHIP       ?= w5100

# The external store the OTA stager writes into (issue #75, docs/ota/): its
# driver is src/store/$(OTA_STORE).c, linked by mk/firmware.mk when OTA_STAGE=1.
# The part and its chip select are ADR open question 2; the select is board
# data in board_profile.h (TFW_OTA_STORE_CS_PIN).
OTA_STORE      := spi_nor
