# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The AVR architecture: the one file that knows what an AVR toolchain is called, what it is told, how its images are measured and written, and where its linker is asked to put the CDI handoff block.

ARCH_CC      := avr-gcc
ARCH_OBJCOPY := avr-objcopy
ARCH_SIZE    := avr-size
ARCH_NM      := avr-nm
ARCH_LIBC    := avr-libc

# Static RAM an image occupies: every section the ELF places in SRAM, read from the section table by address.
ARCH_RAM_USED = $(ARCH_SIZE) -A -d $(1) | awk '$$3 >= 8388608 && $$3 < 8454144 { s += $$2 } END { if (NR) print s + 0 }'

# Link-time optimisation (-flto) and --gc-sections together drop an unused peripheral's code entirely.
ARCH_MACH_CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU)UL
ARCH_OPT_CFLAGS  := -Os -flto -ffunction-sections -fdata-sections \
                    -fno-fat-lto-objects

ARCH_LDFLAGS := -mmcu=$(MCU) -Os -flto -Wl,--gc-sections -Wl,--relax

# Code size before speed, for tfw-boot alone: -mcall-prologues replaces each function's register saves and restores with a call to one shared libgcc routine.
ARCH_BOOT_OPT_CFLAGS := -mcall-prologues -mstrict-X -fno-tree-scev-cprop

# Micro-ecc's word for BOOT_SIG=p256-soft (mk/boot.mk): one byte, the AVR's own, which its platform detection would pick anyway; stated so the host test's UECC_WORD default and the part build name the same word path.
ARCH_UECC_CFLAGS := -DuECC_WORD_SIZE=1

# The CDI handoff block's SRAM address, and the stack kept below it.
ifneq ($(RAM_END),)
ARCH_HANDOFF_AT := $(shell printf '0x%x' $$(( 0x800000 + $(RAM_END) - 111 )))
ARCH_STACK_TOP  := $(shell printf '0x%x' $$(( $(RAM_END) - 112 )))
ARCH_FW_PLACEMENT_LDFLAGS := -Wl,-T,mk/handoff.ld \
                             -Wl,--section-start=.handoff=$(ARCH_HANDOFF_AT) \
                             -Wl,--defsym=__stack=$(ARCH_STACK_TOP)
else
ARCH_FW_PLACEMENT_LDFLAGS :=
endif

# tfw-boot: .text at the boot section's start, and the stack below the handoff block.
ifneq ($(BOOT_SECTION),)
ifneq ($(shell echo $$(( $(BOOT_BASE) + $(BOOT_SECTION) ))),$(FLASH_TOTAL))
$(error BOOT_BASE "$(BOOT_BASE)" + BOOT_SECTION $(BOOT_SECTION) is not FLASH_TOTAL $(FLASH_TOTAL): BOOTRST puts the AVR boot section at the top of flash)
endif
ARCH_BOOT_TEXT_START := $(shell printf '0x%x' $$(( $(BOOT_BASE) )))
ARCH_BOOT_STACK_TOP  := $(shell printf '0x%x' $$(( $(RAM_END) - 112 )))
ARCH_BOOT_HANDOFF_AT := $(shell printf '0x%x' $$(( 0x800000 + $(RAM_END) - 111 )))
endif

# And tfw-boot starts in its own boot/hal/avr/boot_start_avr.S, not the crt: -nostartfiles drops the crt's interrupt vector table, which a boot that takes no interrupt does not need (228 bytes on the 2560).
ARCH_BOOT_START_SRC := boot/hal/avr/boot_start_avr.S
ARCH_BOOT_START_LDFLAGS := -nostartfiles

ARCH_BOOT_PLACEMENT_LDFLAGS := $(ARCH_BOOT_START_LDFLAGS) -Wl,-T,mk/handoff.ld \
                               -Wl,--section-start=.text=$(ARCH_BOOT_TEXT_START) \
                               -Wl,--section-start=.handoff=$(ARCH_BOOT_HANDOFF_AT) \
                               -Wl,--defsym=__stack=$(ARCH_BOOT_STACK_TOP) \
                               -Wl,--defsym=__TEXT_REGION_LENGTH__=$(FLASH_TOTAL)

