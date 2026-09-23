# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The programmer is `avr109`, not `arduino`. A Leonardo or a Micro has no
# auto-reset DTR line into an external bootloader: Caterina is a USB CDC
# bootloader that appears as a SECOND serial port for a few seconds after a
# 1200-baud touch, and avrdude's avr109 protocol is what speaks to it. Using
# the 328P's `arduino` programmer here fails with a sync error that reads like
# a wiring fault.
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
