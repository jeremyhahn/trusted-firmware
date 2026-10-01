# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The device build.

BOARD      ?= atmega328p
BOARD_DIR  := boards/$(BOARD)
-include $(BOARD_DIR)/profile.mk

# The crypto tier is part of the OUTPUT PATH, not just the object list. The
# soft and element images are different firmware -- different provider,
# different size, different measurement -- and writing both to
# build/<board>/arduino-generic.elf meant whichever was built last silently
# became "the" image. The simavr suite ran against the element build on a part
# with no element and reported a failure that was entirely the build system's.
# A capability the part does not have is turned off IN THE BOARD PROFILE and
# gated HERE by FILE SELECTION, never by #if inside a file. The ATmega32U4 has
# no Timer2 and sets AG_FEATURE_TONE 0, so its image must not contain the tone
# path at all rather than contain a disabled copy of it.
#
# Asked of the profile once, by a function, because there are now four of these
# and four copies of the same grep is four chances for one of them to test the
# wrong symbol -- which does not fail the build, it silently ships a module the
# profile said to leave out.
#
# The whitespace class is not tidiness. The first version matched a single
# space, a profile that column-aligned its defines did not match, and the build
# compiled a module the board had switched off -- failing on a register the part
# does not have, several files away from the line that caused it. `profile_says`
# anchors the value so AG_FEATURE_I2C 0 cannot be read out of something longer.
profile_says = $(shell grep -Eq '^[[:space:]]*#[[:space:]]*define[[:space:]]+$(1)[[:space:]]+$(2)[[:space:]]*(/\*.*)?$$' \
                       $(BOARD_DIR)/board_profile.h && echo 1)
feature_off  = $(call profile_says,AG_FEATURE_$(1),0)

TONE_SRCS := $(if $(call feature_off,TONE),,src/caps/tone.c src/hal/avr/hal_tone.c)

# PWM. The ATtiny85 spends Timer0 on the millisecond tick and Timer1 on the
# software UART's bit clock, and Timer1's control register is not a mega's in
# any case -- so there is no compare unit for ag_hal_pwm_write to drive and the
# board sets AG_FEATURE_PWM 0.
PWM_SRCS := $(if $(call feature_off,PWM),,src/caps/pwm.c src/hal/avr/hal_pwm.c)

# The three buses, gated as ONE unit because they are one translation unit.
#
# src/hal/avr/hal_bus.c holds the TWI driver, the SPI driver and the bit-banged
# 1-Wire driver, and file selection cannot take two of three. On a part with a
# USI and neither peripheral -- the ATtiny85 -- that file does not compile at
# all, so all three go, and 1-Wire goes with them even though it would have
# worked. Splitting the file is the change that would give it back; until then
# the board profile says AG_FEATURE_ONEWIRE 0 and CAPS reports the truth about
# THIS BUILD, which is the property that matters to a host.
BUS_OFF  := $(and $(call feature_off,I2C),$(call feature_off,SPI),$(call feature_off,ONEWIRE))
# main.c calls ag_bus_init() unconditionally (no #if there, by the same rule
# as everywhere else): bus.c provides it when any bus is on, and bus_none.c --
# a no-op -- provides the same symbol when BUS_OFF, so exactly one is always
# linked. Mirrors how CRYPTO_SRCS gives every tier its own ag_crypto_init().
BUS_SRCS := $(if $(BUS_OFF),src/caps/bus_none.c,src/caps/bus.c src/hal/avr/hal_bus.c)

# The byte link. A part with no USART needs a software one, and the two
# implementations define the same four functions -- so exactly one is linked and
# the choice is the board's, not a flag's.
UART_SRCS := $(if $(call profile_says,AG_UART_SOFT,1), \
                  src/hal/avr/hal_uart_soft.c,src/hal/avr/hal_uart.c)

# The HAL modules that are chosen rather than always present. Filtered out of
# the wildcard below and added back by the lists above, so a file that is gated
# off is not compiled at all -- which is the point, since several of them do not
# compile on the parts that gate them.
HAL_OPTIONAL := src/hal/avr/hal_tone.c src/hal/avr/hal_pwm.c src/hal/avr/hal_bus.c \
                src/hal/avr/hal_uart.c src/hal/avr/hal_uart_soft.c

# Provisioning commands are a JIG image, never a production one. The gate is
# file selection: PROVISION=1 compiles the translation unit and defines the
# feature, and anything else leaves a production image with no handler at all --
# not a disabled one, not one behind a check an attacker can argue with.
PROVISION ?= 0
ifeq ($(PROVISION),1)
PROV_SRCS  := src/caps/provision.c
PROV_DEFS  := -DAG_FEATURE_PROVISION=1
else
PROV_SRCS  :=
PROV_DEFS  :=
endif