# The text region's length is the part's flash, given again. -nostartfiles drops the crt object that supplied it, and the linker script's default is 1 MB: ld then never refuses an oversized boot, no "region `text' overflowed", and .text runs on past the top of flash.

# Writing an image.
ARCH_FLASH = avrdude -v -p$(AVRDUDE_PART) -c$(AVRDUDE_PROG) -P$(AVRDUDE_PORT) \
             -b$(AVRDUDE_BAUD) -D -Uflash:w:$<:i

# Not the `arduino` programmer: a bootloader cannot write the boot section, so this needs ISP.
ISP_PROG ?= usbasp
ISP_PORT ?= usb

ARCH_ISP = avrdude -p$(AVRDUDE_PART) -c$(ISP_PROG) -P$(ISP_PORT)

# Never a bare chip erase: it takes the EEPROM, which holds the platform block, unless EESAVE is programmed.
BOOT_EEPROM_RESTORE ?= 0
BOOT_FIRST_FLASH    ?= 0

# The bytes a factory record holds: every fuse byte and the lock byte of a classic AVR, by avrdude's memory names.
ARCH_FACTORY_BYTES := lfuse hfuse efuse lock

# The first boot flash.
ARCH_BOOT_FIRST_FLASH = \
    rec=$(BUILD_DIR)/factory-$(BOARD)-$$(date -u +%Y%m%dT%H%M%SZ); \
    echo "$@: first boot flash: EESAVE is not programmed ($(BOOT_EESAVE_FUSE)=$$fuse); writing the factory record first"; \
    lines=""; \
    for f in $(ARCH_FACTORY_BYTES); do \
      v=$$($(ARCH_ISP) -q -q -U $$f:r:-:h) || exit 1; \
      case "$$v" in 0x[0-9a-fA-F]*) ;; \
        *) echo "$@: could not read $$f: avrdude printed '$$v'; nothing was written to the part" >&2; exit 1;; esac; \
      lines="$$lines$$(printf 'FACTORY_%-5s := %s' $$(echo $$f | tr a-z A-Z) $$v)\n"; \
    done; \
    mkdir -p $(BUILD_DIR) && \
    $(ARCH_ISP) -U eeprom:r:$$rec.eeprom.bin:r -U flash:r:$$rec.flash.bin:r || \
      { echo "$@: FAILED reading the EEPROM and flash for the factory record; nothing was written to the part" >&2; exit 1; }; \
    { echo "\# The factory record of $(BOARD) ($(AVRDUDE_PART)), read by boot-flash"; \
      echo "\# before its first write, $$(date -u +%Y-%m-%dT%H:%M:%SZ). The FACTORY_* lines go"; \
      echo "\# in boards/$(BOARD)/profile.mk (make board-restore reads them)."; \
      printf "$$lines"; \
      echo "\# EEPROM $$(basename $$rec).eeprom.bin sha256 $$(sha256sum < $$rec.eeprom.bin | cut -d' ' -f1)"; \
      echo "\# flash  $$(basename $$rec).flash.bin sha256 $$(sha256sum < $$rec.flash.bin | cut -d' ' -f1)"; \
    } > $$rec.mk || { echo "$@: FAILED writing $$rec.mk; nothing was written to the part" >&2; exit 1; }; \
    echo "$@: the factory record is $$rec.mk"; \
    fix=$$(printf '0x%02x' $$(( fuse & ~(1 << $(BOOT_EESAVE_BIT)) & 0xff ))); \
    echo "$@: programming EESAVE: $(BOOT_EESAVE_FUSE) $$fuse -> $$fix (bit $(BOOT_EESAVE_BIT), nothing else)"; \
    $(ARCH_ISP) -U $(BOOT_EESAVE_FUSE):w:$$fix:m || \
      { echo "$@: FAILED writing $(BOOT_EESAVE_FUSE); nothing was flashed. The factory record is $$rec.mk" >&2; exit 1; }; \
    now=$$($(ARCH_ISP) -q -q -U $(BOOT_EESAVE_FUSE):r:-:h) || exit 1; \
    case "$$now" in 0x[0-9a-fA-F]*) ;; \
      *) echo "$@: could not read $(BOOT_EESAVE_FUSE) back: avrdude printed '$$now'; nothing was flashed" >&2; exit 1;; esac; \
    if [ $$(( now )) -ne $$(( fix )) ]; then \
      echo "$@: REFUSED. $(BOOT_EESAVE_FUSE) reads $$now after writing $$fix: EESAVE did not hold, and the" >&2; \
      echo "  chip erase would take the EEPROM. Nothing was flashed. The factory record is $$rec.mk" >&2; \
      exit 1; \
    fi; \
    echo "$@: EESAVE is programmed ($(BOOT_EESAVE_FUSE)=$$now): the chip erase keeps the EEPROM"; \
    $(ARCH_ISP) -v -Uflash:w:$<:i

