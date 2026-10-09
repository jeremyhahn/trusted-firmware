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