# The tier default is the BOARD'S, when the board has an opinion. A profile that
# says nothing gets `soft`, as every board did before there was a part that
# could not carry it. A command-line CRYPTO= still wins over both, so an
# unbuildable combination can still be asked for and still fails honestly.
CRYPTO ?= soft
ifeq ($(CRYPTO),soft)
CRYPTO_SRCS := src/crypto/sha256.c src/crypto/soft/provider.c src/crypto/soft/uds_flash.c
ALIAS_SRCS  :=
ALIAS_DEFS  :=
else ifeq ($(CRYPTO),none)
# THE UNATTESTED TIER. Not "soft with the crypto taken out" -- see the header of
# src/crypto/none/provider.c. SHA-256 is part of a TIER's source list rather
# than a fixed member of the image precisely so that a tier which cannot use it
# does not carry it: on an ATtiny85 that one file is 4 018 bytes of an 8 192
# byte part, spent on a digest there is no key to sign.
CRYPTO_SRCS := src/crypto/none/provider.c
ALIAS_SRCS  :=
ALIAS_DEFS  :=
else ifeq ($(CRYPTO),atecc608a)
CRYPTO_SRCS := src/crypto/sha256.c src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c
# The alias certificate rides with the tier that can sign one, and only on a
# board with room for it. It is gated by FILE SELECTION, exactly as tone is: a
# certificate names a subject public key, the software tier's identity is an
# HMAC and has no public half, so a soft image must not contain the code at all
# rather than contain a copy of it that can only refuse -- and the ATmega32U4
# loses 4 KB of its flash to a DFU bootloader and cannot carry the builder at
# all. Both boards say so in their profile, and the grep here is what makes the
# profile's word the build's.
ALIAS_OFF   := $(call feature_off,ALIAS_CERT)
ifeq ($(ALIAS_OFF),1)
ALIAS_SRCS  :=
ALIAS_DEFS  :=
else
ALIAS_SRCS  := src/caps/alias.c src/caps/uccs.c
ALIAS_DEFS  := -DAG_FEATURE_ALIAS_CERT=1
endif
else
$(error unknown CRYPTO tier "$(CRYPTO)" -- expected none, soft or atecc608a)
endif

AVR_DIR    := $(BUILD_DIR)/$(BOARD)/$(CRYPTO)$(if $(filter 1,$(PROVISION)),-prov,)
TARGET     := $(AVR_DIR)/arduino-generic

CC_AVR     := avr-gcc
OBJCOPY    := avr-objcopy
SIZE       := avr-size

# -flto and --gc-sections together are what make link-time HAL substitution pay
# off: an unused peripheral's code is not merely unreferenced, it is gone, and
# ag_hal_gpio_write inlines to a two-instruction sbi at its call site.
#
# The reproducibility flags are NOT optional. FWHash is signed evidence, so a
# verifier needs a golden measurement computable from source -- which requires
# that the same source produce the same bytes on any machine. -ffile-prefix-map
# strips the build path, -frandom-seed makes the compiler's internal symbol
# naming deterministic, and SOURCE_DATE_EPOCH pins anything time-derived.
AVR_CFLAGS := -std=c11 -mmcu=$(MCU) -DF_CPU=$(F_CPU)UL \
              -Os -flto -ffunction-sections -fdata-sections \
              -fno-fat-lto-objects \
              -Wall -Wextra -Werror -Wconversion -Wshadow \
              -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
              -ffile-prefix-map=$(CURDIR)=. \
              -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Iboot -I$(BOARD_DIR) \
              -DAG_BOARD_HEADER='"board_profile.h"' $(PROV_DEFS) $(ALIAS_DEFS)

AVR_LDFLAGS := -mmcu=$(MCU) -Os -flto -Wl,--gc-sections -Wl,--relax

