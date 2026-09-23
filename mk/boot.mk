# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# ag-boot: a SECOND, SEPARATE image.
#
# Not another configuration of the firmware. It links a different HAL, a
# different main(), a different section address and a different budget, and it
# is flashed separately -- `make firmware` and `make bootloader` produce two
# .hex files that live in different parts of the same flash and are written by
# two avrdude invocations.
#
# It shares three things with the application on purpose, and each would
# otherwise be a second copy of something that must not disagree with itself:
#
#   src/crypto/sha256.c   the measurement must be the hash the application
#                         reports and a verifier recomputes
#   src/proto/frame.c     one codec, so OTA frames and application frames are
#                         framed the same way by construction
#   src/hal/avr/hal_early.c  one "clear MCUSR and disarm the watchdog", because
#                         the copy that is wrong bootloops the part

BOOT_DIR_OUT := $(BUILD_DIR)/$(BOARD)/boot
BOOT_TARGET  := $(BOOT_DIR_OUT)/ag-boot

# The tier, chosen at LINK time exactly as CRYPTO is for the application.
BOOT_TIER ?= soft
ifeq ($(BOOT_TIER),soft)
BOOT_TIER_SRCS := boot/tier/cdi_soft.c
else ifeq ($(BOOT_TIER),atecc608a)
# The element driver the APPLICATION links, not a second copy of it. hal_bus.c
# and hal_sys.c come along for ag_hal_i2c_xfer and ag_hal_delay_us; everything
# else in them is unreferenced and --gc-sections drops it.
BOOT_TIER_SRCS := boot/tier/cdi_atecc.c src/crypto/atecc608a/atecc.c \
                  src/hal/avr/hal_bus.c src/hal/avr/hal_sys.c src/core/board.c \
                  boards/$(BOARD)/board.c
else
$(error unknown BOOT_TIER "$(BOOT_TIER)" -- expected soft or atecc608a)
endif

# The signature backend. ONE is linked, and there is currently one to choose.
#
# See boot/boot_verify.h for what ed25519-donna measures on this part and why no
# substitute has been made for it. `make bootloader-budget` reproduces the
# numbers rather than asking anyone to believe them.
BOOT_SIG ?= ed25519-donna
ifeq ($(BOOT_SIG),ed25519-donna)
BOOT_SIG_SRCS := boot/verify/verify_ed25519_donna.c \
                 src/third_party/ed25519-donna/ed25519.c
else
$(error unknown BOOT_SIG "$(BOOT_SIG)" -- expected ed25519-donna)
endif

# Sorted, for the same reason mk/avr.mk sorts: a wildcard's order depends on the
# filesystem, and link order affects the image, so an unsorted list makes the
# reproducibility check pass on one machine and fail on another.
BOOT_SRCS := $(sort \
  boot/boot_main.c boot/boot_image.c boot/boot_measure.c boot/boot_ota.c \
  boot/boot_platform.c boot/boot_policy.c boot/boot_session.c \
  boot/hal/avr/boot_hal_avr.c \
  src/crypto/sha256.c src/proto/crc16.c src/proto/frame.c \
  src/hal/avr/hal_early.c \
  $(BOOT_TIER_SRCS) $(BOOT_SIG_SRCS))

BOOT_OBJS := $(patsubst %.c,$(BOOT_DIR_OUT)/%.o,$(BOOT_SRCS))

# The same reproducibility flags as the application, and for a stronger reason:
# the bootloader is what a verifier must be able to identify, and a bootloader
# nobody can rebuild byte-for-byte is a root of trust nobody can audit.
BOOT_CFLAGS := -std=c11 -mmcu=$(MCU) -DF_CPU=$(F_CPU)UL \
               -Os -flto -ffunction-sections -fdata-sections \
               -fno-fat-lto-objects \
               -Wall -Wextra -Werror -Wconversion -Wshadow \
               -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
               -ffile-prefix-map=$(CURDIR)=. \
               -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Isrc/crypto/atecc608a \
               -Isrc/hal/avr -Isrc/third_party/ed25519-donna \
               -Iboot -Iboot/verify -I$(BOARD_DIR) \
               -DAG_BOARD_HEADER='"board_profile.h"'

