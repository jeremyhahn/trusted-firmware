# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The programmer is `avr109`, not `arduino`. A Leonardo or a Micro has no
# auto-reset DTR line into an external bootloader: Caterina is a USB CDC
# bootloader that appears as a SECOND serial port for a few seconds after a
# 1200-baud touch, and avrdude's avr109 protocol is what speaks to it. Using
# the 328P's `arduino` programmer here fails with a sync error that reads like
# a wiring fault.
ARCH           := avr
MCU            := atmega32u4
F_CPU          := 16000000
AVRDUDE_PART   := m32u4
AVRDUDE_PROG   := avr109
AVRDUDE_PORT   ?= /dev/ttyACM0
AVRDUDE_BAUD   := 57600
# Half of the 28672 bytes Caterina leaves, and half of the SRAM. See the
# reasoning in board_profile.h, where the same two numbers gate the firmware.
FLASH_BUDGET   := 14336
RAM_BUDGET     := 1280
# The TRUE part sizes, used only to report what percentage of the silicon an
# image occupies. The bootloader is a board fact rather than a part fact, so it
# belongs in the budget above and not here.
FLASH_TOTAL    := 32768
RAM_TOTAL      := 2560

# tfw-boot's geometry. Flashing it here REPLACES Caterina, and with it the
# 1200-baud-touch reset the avr109 programmer above depends on -- recovery then
# needs an ISP. See board_profile.h. BOOT_BASE is where the linker places it:
# the top of flash, which mk/arch/avr.mk checks.
BOOT_SECTION   := 4096
BOOT_BASE      := 0x7000
BOOT_BUDGET    := 4096
RAM_END        := 0x0AFF

# EESAVE, the fuse bit that keeps the EEPROM through a chip erase: high fuse
# bit 3 (ATmega32U4 data sheet, Memory Programming, the Fuse High Byte table).
# `make boot-flash` reads it before the erase; mk/arch/avr.mk.
BOOT_EESAVE_FUSE := hfuse
BOOT_EESAVE_BIT  := 3

# The lock bits that must read programmed (0) before the part is called
# provisioned: `make lock-check` reads the lock byte back and refuses
# otherwise (mk/arch/avr.mk, SPEC device-provisioning R6). bit:name, from the
# ATmega32U4 data sheet (Atmel-7766J, docs/reference/README.md), §28.1 Table
# 28-1 for the bit positions and §27.4 Tables 27-2 and 27-3 for the modes: BLB1
# mode 3, so the application cannot read the boot section, and LB mode 3, so
# ISP cannot read flash or EEPROM. BLB0 stays unprogrammed: it guards the
# application section, and mode 3 would stop tfw-boot reading the image it
# verifies. The same 0x0C as the Mega. Today no soft tfw-boot fits this part's
# 4 KB section (mk/boot.mk BOOT_SUPPORTED has the element row only), so no UDS
# sits behind these bits yet; the value follows the data sheet regardless.
BOOT_LOCK_PROGRAMMED := 0:LB1 1:LB2 4:BLB11 5:BLB12

# The external store the OTA stager writes into (issue #75, docs/ota/): its
# driver is src/store/$(OTA_STORE).c, linked by mk/firmware.mk when OTA_STAGE=1.
# The part and its chip select are ADR open question 2; the select is board
# data in board_profile.h (TFW_OTA_STORE_CS_PIN).
OTA_STORE      := spi_nor

# The soft root's identity (mk/firmware.mk SOFT_ID): the HMAC signer for now.
# Whether the P-256 DeviceID fits beside the DFU bootloader is measured before
# this changes; hmac is the reversible choice.
BOARD_SOFT_ID  := hmac
