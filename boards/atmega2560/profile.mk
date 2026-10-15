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
# A quarter of the flash and an eighth of the SRAM; see board_profile.h for why
# the fractions are smaller than the 328P's and the headroom is still larger.
FLASH_BUDGET   := 65536
RAM_BUDGET     := 1024
FLASH_TOTAL    := 262144
RAM_TOTAL      := 8192

# tfw-boot's geometry. The Mega's largest BOOTSZ is 4096 words, so 8192 bytes --
# twice the 328P's. The gate follows the part rather than a constant. BOOT_BASE
# is where the linker places it: the top of flash, which mk/arch/avr.mk checks.
BOOT_SECTION   := 8192
BOOT_BASE      := 0x3e000
BOOT_BUDGET    := 8192
RAM_END        := 0x21FF

# The Ethernet controller an ETH=1 image drives (docs/ethernet-transport/): the
# Ethernet Shield Rev3's W5100 unless the command line says w5500 for Shield 2.
# The chip file checks the part's identity, so the wrong shield stays down.
ETH_CHIP       ?= w5100