# The vendored tree, built WITHOUT -Werror and without our warning set.
#
# The same arrangement mk/host.mk uses for Unity, and the same reasoning: those
# warnings are not ours to fix, and -Werror over a vendored tree turns an
# upstream bump into a build break with no bug behind it. -Iboot/verify/shim
# supplies the empty <sys/param.h> avr-libc does not have, and -include supplies
# the one declaration upstream expects the integrator to provide -- both without
# editing a vendored byte. See test/third_party/MANIFEST.md.
DONNA_CFLAGS := -std=gnu11 -mmcu=$(MCU) -DF_CPU=$(F_CPU)UL \
                -Os -flto -ffunction-sections -fdata-sections \
                -fno-fat-lto-objects \
                -ffile-prefix-map=$(CURDIR)=. \
                -DED25519_REFHASH -DED25519_CUSTOMRANDOM -DED25519_NO_INLINE_ASM \
                -Iboot/verify/shim -Isrc/third_party/ed25519-donna \
                -include boot/verify/donna_shim.h

# .text at the boot section's start, and the stack below the handoff block.
#
# AG_BOOT_HANDOFF_LEN bytes at the top of SRAM belong to the CDI handoff, so the
# bootloader's stack must start below them or the first deep call writes over
# the thing it is about to hand across. AVR data addresses carry the 0x800000
# offset the linker uses to tell flash from SRAM.
BOOT_TEXT_START := $(shell printf '0x%x' $$(( $(FLASH_TOTAL) - $(BOOT_SECTION) )))
BOOT_STACK_TOP  := $(shell printf '0x%x' $$(( $(RAM_END) - 80 )))
BOOT_HANDOFF_AT := $(shell printf '0x%x' $$(( 0x800000 + $(RAM_END) - 79 )))

BOOT_LDFLAGS := -mmcu=$(MCU) -Os -flto -Wl,--gc-sections -Wl,--relax \
                -Wl,--section-start=.text=$(BOOT_TEXT_START) \
                -Wl,--section-start=.noinit=$(BOOT_HANDOFF_AT) \
                -Wl,--defsym=__stack=$(BOOT_STACK_TOP)

$(BOOT_DIR_OUT)/src/third_party/ed25519-donna/%.o: src/third_party/ed25519-donna/%.c
	@mkdir -p $(dir $@)
	@$(CC_AVR) $(DONNA_CFLAGS) -frandom-seed=$(notdir $<) -c -o $@ $<

$(BOOT_DIR_OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC_AVR) $(BOOT_CFLAGS) -frandom-seed=$(notdir $<) -c -o $@ $<

$(BOOT_TARGET).elf: $(BOOT_OBJS)
	@$(CC_AVR) $(BOOT_LDFLAGS) -o $@ $(BOOT_OBJS)

$(BOOT_TARGET).hex: $(BOOT_TARGET).elf
	@$(OBJCOPY) -O ihex -R .eeprom $< $@

## bootloader: build ag-boot for BOARD and enforce the boot-section budget
bootloader: $(BOOT_TARGET).hex boot-size-check
	@echo "built $(BOOT_TARGET).hex"

## boot-size-check: fail when ag-boot does not fit in the boot section
# A GATE, exactly as size-check is for the application, and a prerequisite of
# `bootloader` rather than something to run when someone remembers.
#
# The budget is BOOT_BUDGET from the board profile, which is the boot section's
# size. It is NOT raised to meet an image: on the 328P there is nothing to raise
# it to, because 2048 words is the largest BOOTSZ the part has.
boot-size-check: $(BOOT_TARGET).elf
	@flash=$$($(SIZE) --format=berkeley $< | awk 'NR==2{print $$1+$$2}'); \
	 ram=$$($(SIZE)  --format=berkeley $< | awk 'NR==2{print $$2+$$3}'); \
	 printf 'ag-boot %-10s flash %6s / %-6s   ram %5s / %-5s (%s tier, %s)\n' \
	   "$(BOARD)" "$$flash" "$(BOOT_BUDGET)" "$$ram" "$(RAM_TOTAL)" \
	   "$(BOOT_TIER)" "$(BOOT_SIG)"; \
	 if [ "$$flash" -gt "$(BOOT_BUDGET)" ]; then \
	   echo "BOOT SECTION BUDGET EXCEEDED: $$flash > $(BOOT_BUDGET) by $$((flash-$(BOOT_BUDGET))) bytes"; \
	   echo "run 'make bootloader-budget' for the per-component breakdown"; \
	   exit 1; fi

