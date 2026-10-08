# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The programmer is an ISP, not a serial bootloader, and that is a property of
# the part rather than a preference. An ATtiny85 has no boot-loader section and
# no BOOTRST fuse, so there is nowhere for a bootloader to live and nothing to
# jump to it -- every flash write comes in over SPI from an external programmer.
# `usbasp` is named because it is the common one; any ISP works.
ARCH           := avr
MCU            := attiny85
# 8 MHz from the internal RC oscillator, which is the part's own default with
# CKDIV8 unprogrammed. NOT 16 MHz: the 16 MHz option is the internal PLL, which
# is the same RC oscillator multiplied and therefore no more accurate, and the
# software UART's error budget is dominated by that oscillator rather than by
# the divisor. See TFW_UART_BAUD in board_profile.h for the arithmetic.
F_CPU          := 8000000
AVRDUDE_PART   := t85
AVRDUDE_PROG   := usbasp
AVRDUDE_PORT   ?= usb
# An ISP has no serial baud; avrdude ignores -b for usbasp. Set to the link's
# baud rather than left empty so `make flash`'s command line is well-formed, and
# said here so nobody reads it as the ISP clock -- that is -B, and the default is
# right for a part running at 8 MHz.
AVRDUDE_BAUD   := 19200

# A part shipped from the factory has CKDIV8 programmed and boots at 1 MHz.
# That is NOT a fuse that has to be changed: tfw_hal_init writes CLKPR = 0 as its
# first act, so the part is at the full oscillator rate before the software
# UART's bit clock is programmed. CKSEL is the fuse that matters, and the
# factory default -- the 8 MHz internal RC -- is the one F_CPU above assumes.
# Half the flash and half the SRAM, the same rule as every other board. There
# is no bootloader to subtract here -- the part has no boot section -- so the
# flash half is a half of the whole 8 KB.
#
# These are GATES, not estimates. If an image does not fit, the number to
# report is the overrun; the number to change is not this one.
FLASH_BUDGET   := 6144
RAM_BUDGET     := 256
FLASH_TOTAL    := 8192
RAM_TOTAL      := 512

# ONE CRYPTO TIER, AND IT IS `none`. Both the others are impossible here, and
# not by a margin that a smaller build would close:
#
#   soft       the software tier's definition is a key in BOOT-SECTION flash
#              behind BLB lock bits. This part has no boot section and no boot
#              lock bits, so there is nowhere to put one. An image carrying that
#              tier would report TFW_CRYPTO_SOFTWARE to a host deciding what the
#              device's evidence is worth, and the sentence that byte stands for
#              would be false.
#   atecc608a  the element is an I2C device. This part has a USI and no TWI.
#
# `none` is TFW_CRYPTO_NONE, which tfw_crypto_tier_t has named since the protocol
# was written. CAPS reports tier 0 and an empty algorithm bitmap, ATTEST is not
# in the command table, and a host learns all of that before it asks.
BOARD_TIERS    := none
CRYPTO         ?= none

# No BOOT_SECTION, BOOT_BUDGET or RAM_END, and their absence is deliberate.
# tfw-boot cannot exist on this part: there is no boot-loader section, so there
# is no read-while-write region to execute a flash write from and no lock bits
# that would hide a UDS from the application. mk/boot.mk's BOOT_SUPPORTED list
# does not name this board, and a `make bootloader BOARD=attiny85` is meant to
# fail on the missing geometry rather than produce an image that cannot run.