ARCH_BOOT_FLASH = \
  if [ "$(BOOT_FIRST_FLASH)" = 1 ] && [ "$(BOOT_EEPROM_RESTORE)" = 1 ]; then \
    echo "$@: REFUSED. BOOT_FIRST_FLASH=1 programs EESAVE and BOOT_EEPROM_RESTORE=1 flashes without it: pick one" >&2; \
    exit 1; \
  fi; \
  fuse=$$($(ARCH_ISP) -q -q -U $(BOOT_EESAVE_FUSE):r:-:h) || exit 1; \
  case "$$fuse" in 0x[0-9a-fA-F]*) ;; \
    *) echo "$@: could not read $(BOOT_EESAVE_FUSE): avrdude printed '$$fuse'" >&2; exit 1;; esac; \
  if [ $$(( (fuse >> $(BOOT_EESAVE_BIT)) & 1 )) -eq 0 ]; then \
    echo "$@: EESAVE is programmed ($(BOOT_EESAVE_FUSE)=$$fuse): the chip erase keeps the EEPROM"; \
    $(ARCH_ISP) -v -Uflash:w:$<:i; \
  elif [ "$(BOOT_FIRST_FLASH)" = 1 ]; then \
    $(ARCH_BOOT_FIRST_FLASH); \
  elif [ "$(BOOT_EEPROM_RESTORE)" = 1 ]; then \
    save=$(BUILD_DIR)/eeprom-$(BOARD)-$$(date -u +%Y%m%dT%H%M%SZ).hex; \
    echo "$@: EESAVE is not programmed ($(BOOT_EESAVE_FUSE)=$$fuse); saving the EEPROM to $$save"; \
    mkdir -p $(BUILD_DIR) && \
    $(ARCH_ISP) -U eeprom:r:$$save:i && \
    $(ARCH_ISP) -v -Uflash:w:$<:i && \
    $(ARCH_ISP) -D -U eeprom:w:$$save:i && \
    $(ARCH_ISP) -U eeprom:r:$$save.readback:i || \
      { echo "$@: FAILED part-way. The EEPROM as it was is in $$save; write it back with:" >&2; \
        echo "  $(ARCH_ISP) -D -U eeprom:w:$$save:i" >&2; exit 1; }; \
    if ! cmp -s $$save $$save.readback; then \
      echo "$@: EEPROM READ-BACK MISMATCH. Saved before the erase: $$save; on the part now: $$save.readback" >&2; \
      echo "  write it back with: $(ARCH_ISP) -D -U eeprom:w:$$save:i" >&2; exit 1; \
    fi; \
    rm -f $$save.readback; \
    echo "$@: the EEPROM was written back and reads back equal; the saved copy is $$save"; \
  else \
    fix=$$(printf '0x%02x' $$(( fuse & ~(1 << $(BOOT_EESAVE_BIT)) & 0xff ))); \
    echo "$@: REFUSED. EESAVE is not programmed ($(BOOT_EESAVE_FUSE)=$$fuse, bit $(BOOT_EESAVE_BIT) reads 1)," >&2; \
    echo "  and writing the boot section needs a chip erase, which would take the EEPROM" >&2; \
    echo "  -- the platform block, and the device's identity -- with it. Either:" >&2; \
    echo "  program EESAVE first (this clears bit $(BOOT_EESAVE_BIT) and changes nothing else):" >&2; \
    echo "    $(ARCH_ISP) -U $(BOOT_EESAVE_FUSE):w:$$fix:m" >&2; \
    if [ "$@" = boot-flash ]; then \
      echo "  or, on the board's first boot flash, have $@ write the factory record and program it:" >&2; \
      echo "    make $@ BOARD=$(BOARD) $(ISP_RERUN) BOOT_FIRST_FLASH=1" >&2; \
    fi; \
    echo "  or have $@ save the EEPROM, flash, write it back and compare:" >&2; \
    echo "    make $@ BOARD=$(BOARD) $(ISP_RERUN) BOOT_EEPROM_RESTORE=1" >&2; \
    exit 1; \
  fi