## bootloader-budget: what each part of ag-boot costs, measured
# The numbers boot/boot_verify.h quotes, reproduced rather than asserted.
#
# Built WITHOUT -flto, because an LTO object carries bytecode and no sections --
# avr-size reports zero for every one of them, which looks like a working report
# saying the bootloader is free. The trade is that these figures are slightly
# pessimistic against the shipped image, where LTO inlines across units; the
# linked core figure below is measured with LTO on and is the honest one.
BOOT_MEAS_DIR := $(BOOT_DIR_OUT)/measure
BOOT_CORE_SRCS := $(filter-out $(BOOT_SIG_SRCS),$(BOOT_SRCS))

bootloader-budget:
	@mkdir -p $(BOOT_MEAS_DIR)
	@echo "ag-boot component sizes -- $(BOARD), $(BOOT_TIER) tier, $(BOOT_SIG)"
	@echo "budget: $(BOOT_BUDGET) bytes (BOOTSZ $(BOOT_SECTION) bytes, part flash $(FLASH_TOTAL), part SRAM $(RAM_TOTAL))"
	@echo
	@core=0; \
	 for src in $(BOOT_CORE_SRCS); do \
	   o=$(BOOT_MEAS_DIR)/$$(echo $$src | tr / _).o; \
	   $(CC_AVR) $(BOOT_CFLAGS) -fno-lto -c -o $$o $$src 2>/dev/null; \
	   n=$$($(SIZE) --format=berkeley $$o | awk 'NR==2{print $$1+$$2}'); \
	   printf '  %-52s %7s\n' "$$src" "$$n"; core=$$((core+n)); \
	 done; \
	 printf '  %-52s %7s\n' "-- core, summed before --gc-sections" "$$core"; \
	 echo; \
	 for src in $(BOOT_SIG_SRCS); do \
	   o=$(BOOT_MEAS_DIR)/$$(echo $$src | tr / _).o; \
	   $(CC_AVR) $(if $(findstring third_party,$(src)),$(DONNA_CFLAGS),$(BOOT_CFLAGS)) -fno-lto -c -o $$o $$src 2>/dev/null || \
	     $(CC_AVR) $(DONNA_CFLAGS) -fno-lto -c -o $$o $$src 2>/dev/null; \
	   n=$$($(SIZE) --format=berkeley $$o | awk 'NR==2{print $$1+$$2}'); \
	   printf '  %-52s %7s\n' "$$src" "$$n"; \
	 done
	@echo
	@$(MAKE) --no-print-directory boot-core-size
	@echo
	@echo "signature backend, linked and gc-sectioned (verify reachable only):"
	@$(CC_AVR) $(DONNA_CFLAGS) -fno-lto -fstack-usage -c \
	   -o $(BOOT_MEAS_DIR)/su.o src/third_party/ed25519-donna/ed25519.c 2>/dev/null; \
	 mv ed25519.su $(BOOT_MEAS_DIR)/ 2>/dev/null || true; \
	 awk -F'\t' '$$1 ~ /ed25519_sign_open$$/ {printf "  peak stack frame  %7s bytes   (part SRAM %s)\n", $$2, "$(RAM_TOTAL)"}' \
	   $(BOOT_MEAS_DIR)/ed25519.su 2>/dev/null || true

