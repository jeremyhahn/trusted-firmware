# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The programmer is `avr109`, not `arduino`.
ARCH           := avr
MCU            := atmega32u4
F_CPU          := 16000000
AVRDUDE_PART   := m32u4
AVRDUDE_PROG   := avr109
AVRDUDE_PORT   ?= /dev/ttyACM0
AVRDUDE_BAUD   := 57600
# Half of the 28672 bytes Caterina leaves, and half of the SRAM.
FLASH_BUDGET   := 14336
RAM_BUDGET     := 1280
# The true part sizes, used only to report what percentage of the silicon an image occupies.
FLASH_TOTAL    := 32768
RAM_TOTAL      := 2560

# Tfw-boot's geometry.
BOOT_SECTION   := 4096
BOOT_BASE      := 0x7000
BOOT_BUDGET    := 4096
RAM_END        := 0x0AFF

# EESAVE, the fuse bit that keeps the EEPROM through a chip erase: high fuse bit 3 (ATmega32U4 data sheet, Memory Programming, the Fuse High Byte table).
BOOT_EESAVE_FUSE := hfuse
BOOT_EESAVE_BIT  := 3

# The lock bits that must read programmed (0) before the part is called provisioned: `make lock-check` reads the lock byte back and refuses otherwise (mk/arch/avr.mk, SPEC device-provisioning R6).
BOOT_LOCK_PROGRAMMED := 0:LB1 1:LB2 4:BLB11 5:BLB12

# The Ethernet controller an ETH=1 image drives (docs/ethernet-transport/): the Ethernet Shield Rev3's W5100 unless the command line says w5500 for Shield 2.
ETH_CHIP       ?= w5100

# The external store the OTA stager writes into: its driver is src/store/$(OTA_STORE).c, linked by mk/firmware.mk when OTA_STAGE=1.
OTA_STORE      := spi_nor

# The soft root's identity (mk/firmware.mk SOFT_ID): the HMAC signer for now.
BOARD_SOFT_ID  := hmac