# `make lock-check`: the lock bits over the soft root's UDS, read back after the operator's last avrdude step (SPEC device-provisioning R6).
ARCH_LOCK_CHECK = \
  lock=$$($(ARCH_ISP) -q -q -U lock:r:-:h) || exit 1; \
  case "$$lock" in 0x[0-9a-fA-F]*) ;; \
    *) echo "$@: could not read the lock byte: avrdude printed '$$lock'" >&2; exit 1;; esac; \
  want=0; short=""; \
  for pair in $(BOOT_LOCK_PROGRAMMED); do \
    bit=$${pair%%:*}; name=$${pair\#*:}; \
    want=$$(( want | (1 << bit) )); \
    if [ $$(( (lock >> bit) & 1 )) -ne 0 ]; then short="$$short $$name (bit $$bit)"; fi; \
  done; \
  mask=$$(printf '0x%02x' $$(( want ))); \
  if [ -n "$$short" ]; then \
    echo "$@: REFUSED. $(BOARD) is not provisioned: the lock byte reads $$lock, and these" >&2; \
    echo "  bits of $$mask must read programmed (0) but read 1:$$short." >&2; \
    echo "  The soft root's UDS is not locked. See docs/device-provisioning/user.md, step 7." >&2; \
    exit 1; \
  fi; \
  echo "$@: $(BOARD) lock byte $$lock: every bit of $$mask is programmed ($(BOOT_LOCK_PROGRAMMED)); the UDS is locked"

# `make board-restore` (mk/board-restore.mk): the board as it left the factory,
# its stock bootloader and the FACTORY_* fuse and lock bytes of its profile.mk,
# recorded on its page under docs/development/bench/. The hex is checked against
# the profile's SHA-256 first, so nothing is erased for the wrong file. The boot
# section goes in through ARCH_BOOT_FLASH, so its chip erase is behind the same
# EESAVE guard. The fuses follow, the high fuse (which holds EESAVE on every AVR
# board here) last, so the erase ran with EESAVE as it was. The lock byte comes last of all: a chip erase is the
# only thing that clears it. Each byte is then read back and compared.
ARCH_BOARD_RESTORE = \
  sum=$$(sha256sum $< | cut -d' ' -f1); \
  if [ "$$sum" != "$(FACTORY_BOOT_HEX_SHA256)" ]; then \
    echo "$@: REFUSED. $< has SHA-256 $$sum, not the stock bootloader's $(FACTORY_BOOT_HEX_SHA256)" >&2; \
    exit 1; fi; \
  { $(ARCH_BOOT_FLASH); } || exit 1; \
  $(ARCH_ISP) -U lfuse:w:$(FACTORY_LFUSE):m -U efuse:w:$(FACTORY_EFUSE):m \
              -U hfuse:w:$(FACTORY_HFUSE):m -U lock:w:$(FACTORY_LOCK):m || exit 1; \
  for pair in lfuse:$(FACTORY_LFUSE) hfuse:$(FACTORY_HFUSE) efuse:$(FACTORY_EFUSE) lock:$(FACTORY_LOCK); do \
    f=$${pair%%:*}; want=$${pair\#*:}; \
    got=$$($(ARCH_ISP) -q -q -U $$f:r:-:h) || exit 1; \
    case "$$got" in 0x[0-9a-fA-F]*) ;; \
      *) echo "$@: could not read $$f back: avrdude printed '$$got'" >&2; exit 1;; esac; \
    if [ $$((got)) -ne $$((want)) ]; then \
      echo "$@: $$f reads $$got after the write, not the factory $$want" >&2; exit 1; fi; \
  done; \
  echo "$@: $(BOARD) has its stock bootloader and the factory fuses: lfuse=$(FACTORY_LFUSE) hfuse=$(FACTORY_HFUSE) efuse=$(FACTORY_EFUSE) lock=$(FACTORY_LOCK)"