# The CDI handoff block's SRAM address, and the stack kept below it -- issue
# #6/#18. THE SAME FORMULA mk/boot.mk uses for BOOT_HANDOFF_AT and
# BOOT_STACK_TOP, because the application and ag-boot must land on the
# identical address with no shared linker script to enforce it; one formula,
# derived once from the board's own RAM_END, is the only way the two images
# cannot drift apart on it. Without the stack move the C runtime's default
# __stack -- the physical top of SRAM -- puts the application's own stack
# exactly where the handoff block lives, and the first deep call overwrites
# the CDI before ag_boot_handoff_init ever reads it.
#
# Guarded on RAM_END for the same reason mk/boot.mk guards on BOOT_SECTION: not
# every board has a bootloader to hand anything off, and the ATtiny85's
# profile.mk states so by defining neither.
#
# `.handoff`, NOT the generic `.noinit` output section -- src/hal/avr/
# hal_early.c's ag_hal_early_flags lives in ordinary (unpinned) `.noinit` too,
# for the same "must survive past .init3" reason, and pinning the generic
# section's address would carry every object assigned to it to this one fixed
# spot, overflowing the 80 bytes meant for the handoff block alone.
ifneq ($(RAM_END),)
AVR_HANDOFF_AT := $(shell printf '0x%x' $$(( 0x800000 + $(RAM_END) - 79 )))
AVR_STACK_TOP  := $(shell printf '0x%x' $$(( $(RAM_END) - 80 )))
AVR_LDFLAGS    += -Wl,-T,mk/handoff.ld \
                  -Wl,--section-start=.handoff=$(AVR_HANDOFF_AT) \
                  -Wl,--defsym=__stack=$(AVR_STACK_TOP)
endif

# The identity sources follow the same RAM_END gate, by FILE SELECTION rather
# than a conditional in main.c -- main.c is not one of the four places a
# target conditional is permitted. A board with no boot section links the
# _none.c stubs instead: same two symbols main.c and hal_sys.c call on every
# board, no storage behind them, and everything reachable only from ATTEST
# (which such a board's profile already turns off) for --gc-sections to drop.
# Costed on the ATtiny85: firmware-matrix caught this the first time around --
# the real modules pushed it 77 bytes over a 256-byte RAM budget.
ifneq ($(RAM_END),)
IDENTITY_SRCS := src/core/handoff.c src/core/platform_id.c \
                 boot/boot_handoff.c boot/boot_platform.c
else
IDENTITY_SRCS := src/core/handoff_none.c src/core/platform_id_none.c
endif

