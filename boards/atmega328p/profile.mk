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

# The lock bits that must read programmed (0) before the part is called
# provisioned: `make lock-check` reads the lock byte back and refuses
# otherwise (mk/arch/avr.mk, SPEC device-provisioning R6). bit:name, from the
# ATmega328P data sheet (DS40002061B, docs/reference/README.md), §28.1 Table
# 28-1 for the bit positions and §27.5 Tables 27-2 and 27-3 for the modes: BLB1
# mode 3, so the application cannot read the boot section, and LB mode 3, so
# ISP cannot read flash or EEPROM. BLB0 stays unprogrammed: it guards the
# application section, and mode 3 would stop tfw-boot reading the image it
# verifies. The same 0x0C as the Mega. Today no soft tfw-boot fits this part's
# 4 KB section (mk/boot.mk BOOT_SUPPORTED has the element row only), so no UDS
# sits behind these bits yet; the value follows the data sheet regardless.
BOOT_LOCK_PROGRAMMED := 0:LB1 1:LB2 4:BLB11 5:BLB12

# The Ethernet controller an ETH=1 image drives (docs/ethernet-transport/): the
# Ethernet Shield Rev3's W5100 unless the command line says w5500 for Shield 2.
# The chip file checks the part's identity, so the wrong shield stays down.
ETH_CHIP       ?= w5100

# The ATmega328P is supported only with a secure element (owner ruling
# 2026-10-17, m522, item 79): its one tier and its default are the element's,
# and CRYPTO=soft is refused here (mk/firmware.mk, BOARD_REFUSED_TIERS) rather
# than built. That withdraws the soft-crypto Ethernet node the profile once
# carried as BOARD_ROWS (issue #55 item 48). Without an element answering at
# init the image still runs, and ATTEST answers TFW_ERR_NOT_PROVISIONED.
CRYPTO         ?= atecc608a
BOARD_TIERS    := atecc608a
BOARD_REFUSED_TIERS := soft

# The external store the OTA stager writes into (issue #75, docs/ota/): its
# driver is src/store/$(OTA_STORE).c, linked by mk/firmware.mk when OTA_STAGE=1.
# The part and its chip select are ADR open question 2; the select is board
# data in board_profile.h (TFW_OTA_STORE_CS_PIN).
OTA_STORE      := spi_nor

# No boot-time rollback on this part (owner queue 176, option (a); docs/SPEC.md):
# tfw-boot's store row keeps the signature check and the activation, and drops
# the restore from PREV and the confirm. A failed activation is recovered by
# reflash. mk/boot.mk links boot_swap_none.c in place of boot_prev.c.
BOOT_PREV      ?= 0

# The soft root's identity (mk/firmware.mk SOFT_ID): the HMAC signer, which links
# no P-256 code. The P-256 DeviceID on this part's soft row is the owner's call
# (item 79); until it is made, this row does not change.
BOARD_SOFT_ID  := hmac
