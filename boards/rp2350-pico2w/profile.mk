# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Raspberry Pi Pico 2 W: an RP2350A with its two Cortex-M33 cores, 520 KiB of SRAM and 4 MiB of QSPI flash behind the execute-in-place window (SPEC row `rp2350`).
ARCH           := cortex-m
CPU            := cortex-m33
SOC            := rp2350

# The part scripts/board-check.py checks the totals and geometry against.
MCU            := rp2350
# The generic script with the boot ROM's IMAGE_DEF block and .ramfunc (mk/arch/cortex-m.mk).
CM_LDSCRIPT    := mk/arch/cortex-m/rp2350.ld

# The memory map ([RP-DS], "Address map"), core 0 only, Secure with the SAU off as the boot ROM leaves an Arm image:
FLASH_BUDGET   := 131072
RAM_BUDGET     := 65536
FLASH_TOTAL    := 4194304
RAM_TOTAL      := 524288

APP_BASE       := 0x10008000
RAM_BASE       := 0x20000000
RAM_END        := 0x2007FFFF

# The core clock, TFW_CM_CPU_HZ in board_profile.h.
F_CPU          := 12000000

# The region tfw-boot measures (mk/release.mk's provenance): the application slot, TFW_APP_REGION_BASE and TFW_APP_REGION_SIZE in board_profile.h, which boot/hal/cortex-m reads by address.
FWHASH_BASE    := $(APP_BASE)
FWHASH_SIZE    := $(FLASH_BUDGET)

# Tfw-boot's geometry.
BOOT_SECTION   := 32768
BOOT_BASE      := 0x10000000
BOOT_BUDGET    := 32768

# The soft root only until OTP is decided (the ADR's open questions 1-4): no element on this board and no OTP write.
BOARD_TIERS    := soft

# I2C on I2C0 (GPIO4 SDA, GPIO5 SCL), PWM, the converter, the tone and the flash EEPROM (src/hal/cortex-m/rp2350/), on by default.
I2C               ?= 1
PERIPH            ?= 1
BOARD_DEFS        = $(if $(filter 1,$(I2C)),-DTFW_CM_I2C=1) $(if $(filter 1,$(PERIPH)),-DTFW_CM_PERIPH=1)
BOARD_FEATURES_ON := $(if $(filter 1,$(I2C)),I2C) $(if $(filter 1,$(PERIPH)),ANALOG PWM TONE EEPROM)
BOARD_ROWS        := soft:I2C=0 soft:PERIPH=0

# Written over USB with picotool, the board in BOOTSEL mode, as the onboarding ADR has it: the Pico 2 W has no on-board debug probe.
CM_FLASH_TOOL  := picotool
CM_FLASH_BASE  := 0x10000000
