# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The AVR architecture: the one file that knows what an AVR toolchain is called,
# what it is told, how its images are measured and written, and where its linker
# is asked to put the CDI handoff block.
#
# Included by mk/firmware.mk, after the board's profile.mk has said ARCH := avr
# -- so MCU, F_CPU, RAM_END and the rest of the profile are already defined here.
# Nothing outside mk/arch/ names a toolchain binary, a -m flag or an avrdude
# switch. Adding an architecture is adding a sibling of this file, a
# src/hal/<arch>/ and a boot/hal/<arch>/ -- see docs/boards/adding-a-board.md.
#
# The contract with mk/firmware.mk and mk/boot.mk. Every name below is required,
# and a new architecture that leaves one out fails at the first use of it:
#
#   ARCH_CC, ARCH_OBJCOPY, ARCH_SIZE   the toolchain binaries
#   ARCH_LIBC                          named in the SBOM
#   ARCH_MACH_CFLAGS                   which machine, and the clock it runs at
#   ARCH_OPT_CFLAGS                    optimisation and section/LTO policy
#   ARCH_LDFLAGS                       the link flags every image shares
#   ARCH_FW_PLACEMENT_LDFLAGS          where the application puts the handoff
#   ARCH_BOOT_PLACEMENT_LDFLAGS        where tfw-boot puts .text and the handoff
#   ARCH_FLASH, ARCH_BOOT_FLASH        recipes that write the image to a part

ARCH_CC      := avr-gcc
ARCH_OBJCOPY := avr-objcopy
ARCH_SIZE    := avr-size
ARCH_LIBC    := avr-libc

# -flto and --gc-sections together are what make link-time HAL substitution pay
# off: an unused peripheral's code is not merely unreferenced, it is gone, and
# tfw_hal_gpio_write inlines to a two-instruction sbi at its call site.
ARCH_MACH_CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU)UL
ARCH_OPT_CFLAGS  := -Os -flto -ffunction-sections -fdata-sections \
                    -fno-fat-lto-objects

ARCH_LDFLAGS := -mmcu=$(MCU) -Os -flto -Wl,--gc-sections -Wl,--relax

# The CDI handoff block's SRAM address, and the stack kept below it -- issue
# #6/#18. THE SAME FORMULA for the application and for tfw-boot, because the two
# images must land on the identical address with no shared linker script to
# enforce it; one formula, derived once from the board's own RAM_END, is the only
# way the two cannot drift apart on it. Without the stack move the C runtime's
# default __stack -- the physical top of SRAM -- puts the application's own stack
# exactly where the handoff block lives, and the first deep call overwrites the
# CDI before tfw_boot_handoff_init ever reads it.
#
# Guarded on RAM_END because not every board has a bootloader to hand anything
# off, and the ATtiny85's profile.mk states so by defining neither.
#
# `.handoff`, NOT the generic `.noinit` output section -- src/hal/avr/
# hal_early.c's tfw_hal_early_flags lives in ordinary (unpinned) `.noinit` too,
# for the same "must survive past .init3" reason, and pinning the generic
# section's address would carry every object assigned to it to this one fixed
# spot, overflowing the 80 bytes meant for the handoff block alone.
#
# AVR data addresses carry the 0x800000 offset the linker uses to tell flash from
# SRAM.
ifneq ($(RAM_END),)
ARCH_HANDOFF_AT := $(shell printf '0x%x' $$(( 0x800000 + $(RAM_END) - 79 )))
ARCH_STACK_TOP  := $(shell printf '0x%x' $$(( $(RAM_END) - 80 )))
ARCH_FW_PLACEMENT_LDFLAGS := -Wl,-T,mk/handoff.ld \
                             -Wl,--section-start=.handoff=$(ARCH_HANDOFF_AT) \
                             -Wl,--defsym=__stack=$(ARCH_STACK_TOP)
else
ARCH_FW_PLACEMENT_LDFLAGS :=
endif

# tfw-boot: .text at the boot section's start, and the stack below the handoff
# block. TFW_BOOT_HANDOFF_LEN bytes at the top of SRAM belong to the CDI handoff,
# so the bootloader's stack must start below them or the first deep call writes
# over the thing it is about to hand across.
#
# Guarded on BOOT_SECTION because not every board has one. These lines are
# expanded on EVERY make invocation, whatever the target, so a board whose
# profile states no boot geometry -- the ATtiny85 has no boot-loader section at
# all -- turned `make firmware` into a shell arithmetic error printed before the
# first compile, pointing at nothing. The bootloader targets still fail for that
# board, which is correct; they now fail on the linker script rather than on
# every unrelated build.
ifneq ($(BOOT_SECTION),)
ARCH_BOOT_TEXT_START := $(shell printf '0x%x' $$(( $(FLASH_TOTAL) - $(BOOT_SECTION) )))
ARCH_BOOT_STACK_TOP  := $(shell printf '0x%x' $$(( $(RAM_END) - 80 )))
ARCH_BOOT_HANDOFF_AT := $(shell printf '0x%x' $$(( 0x800000 + $(RAM_END) - 79 )))
endif

ARCH_BOOT_PLACEMENT_LDFLAGS := -Wl,-T,mk/handoff.ld \
                               -Wl,--section-start=.text=$(ARCH_BOOT_TEXT_START) \
                               -Wl,--section-start=.handoff=$(ARCH_BOOT_HANDOFF_AT) \
                               -Wl,--defsym=__stack=$(ARCH_BOOT_STACK_TOP)

# Writing an image. Recipes, not rules: mk/firmware.mk owns the targets, so the
# one `flash` and the one `boot-flash` exist for every architecture and only the
# programmer differs. The AVRDUDE_* settings come from the board's profile.mk.
ARCH_FLASH = avrdude -v -p$(AVRDUDE_PART) -c$(AVRDUDE_PROG) -P$(AVRDUDE_PORT) \
             -b$(AVRDUDE_BAUD) -D -Uflash:w:$<:i

# NOT the `arduino` programmer. That one talks TO a bootloader, and the boot
# section is the one place a bootloader cannot write. This needs an ISP.
ISP_PROG ?= usbasp
ISP_PORT ?= usb

ARCH_BOOT_FLASH = avrdude -v -p$(AVRDUDE_PART) -c$(ISP_PROG) -P$(ISP_PORT) \
                  -Uflash:w:$<:i