## boot-core-size: ag-boot WITHOUT a signature backend, linked and measured
# NOT A BOOTLOADER AND NOT FLASHABLE. ag_boot_verify and ag_boot_pubkey are
# resolved to address zero by --defsym, so this image would jump into the reset
# vector instead of verifying anything. It exists to answer one question
# precisely: how much of the boot section everything EXCEPT the signature
# primitive occupies, and therefore how much budget a primitive has to fit in.
#
# That number is the one that matters when choosing a replacement for a
# primitive that does not fit, which is why it is a target rather than a
# figure in a comment that would go stale.
boot-core-size:
	@mkdir -p $(BOOT_MEAS_DIR)
	@for src in $(BOOT_CORE_SRCS); do \
	   o=$(BOOT_MEAS_DIR)/lto_$$(echo $$src | tr / _).o; \
	   $(CC_AVR) $(BOOT_CFLAGS) -frandom-seed=$$(basename $$src) -c -o $$o $$src; \
	 done
	@# Linked at address zero, NOT at the boot section's start. The question is
	@# how many bytes the core is, and placing it at 0x7000 makes an image that
	@# is merely too big fail to link instead of reporting its size -- which is
	@# the one thing this target exists to do.
	@$(CC_AVR) -mmcu=$(MCU) -Os -flto -Wl,--gc-sections -Wl,--relax \
	   -Wl,--defsym=ag_boot_verify=0 -Wl,--defsym=ag_boot_pubkey=0 \
	   -o $(BOOT_MEAS_DIR)/core.elf $(BOOT_MEAS_DIR)/lto_*.o
	@flash=$$($(SIZE) --format=berkeley $(BOOT_MEAS_DIR)/core.elf | awk 'NR==2{print $$1+$$2}'); \
	 ram=$$($(SIZE) --format=berkeley $(BOOT_MEAS_DIR)/core.elf | awk 'NR==2{print $$2+$$3}'); \
	 printf 'ag-boot core, linked (measure + verify seam + rollback + EEPROM + CDI + OTA + UART + framing):\n'; \
	 printf '  flash %6s / %-6s   ram %5s / %-5s\n' "$$flash" "$(BOOT_BUDGET)" "$$ram" "$(RAM_TOTAL)"; \
	 printf '  a signature backend has %s bytes of flash and %s bytes of SRAM to fit in\n' \
	   "$$(($(BOOT_BUDGET)-flash))" "$$(($(RAM_TOTAL)-ram))"

## bootloader-reproducible: two ag-boot builds in different directories, diffed
bootloader-reproducible:
	@rm -rf /tmp/agboot-a /tmp/agboot-b
	@cp -a . /tmp/agboot-a && cp -a . /tmp/agboot-b
	@$(MAKE) -C /tmp/agboot-a --no-print-directory BOARD=$(BOARD) BOOT_TIER=$(BOOT_TIER) $(BOOT_TARGET).hex >/dev/null
	@$(MAKE) -C /tmp/agboot-b --no-print-directory BOARD=$(BOARD) BOOT_TIER=$(BOOT_TIER) $(BOOT_TARGET).hex >/dev/null
	@if cmp -s /tmp/agboot-a/$(BOOT_TARGET).hex /tmp/agboot-b/$(BOOT_TARGET).hex; then \
	   echo "ag-boot reproducible: two builds in different directories are byte-identical"; \
	 else \
	   echo "ag-boot NOT REPRODUCIBLE"; exit 1; \
	 fi

## boot-flash: write ag-boot to a connected board over ISP
# NOT the `arduino` programmer. That one talks TO a bootloader, and the boot
# section is the one place a bootloader cannot write. This needs an ISP.
boot-flash: $(BOOT_TARGET).hex
	avrdude -v -p$(AVRDUDE_PART) -c$(ISP_PROG) -P$(ISP_PORT) \
	        -Uflash:w:$<:i

ISP_PROG ?= usbasp
ISP_PORT ?= usb

.PHONY: bootloader boot-size-check bootloader-budget boot-core-size bootloader-reproducible boot-flash
