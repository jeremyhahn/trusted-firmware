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
# Tone is optional and gated by FILE SELECTION, not by #if inside a file: the
# ATmega32U4 has no Timer2 and sets AG_FEATURE_TONE 0, so its image must not
# contain the tone path at all rather than contain a disabled copy of it.
TONE_SRCS := $(shell grep -q 'define AG_FEATURE_TONE 0' $(BOARD_DIR)/board_profile.h \
                     && echo "" || echo "src/caps/tone.c src/hal/avr/hal_tone.c")

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

CRYPTO ?= soft
ifeq ($(CRYPTO),soft)
CRYPTO_SRCS := src/crypto/soft/provider.c src/crypto/soft/uds_flash.c
ALIAS_SRCS  :=
ALIAS_DEFS  :=
else ifeq ($(CRYPTO),atecc608a)
CRYPTO_SRCS := src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c
# The alias certificate rides with the tier that can sign one, and only on a
# board with room for it. It is gated by FILE SELECTION, exactly as tone is: a
# certificate names a subject public key, the software tier's identity is an
# HMAC and has no public half, so a soft image must not contain the code at all
# rather than contain a copy of it that can only refuse -- and the ATmega32U4
# loses 4 KB of its flash to a DFU bootloader and cannot carry the builder at
# all. Both boards say so in their profile, and the grep here is what makes the
# profile's word the build's.
ALIAS_OFF   := $(shell grep -q 'define AG_FEATURE_ALIAS_CERT 0' $(BOARD_DIR)/board_profile.h && echo 1)
ifeq ($(ALIAS_OFF),1)
ALIAS_SRCS  :=
ALIAS_DEFS  :=
else
ALIAS_SRCS  := src/caps/alias.c src/caps/uccs.c
ALIAS_DEFS  := -DAG_FEATURE_ALIAS_CERT=1
endif
else
$(error unknown CRYPTO tier "$(CRYPTO)" -- expected soft or atecc608a)
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
              -Iinclude -Isrc -Isrc/crypto -I$(BOARD_DIR) \
              -DAG_BOARD_HEADER='"board_profile.h"' $(PROV_DEFS) $(ALIAS_DEFS)

AVR_LDFLAGS := -mmcu=$(MCU) -Os -flto -Wl,--gc-sections -Wl,--relax

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
  src/caps/sys.c src/caps/gpio.c src/caps/sched.c \
  src/caps/analog.c src/caps/storage.c src/caps/attest.c \
  src/caps/pwm.c src/caps/pulse.c src/caps/bus.c $(TONE_SRCS) $(PROV_SRCS) \
  src/crypto/sha256.c $(CRYPTO_SRCS) $(ALIAS_SRCS) \
  $(BOARD_DIR)/board.c \
  $(filter-out src/hal/avr/hal_tone.c,$(wildcard src/hal/avr/*.c)))

AVR_OBJS := $(patsubst %.c,$(AVR_DIR)/%.o,$(AVR_SRCS))

$(AVR_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC_AVR) $(AVR_CFLAGS) -frandom-seed=$(notdir $<) -c -o $@ $<

$(TARGET).elf: $(AVR_OBJS)
	@$(CC_AVR) $(AVR_LDFLAGS) -o $@ $(AVR_OBJS)

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
FIRMWARE_BOARDS := $(notdir $(wildcard boards/*))
FIRMWARE_TIERS  := soft atecc608a

firmware-matrix:
	@fail=0; \
	for b in $(FIRMWARE_BOARDS); do \
	  for t in $(FIRMWARE_TIERS); do \
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