# Sorted, not wildcard-ordered. Link order affects the output image, and a
# wildcard's order depends on the filesystem -- which would make the
# reproducibility check pass on one machine and fail on another for no reason
# anyone could see.
# The crypto tier is a LINK-TIME choice, exactly as the HAL is. One provider is
# linked per image; there is no runtime selection and no vtable, so an image
# built for the software tier cannot be talked into using an element it does not
# have, and --gc-sections keeps working.
#
#   CRYPTO=soft      SHA-256 and HMAC in software, UDS in boot-section flash
#   CRYPTO=atecc608a the element holds the UDS and signs; DICE-HW
AVR_SRCS := $(sort \
  src/main.c \
  src/proto/crc16.c src/proto/frame.c src/proto/transport.c \
  src/core/reader.c src/core/writer.c src/core/dispatch.c src/core/board_lookup.c \
  src/core/reset_cause.c src/core/secure_zero.c $(IDENTITY_SRCS) \
  src/caps/sys.c src/caps/gpio.c src/caps/sched.c \
  src/caps/analog.c src/caps/storage.c src/caps/attest.c \
  src/caps/pulse.c $(PWM_SRCS) $(BUS_SRCS) $(TONE_SRCS) $(PROV_SRCS) \
  $(CRYPTO_SRCS) $(ALIAS_SRCS) \
  $(BOARD_DIR)/board.c $(UART_SRCS) \
  $(filter-out $(HAL_OPTIONAL),$(wildcard src/hal/avr/*.c)))

AVR_OBJS := $(patsubst %.c,$(AVR_DIR)/%.o,$(AVR_SRCS))

$(AVR_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC_AVR) $(AVR_CFLAGS) -frandom-seed=$(notdir $<) -c -o $@ $<

$(TARGET).elf: $(AVR_OBJS)
	@$(CC_AVR) $(AVR_LDFLAGS) -o $@ $(AVR_OBJS)

# -R .eeprom only. mk/handoff.ld (in AVR_LDFLAGS, above) makes `.handoff`
# genuinely NOLOAD -- SHT_NOBITS, the same section type `.bss` already is --
# so it carries no bytes in the ELF and objcopy already leaves it out of
# every derived image on its own: the *.hex here, and mk/release.mk's
# provenance .bin. A second `-R .handoff` at each of those points would be
# the exclude-it-everywhere shape this file's own linker script exists to
# avoid needing.
$(TARGET).hex: $(TARGET).elf
	@$(OBJCOPY) -O ihex -R .eeprom $< $@

## firmware-matrix: build EVERY board and crypto tier, as CI does
#
# `make ci` used to build `firmware` and `firmware-attested` and nothing else --
# both at the default BOARD -- while the workflow built a six-way board x tier
# matrix. So a change that overflowed one board's budget passed the local gate
# and went red on the server, which is exactly what happened with the UCCS work
# on the 32U4: 370 bytes over, invisible to `make ci`.
#
# This is the third time a gate CI runs and `make ci` does not has cost a red
# main. A gate whose local and CI invocations differ is one a developer cannot
# reproduce, and therefore one that gets argued with rather than fixed.
# The TIERS ARE PER BOARD, and that is not a refinement -- it is the difference
# between a matrix and a guess. The list was `soft atecc608a` for every board,
# which was right while every board had a TWI peripheral and a boot section to
# hide a key in. The ATtiny85 has neither: the element tier is an I2C device on
# a part with no I2C, and the software tier's whole definition is a key in
# boot-section flash behind lock bits, which this part does not have. Building
# it at those tiers would not be a size failure, it would be an image whose CAPS
# claims a root of trust the silicon cannot provide.
#
# A board states its tiers in profile.mk. One that says nothing gets the two it
# always had, so no existing board changes.
BOARD_TIERS     ?= soft atecc608a
FIRMWARE_BOARDS := $(notdir $(wildcard boards/*))

## board-tiers: print the crypto tiers BOARD supports (used by firmware-matrix)
board-tiers:
	@echo $(BOARD_TIERS)

.PHONY: board-tiers

firmware-matrix:
	@fail=0; \
	for b in $(FIRMWARE_BOARDS); do \
	  for t in $$($(MAKE) --no-print-directory board-tiers BOARD=$$b); do \
	    $(MAKE) --no-print-directory firmware BOARD=$$b CRYPTO=$$t || fail=1; \
	  done; \
	done; \
	exit $$fail

.PHONY: firmware-matrix

## firmware: build the firmware for BOARD (default atmega328p)
firmware: $(TARGET).hex size-check
	@echo "built $(TARGET).hex"

## size-check: fail the build when an image exceeds its board's budget
# A GATE, not a report, and a prerequisite of `firmware` rather than something
# to run when someone remembers. Flash creep is caught on the commit that
# causes it instead of the day a feature will not fit.
size-check: $(TARGET).elf
	@flash=$$($(SIZE) --format=berkeley $< | awk 'NR==2{print $$1+$$2}'); \
	 ram=$$($(SIZE)  --format=berkeley $< | awk 'NR==2{print $$2+$$3}'); \
	 printf '%-12s flash %6s / %-6s (%2d%% of part)   ram %5s / %-5s (%2d%% of part)\n' \
	   "$(BOARD)" "$$flash" "$(FLASH_BUDGET)" "$$((100*flash/$(FLASH_TOTAL)))" \
	   "$$ram" "$(RAM_BUDGET)" "$$((100*ram/$(RAM_TOTAL)))"; \
	 fail=0; \
	 if [ "$$flash" -gt "$(FLASH_BUDGET)" ]; then echo "FLASH BUDGET EXCEEDED for $(BOARD)"; fail=1; fi; \
	 if [ "$$ram"   -gt "$(RAM_BUDGET)"   ]; then echo "RAM BUDGET EXCEEDED for $(BOARD)";   fail=1; fi; \
	 exit $$fail

## reproducible-check: build twice in different directories and diff the image
# Without this the platform's attestation verdict can never be better than
# "trust whatever hash the device reported", because nobody can compute the
# expected hash from source.
reproducible-check:
	@rm -rf /tmp/agrepro-a /tmp/agrepro-b
	@cp -a . /tmp/agrepro-a && cp -a . /tmp/agrepro-b
	@$(MAKE) -C /tmp/agrepro-a --no-print-directory BOARD=$(BOARD) CRYPTO=$(CRYPTO) $(TARGET).hex >/dev/null
	@$(MAKE) -C /tmp/agrepro-b --no-print-directory BOARD=$(BOARD) CRYPTO=$(CRYPTO) $(TARGET).hex >/dev/null
	@if cmp -s /tmp/agrepro-a/$(TARGET).hex /tmp/agrepro-b/$(TARGET).hex; then \
	   echo "reproducible: two builds in different directories are byte-identical"; \
	 else \
	   echo "NOT REPRODUCIBLE: the same source produced different images"; \
	   cmp /tmp/agrepro-a/$(TARGET).hex /tmp/agrepro-b/$(TARGET).hex || true; exit 1; \
	 fi

## flash: write the firmware to a connected board
flash: $(TARGET).hex
	avrdude -v -p$(AVRDUDE_PART) -c$(AVRDUDE_PROG) -P$(AVRDUDE_PORT) \
	        -b$(AVRDUDE_BAUD) -D -Uflash:w:$<:i

.PHONY: firmware size-check reproducible-check flash
