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

# Tfw-boot's geometry.
BOOT_SECTION   := 4096
BOOT_BASE      := 0x7000
BOOT_BUDGET    := 4096
RAM_END        := 0x08FF

# EESAVE, the fuse bit that keeps the EEPROM through a chip erase: high fuse bit 3 (ATmega328P data sheet, Memory Programming, the Fuse High Byte table).
BOOT_EESAVE_FUSE := hfuse
BOOT_EESAVE_BIT  := 3

# The lock bits that must read programmed (0) before the part is called provisioned: `make lock-check` reads the lock byte back and refuses otherwise (mk/arch/avr.mk, SPEC device-provisioning R6).
BOOT_LOCK_PROGRAMMED := 0:LB1 1:LB2 4:BLB11 5:BLB12

# The Ethernet controller an ETH=1 image drives (docs/ethernet-transport/): the Ethernet Shield Rev3's W5100 unless the command line says w5500 for Shield 2.
ETH_CHIP       ?= w5100

# The ATmega328P is supported only with a secure element: its one tier and its default are the element's, and CRYPTO=soft and CRYPTO=auto (which links the soft root beside the element) are refused here (mk/firmware.mk, BOARD_REFUSED_TIERS) rather than built.
CRYPTO         ?= atecc608a
BOARD_TIERS    := atecc608a
BOARD_REFUSED_TIERS := soft auto

# The external store the OTA stager writes into: its driver is src/store/$(OTA_STORE).c, linked by mk/firmware.mk when OTA_STAGE=1.
OTA_STORE      := spi_nor

# No boot-time rollback on this part (docs/SPEC.md): tfw-boot's store row keeps the signature check and the activation, and drops the restore from PREV and the confirm.
BOOT_PREV      ?= 0

# Its fallback has no PREV restore, so the application does not back up to PREV: mk/firmware.mk links ota_prev_none.c in place of ota_prev.c (docs/ota/architecture.md, "The 328P and 32U4 fallback from PREV").
OTA_NO_PREV    := 1

# The soft root's identity (mk/firmware.mk SOFT_ID): the HMAC signer, which links no P-256 code.
BOARD_SOFT_ID  := hmac
