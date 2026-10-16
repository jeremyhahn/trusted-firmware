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
#   ARCH_RAM_USED                      $(call ...,elf): static RAM the image takes
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

# Static RAM an image occupies: every section the ELF places in SRAM, read from
# the section table by address. Not avr-size's berkeley `data`: a .data that
# holds only constants is flagged READONLY and berkeley counts it as text, so
# a gate built on it would pass an image whose .data does not fit. avr-ld maps
# the data space at 0x800000 and EEPROM at 0x810000; SRAM is what lies between.
ARCH_RAM_USED = $(ARCH_SIZE) -A -d $(1) | awk '$$3 >= 8388608 && $$3 < 8454144 { s += $$2 } END { if (NR) print s + 0 }'

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
#
# BOOT_BASE is the profile's, but on AVR it has exactly one right value: BOOTRST
# enters the boot section at the top of flash, so the section must end there. A
# profile that says otherwise links a bootloader the part never runs, and the
# reset vector lands in whatever the application left at the real address --
# refused here rather than found on a bench.
ifneq ($(BOOT_SECTION),)
ifneq ($(shell echo $$(( $(BOOT_BASE) + $(BOOT_SECTION) ))),$(FLASH_TOTAL))
$(error BOOT_BASE "$(BOOT_BASE)" + BOOT_SECTION $(BOOT_SECTION) is not FLASH_TOTAL $(FLASH_TOTAL): BOOTRST puts the AVR boot section at the top of flash)
endif
ARCH_BOOT_TEXT_START := $(shell printf '0x%x' $$(( $(BOOT_BASE) )))
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

ARCH_ISP = avrdude -p$(AVRDUDE_PART) -c$(ISP_PROG) -P$(ISP_PORT)

# NEVER A BARE CHIP ERASE. Over ISP a flash page cannot be rewritten without
# the chip erase, so replacing the boot section needs one -- and the erase takes
# the EEPROM with it unless EESAVE is programmed. The EEPROM holds the platform
# block, the device's serial and with it its identity: a board flashed this way
# comes back as a different device, or as none (CA m233, item 4).
#
# So the fuse is read first. Which fuse byte and bit is EESAVE is the board's
# (BOOT_EESAVE_FUSE, BOOT_EESAVE_BIT in boards/<board>/profile.mk); an AVR fuse
# bit is programmed when it reads 0.
#
#   EESAVE programmed      flash as before; the erase keeps the EEPROM.
#   not, by default        refuse, and print the two ways on: the exact hfuse
#                          write that programs EESAVE and nothing else, worked
#                          out from the byte just read, or the opt-in below.
#   BOOT_EEPROM_RESTORE=1  read the EEPROM to a timestamped file under
#                          $(BUILD_DIR)/, erase and flash, write it back with -D,
#                          read it back and compare. A mismatch fails and the
#                          saved file stays where it is.
#
# No fuse is ever written here. A fuse write is an operator's decision, made with
# the command in front of them.
BOOT_EEPROM_RESTORE ?= 0

ARCH_BOOT_FLASH = \
  fuse=$$($(ARCH_ISP) -q -q -U $(BOOT_EESAVE_FUSE):r:-:h) || exit 1; \
  case "$$fuse" in 0x[0-9a-fA-F]*) ;; \
    *) echo "boot-flash: could not read $(BOOT_EESAVE_FUSE): avrdude printed '$$fuse'" >&2; exit 1;; esac; \
  if [ $$(( (fuse >> $(BOOT_EESAVE_BIT)) & 1 )) -eq 0 ]; then \
    echo "boot-flash: EESAVE is programmed ($(BOOT_EESAVE_FUSE)=$$fuse): the chip erase keeps the EEPROM"; \
    $(ARCH_ISP) -v -Uflash:w:$<:i; \
  elif [ "$(BOOT_EEPROM_RESTORE)" = 1 ]; then \
    save=$(BUILD_DIR)/eeprom-$(BOARD)-$$(date -u +%Y%m%dT%H%M%SZ).hex; \
    echo "boot-flash: EESAVE is not programmed ($(BOOT_EESAVE_FUSE)=$$fuse); saving the EEPROM to $$save"; \
    mkdir -p $(BUILD_DIR) && \
    $(ARCH_ISP) -U eeprom:r:$$save:i && \
    $(ARCH_ISP) -v -Uflash:w:$<:i && \
    $(ARCH_ISP) -D -U eeprom:w:$$save:i && \
    $(ARCH_ISP) -U eeprom:r:$$save.readback:i || \
      { echo "boot-flash: FAILED part-way. The EEPROM as it was is in $$save; write it back with:" >&2; \
        echo "  $(ARCH_ISP) -D -U eeprom:w:$$save:i" >&2; exit 1; }; \
    if ! cmp -s $$save $$save.readback; then \
      echo "boot-flash: EEPROM READ-BACK MISMATCH. Saved before the erase: $$save; on the part now: $$save.readback" >&2; \
      echo "  write it back with: $(ARCH_ISP) -D -U eeprom:w:$$save:i" >&2; exit 1; \
    fi; \
    rm -f $$save.readback; \
    echo "boot-flash: the EEPROM was written back and reads back equal; the saved copy is $$save"; \
  else \
    fix=$$(printf '0x%02x' $$(( fuse & ~(1 << $(BOOT_EESAVE_BIT)) & 0xff ))); \
    echo "boot-flash: REFUSED. EESAVE is not programmed ($(BOOT_EESAVE_FUSE)=$$fuse, bit $(BOOT_EESAVE_BIT) reads 1)," >&2; \
    echo "  and writing the boot section needs a chip erase, which would take the EEPROM" >&2; \
    echo "  -- the platform block, and the device's identity -- with it. Either:" >&2; \
    echo "  program EESAVE first (this clears bit $(BOOT_EESAVE_BIT) and changes nothing else):" >&2; \
    echo "    $(ARCH_ISP) -U $(BOOT_EESAVE_FUSE):w:$$fix:m" >&2; \
    echo "  or have boot-flash save the EEPROM, flash, write it back and compare:" >&2; \
    echo "    make boot-flash BOARD=$(BOARD) BOOT_TIER=$(BOOT_TIER) BOOT_MEASURE=$(BOOT_MEASURE) BOOT_SIG=$(BOOT_SIG) BOOT_OTA=$(BOOT_OTA) BOOT_EEPROM_RESTORE=1" >&2; \
    exit 1; \
  fi
