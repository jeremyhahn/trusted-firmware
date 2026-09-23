# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
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

# ag-boot's geometry. BOOT_SECTION is BOOTSZ in BYTES (2048 words); BOOT_BUDGET
# is the gate mk/boot.mk enforces, and it is the section size because there is
# nowhere else for a bootloader to go. RAM_END is the last SRAM address, used to
# place the CDI handoff block at the top of SRAM and the bootloader's stack
# below it.
BOOT_SECTION   := 4096
BOOT_BUDGET    := 4096
RAM_END        := 0x08FF
