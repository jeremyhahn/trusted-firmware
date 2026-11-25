# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The programmer is an ISP, not a serial bootloader, and that is a property of the part rather than a preference.
ARCH           := avr
MCU            := attiny85
# 8 MHz from the internal RC oscillator, which is the part's own default with CKDIV8 unprogrammed.
F_CPU          := 8000000
AVRDUDE_PART   := t85
AVRDUDE_PROG   := usbasp
AVRDUDE_PORT   ?= usb
# An ISP has no serial baud; avrdude ignores -b for usbasp.
AVRDUDE_BAUD   := 19200

# A part shipped from the factory has CKDIV8 programmed and boots at 1 MHz.
FLASH_BUDGET   := 6144
RAM_BUDGET     := 256
FLASH_TOTAL    := 8192
RAM_TOTAL      := 512

# One crypto tier, `none`: the others do not fit this part.
BOARD_TIERS    := none
CRYPTO         ?= none

# No BOOT_SECTION, BOOT_BASE, BOOT_BUDGET or RAM_END, and their absence is deliberate.
