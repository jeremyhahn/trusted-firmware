# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The device build, for whatever architecture the board says it is.
#
# The BOARD names its architecture (`ARCH := avr` in boards/<board>/profile.mk)
# and everything architecture-specific -- the toolchain, its flags, how an image
# is measured and written, where the linker puts the handoff block -- lives in
# mk/arch/<ARCH>.mk. This file names none of it: the HAL is src/hal/$(ARCH), the
# toolchain is ARCH_CC, and a second architecture is a directory, not an edit
# here.

BOARD      ?= atmega328p
BOARD_DIR  := boards/$(BOARD)
-include $(BOARD_DIR)/profile.mk

ifeq ($(wildcard $(BOARD_DIR)/profile.mk),)
$(error no such board "$(BOARD)" -- expected boards/$(BOARD)/profile.mk, one of: $(notdir $(wildcard boards/*)))
endif
ifeq ($(ARCH),)
$(error $(BOARD_DIR)/profile.mk does not set ARCH -- every board names its architecture, e.g. ARCH := avr)
endif
ifeq ($(wildcard mk/arch/$(ARCH).mk),)
$(error $(BOARD) says ARCH := $(ARCH) but mk/arch/$(ARCH).mk does not exist; known: $(basename $(notdir $(wildcard mk/arch/*.mk))))
endif
include mk/arch/$(ARCH).mk

# Where this architecture's register-level code lives. The HAL is substituted at
# link time, so the directory is the whole of the selection: every .c in it is
# the architecture's, and the optional ones are chosen below by FILE.
HAL_DIR := src/hal/$(ARCH)

# The crypto tier is part of the OUTPUT PATH, not just the object list. The
# soft and element images are different firmware -- different provider,
# different size, different measurement -- and writing both to
# build/<board>/trusted-firmware.elf meant whichever was built last silently
# became "the" image. The simavr suite ran against the element build on a part
# with no element and reported a failure that was entirely the build system's.
# A capability the part does not have is turned off IN THE BOARD PROFILE and
# gated HERE by FILE SELECTION, never by #if inside a file. The ATmega32U4 has
# no Timer2 and sets TFW_FEATURE_TONE 0, so its image must not contain the tone
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
# anchors the value so TFW_FEATURE_I2C 0 cannot be read out of something longer.
profile_says = $(shell grep -Eq '^[[:space:]]*#[[:space:]]*define[[:space:]]+$(1)[[:space:]]+$(2)[[:space:]]*(/\*.*)?$$' \
                       $(BOARD_DIR)/board_profile.h && echo 1)
feature_off  = $(call profile_says,TFW_FEATURE_$(1),0)

TONE_SRCS := $(if $(call feature_off,TONE),,src/caps/tone.c $(HAL_DIR)/hal_tone.c)

# PWM. The ATtiny85 spends Timer0 on the millisecond tick and Timer1 on the
# software UART's bit clock, and Timer1's control register is not a mega's in
# any case -- so there is no compare unit for tfw_hal_pwm_write to drive and the
# board sets TFW_FEATURE_PWM 0.
PWM_SRCS := $(if $(call feature_off,PWM),,src/caps/pwm.c $(HAL_DIR)/hal_pwm.c)

# The three buses, gated as ONE unit because they are one translation unit.
#
# $(HAL_DIR)/hal_bus.c holds (on AVR) the TWI driver, the SPI driver and the bit-banged
# 1-Wire driver, and file selection cannot take two of three. On a part with a
# USI and neither peripheral -- the ATtiny85 -- that file does not compile at
# all, so all three go, and 1-Wire goes with them even though it would have
# worked. Splitting the file is the change that would give it back; until then
# the board profile says TFW_FEATURE_ONEWIRE 0 and CAPS reports the truth about
# THIS BUILD, which is the property that matters to a host.
BUS_OFF  := $(and $(call feature_off,I2C),$(call feature_off,SPI),$(call feature_off,ONEWIRE))
# main.c calls tfw_bus_init() unconditionally (no #if there, by the same rule
# as everywhere else): bus.c provides it when any bus is on, and bus_none.c --
# a no-op -- provides the same symbol when BUS_OFF, so exactly one is always
# linked. Mirrors how CRYPTO_SRCS gives every tier its own tfw_crypto_init().
BUS_SRCS := $(if $(BUS_OFF),src/caps/bus_none.c,src/caps/bus.c $(HAL_DIR)/hal_bus.c)

# The byte link. A part with no USART needs a software one, and the two
# implementations define the same four functions -- so exactly one is linked and
# the choice is the board's, not a flag's.
UART_SRCS := $(if $(call profile_says,TFW_UART_SOFT,1), \
                  $(HAL_DIR)/hal_uart_soft.c,$(HAL_DIR)/hal_uart.c)

# The HAL modules that are chosen rather than always present. Filtered out of
# the wildcard below and added back by the lists above, so a file that is gated
# off is not compiled at all -- which is the point, since several of them do not
# compile on the parts that gate them.
HAL_OPTIONAL := $(HAL_DIR)/hal_tone.c $(HAL_DIR)/hal_pwm.c $(HAL_DIR)/hal_bus.c \
                $(HAL_DIR)/hal_uart.c $(HAL_DIR)/hal_uart_soft.c

# Provisioning commands are a JIG image, never a production one. The gate is
# file selection: PROVISION=1 compiles the translation unit and defines the
# feature, and anything else leaves a production image with no handler at all --
# not a disabled one, not one behind a check an attacker can argue with.
PROVISION ?= 0
ifeq ($(PROVISION),1)
PROV_SRCS  := src/caps/provision.c
PROV_DEFS  := -DTFW_FEATURE_PROVISION=1
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
# issue #4: no operator-signature session on this tier -- see
# TFW_FEATURE_AUTH_SESSION in config.h for why, and auth_session_none.c for
# what a soft-tier image links instead.
AUTH_SRCS   := src/core/auth_session_none.c
AUTH_DEFS   :=
else ifeq ($(CRYPTO),none)
# THE UNATTESTED TIER. Not "soft with the crypto taken out" -- see the header of
# src/crypto/none/provider.c. SHA-256 is part of a TIER's source list rather
# than a fixed member of the image precisely so that a tier which cannot use it
# does not carry it: on an ATtiny85 that one file is 4 018 bytes of an 8 192
# byte part, spent on a digest there is no key to sign.
CRYPTO_SRCS := src/crypto/none/provider.c
ALIAS_SRCS  :=
ALIAS_DEFS  :=
AUTH_SRCS   := src/core/auth_session_none.c
AUTH_DEFS   :=
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
ALIAS_SRCS  := src/caps/alias.c src/caps/tmpl.c src/caps/uccs.c
ALIAS_DEFS  := -DTFW_FEATURE_ALIAS_CERT=1
# ATTEST_CSR -- issue #30 -- reads its fixed headers out of the alias template,
# so it is only ever selected beside it; a profile can still say no to it alone.
ifneq ($(call feature_off,ATTEST_CSR),1)
ALIAS_SRCS  += src/caps/csr.c
ALIAS_DEFS  += -DTFW_FEATURE_ATTEST_CSR=1
endif
endif
# The operator-signature auth session -- issue #4. Unlike ALIAS_CERT this
# costs the element's own Verify command and a few hundred bytes, not a
# certificate builder, so no board profile has needed to say no yet -- but the
# knob exists for the same reason ALIAS_OFF's does: a profile that changes its
# mind should not need a code change to say so.
AUTH_OFF    := $(call feature_off,AUTH_SESSION)
ifeq ($(AUTH_OFF),1)
AUTH_SRCS   := src/core/auth_session_none.c
AUTH_DEFS   :=
else
AUTH_SRCS   := src/core/auth_session.c
AUTH_DEFS   := -DTFW_FEATURE_AUTH_SESSION=1
endif
else
$(error unknown CRYPTO tier "$(CRYPTO)" -- expected none, soft or atecc608a)
endif

FW_DIR     := $(BUILD_DIR)/$(BOARD)/$(CRYPTO)$(if $(filter 1,$(PROVISION)),-prov,)
TARGET     := $(FW_DIR)/trusted-firmware

# The toolchain binaries and the flags that name a machine, an optimisation
# policy and a link placement are the ARCHITECTURE'S (mk/arch/$(ARCH).mk). What
# is the project's, and the same on every architecture, is the warning set and
# the reproducibility flags below.
#
# The reproducibility flags are NOT optional. FWHash is signed evidence, so a
# verifier needs a golden measurement computable from source -- which requires
# that the same source produce the same bytes on any machine. -ffile-prefix-map
# strips the build path, -frandom-seed makes the compiler's internal symbol
# naming deterministic, and SOURCE_DATE_EPOCH pins anything time-derived.
FW_CFLAGS := -std=c11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) \
             -Wall -Wextra -Werror -Wconversion -Wshadow \
             -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
             -ffile-prefix-map=$(CURDIR)=. \
             -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Iboot -I$(HAL_DIR) -I$(BOARD_DIR) \
             -DTFW_BOARD_HEADER='"board_profile.h"' $(PROV_DEFS) $(ALIAS_DEFS) $(AUTH_DEFS)

# The CDI handoff block's placement is the architecture's: see
# ARCH_FW_PLACEMENT_LDFLAGS, which is empty on a board with no RAM_END.
FW_LDFLAGS := $(strip $(ARCH_LDFLAGS) $(ARCH_FW_PLACEMENT_LDFLAGS))

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
                 src/core/update_request.c \
                 boot/boot_handoff.c boot/boot_platform.c boot/boot_request.c
else
IDENTITY_SRCS := src/core/handoff_none.c src/core/platform_id_none.c \
                 src/core/update_request_none.c
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
FW_SRCS := $(sort \
  src/main.c \
  src/proto/crc16.c src/proto/frame.c src/proto/transport.c \
  src/core/reader.c src/core/writer.c src/core/dispatch.c src/core/board_lookup.c \
  src/core/secure_zero.c $(IDENTITY_SRCS) \
  src/caps/sys.c src/caps/gpio.c src/caps/sched.c \
  src/caps/analog.c src/caps/storage.c src/caps/attest.c \
  src/caps/pulse.c $(PWM_SRCS) $(BUS_SRCS) $(TONE_SRCS) $(PROV_SRCS) \
  $(CRYPTO_SRCS) $(ALIAS_SRCS) $(AUTH_SRCS) \
  $(BOARD_DIR)/board.c $(UART_SRCS) \
  $(filter-out $(HAL_OPTIONAL),$(wildcard $(HAL_DIR)/*.c)))

FW_OBJS := $(patsubst %.c,$(FW_DIR)/%.o,$(FW_SRCS))

$(FW_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(FW_CFLAGS) -MMD -MP -frandom-seed=$(notdir $<) -c -o $@ $<

# The headers each object read, written by -MMD beside it. Without these an edit
# to a header rebuilt no image: the .o depended on its .c and nothing else.
-include $(FW_OBJS:.o=.d)

$(TARGET).elf: $(FW_OBJS)
	@$(ARCH_CC) $(FW_LDFLAGS) -o $@ $(FW_OBJS)

# -R .eeprom only. mk/handoff.ld (in FW_LDFLAGS, above) makes `.handoff`
# genuinely NOLOAD -- SHT_NOBITS, the same section type `.bss` already is --
# so it carries no bytes in the ELF and objcopy already leaves it out of
# every derived image on its own: the *.hex here, and mk/release.mk's
# provenance .bin. A second `-R .handoff` at each of those points would be
# the exclude-it-everywhere shape this file's own linker script exists to
# avoid needing.
$(TARGET).hex: $(TARGET).elf
	@$(ARCH_OBJCOPY) -O ihex -R .eeprom $< $@

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

## feature-defs: print the -D feature flags the BOARD/CRYPTO image compiles with (used by board-sweep)
# The same variables FW_CFLAGS carries, so a test compiled with this output sees
# the gates the image was built with -- not config.h's defaults, which say 0 to
# every tier-selected feature on every board.
feature-defs:
	@echo $(PROV_DEFS) $(ALIAS_DEFS) $(AUTH_DEFS)

## firmware-matrix-list: print every "board tier" pair firmware-matrix builds
firmware-matrix-list:
	@for b in $(FIRMWARE_BOARDS); do \
	  for t in $$($(MAKE) -s --no-print-directory board-tiers BOARD=$$b); do \
	    echo "$$b $$t"; \
	  done; \
	done

## ci-matrix-check: fail when the CI firmware matrix differs from firmware-matrix
# The workflow's firmware matrix is a HAND LIST -- GitHub and Gitea both want it
# written out in the file -- and a hand list of what the Makefile derives is a
# list that drifts: a new board builds under `make ci` and is never built by the
# server, or the reverse. This is the gate that says so, in both directions.
#
# It reads the `- {board: X, crypto: Y}` lines of the firmware job and compares
# them, as a set, with firmware-matrix-list. A workflow with NO such line is a
# failure and not a pass: a check that finds nothing to compare has not checked.
ci-matrix-check:
	@want=$$($(MAKE) -s --no-print-directory firmware-matrix-list | sort); \
	 have=$$(sed -n 's/^ *- {board: \([a-z0-9]*\), crypto: \([a-z0-9]*\)}$$/\1 \2/p' \
	         .github/workflows/ci.yml | sort); \
	 if [ -z "$$have" ]; then \
	   echo "ci-matrix-check: no '- {board: X, crypto: Y}' lines in .github/workflows/ci.yml"; exit 1; fi; \
	 if [ "$$want" != "$$have" ]; then \
	   echo "ci-matrix-check: the workflow's firmware matrix is not what 'make firmware-matrix' builds."; \
	   echo "  '<' = built by make, missing from the workflow; '>' = in the workflow, not built by make"; \
	   diff <(echo "$$want") <(echo "$$have") | sed 's/^/  /'; exit 1; fi; \
	 echo "ci-matrix-check: $$(echo "$$want" | wc -l) board/tier pairs, workflow and Makefile agree"

.PHONY: board-tiers feature-defs firmware-matrix-list ci-matrix-check

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
	@flash=$$($(ARCH_SIZE) --format=berkeley $< | awk 'NR==2{print $$1+$$2}'); \
	 ram=$$($(ARCH_SIZE)  --format=berkeley $< | awk 'NR==2{print $$2+$$3}'); \
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
# The programmer is the architecture's: ARCH_FLASH is a recipe from
# mk/arch/$(ARCH).mk, so this is the one `flash` for every board.
flash: $(TARGET).hex
	$(ARCH_FLASH)

.PHONY: firmware size-check reproducible-check flash
