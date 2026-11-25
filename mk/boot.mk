# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# tfw-boot: a SECOND, separate image.

# Tfw-boot's own hardware layer, flash, EEPROM, UART, handoff placement, jump and reset, one file per architecture, named for it.
BOOT_HAL_DIR := boot/hal/$(ARCH)

# The SoC's half of the bootloader's HAL, on an architecture that splits its HAL by SoC as mk/firmware.mk's HAL_SOC_DIR does: every .c under boot/hal/<arch>/<soc>/, the SoC file both images share for what $(HAL_DIR)/hal_early.c asks of the part (the watchdog and the reset cause, on Cortex-M), the SoC's register-free tables where it has them (nrf54l's soc_tables.c: the baud table, the PSEL encoding, the revision and the RRAM's bounds), and its reset-cause decode where it has a register for one (nrf54l's soc_reset.c), and its LPUART divisor search where it has one (mcxn94x's lpuart_baud.c, the arithmetic both images' UARTs share).
BOOT_SOC_SRCS := $(if $(SOC),$(wildcard $(BOOT_HAL_DIR)/$(SOC)/*.c $(HAL_DIR)/$(SOC)/hal_soc.c \
                                        $(HAL_DIR)/$(SOC)/soc_tables.c $(HAL_DIR)/$(SOC)/soc_reset.c \
                                        $(HAL_DIR)/$(SOC)/lpuart_baud.c))


# The tier, chosen at LINK time exactly as CRYPTO is for the application.
BOOT_TIER ?= $(if $(filter 4096,$(BOOT_SECTION)),atecc608a,soft)
ifeq ($(BOOT_TIER),soft)
BOOT_TIER_SRCS := boot/tier/cdi_soft.c
else ifeq ($(BOOT_TIER),atecc608a)
# The element driver the APPLICATION links, not a second copy of it.
BOOT_TIER_SRCS := boot/tier/cdi_atecc.c src/crypto/atecc608a/atecc.c \
                  $(HAL_DIR)/hal_bus.c $(HAL_DIR)/hal_sys.c src/core/board_lookup.c \
                  boards/$(BOARD)/board.c
else
$(error unknown BOOT_TIER "$(BOOT_TIER)" -- expected soft or atecc608a)
endif

# The signature backend.
BOOT_MEASURING_GOAL := $(filter bootloader-budget boot-core-size,$(MAKECMDGOALS))
BOOT_SIG ?= $(if $(BOOT_MEASURING_GOAL),ed25519-donna,$(if $(filter atecc608a,$(BOOT_TIER)),atecc608a,p256-soft))
BOOT_SIG_OPT_CFLAGS :=
ifeq ($(BOOT_SIG),ed25519-donna)
BOOT_SIG_SRCS := boot/verify/verify_ed25519_donna.c boot/verify/donna_shim.c \
                 src/third_party/ed25519-donna/ed25519.c
else ifeq ($(BOOT_SIG),atecc608a)
# The element verifies.
BOOT_SIG_SRCS := boot/verify/verify_atecc608a.c
else ifeq ($(BOOT_SIG),p256-soft)
# P-256 ECDSA in software: the element's signature over the same trailer v1, so one signed image serves both tiers.
BOOT_SIG_SRCS := boot/verify/verify_p256_soft.c boot/verify/p256_key_slot.c \
                 src/third_party/micro-ecc/uECC.c
BOOT_SIG_SU_SRC    := src/third_party/micro-ecc/uECC.c
BOOT_SIG_SU_CFLAGS  = $(UECC_BOOT_CFLAGS)
BOOT_SIG_SU_FN     := uECC_verify
# Two code-size options for this backend on AVR, every tfw-boot object and micro-ecc: avr-gcc 14.2 splits each 32-bit value into bytes and hoists loop invariants into registers it then saves, and both cost the 2560's soft row more than the 8 KB section holds.
BOOT_SIG_OPT_CFLAGS := $(if $(filter avr,$(ARCH)),-fno-split-wide-types -fno-move-loop-invariants)
else
$(error unknown BOOT_SIG "$(BOOT_SIG)" -- expected ed25519-donna, atecc608a or p256-soft)
endif
# What bootloader-budget reports the peak stack frame of: donna's verify unless the arm names its own.
BOOT_SIG_SU_SRC    ?= src/third_party/ed25519-donna/ed25519.c
BOOT_SIG_SU_CFLAGS ?= $(DONNA_CFLAGS)
BOOT_SIG_SU_FN     ?= ed25519_sign_open

# Where the measurement is computed.
BOOT_MEASURE ?= $(BOOT_TIER)
ifeq ($(BOOT_MEASURE),soft)
# The size-first SHA-256: the same digest, sha256.h's API, a third of the bytes.
BOOT_MEASURE_SRCS := boot/measure/measure_soft.c src/crypto/sha256_small.c
else ifeq ($(BOOT_MEASURE),atecc608a)
BOOT_MEASURE_SRCS := boot/measure/measure_atecc608a.c
else
$(error unknown BOOT_MEASURE "$(BOOT_MEASURE)" -- expected soft or atecc608a)
endif

# OTA in the boot section, or not.
BOOT_OTA ?= $(if $(and $(filter atecc608a,$(BOOT_SIG)),$(filter-out 4096,$(BOOT_SECTION))),1,0)
ifeq ($(BOOT_OTA),1)
BOOT_OTA_SRCS := boot/recovery/recovery_ota.c boot/boot_ota.c boot/boot_session.c src/proto/frame.c
BOOT_OTA_DEFS :=
else
# The Cortex-M halt names its refusal in an EVENT frame, so it takes the framer; the AVR halt has no room for it and stops mute.
BOOT_OTA_SRCS := $(BOOT_HAL_DIR)/recovery_halt.c $(if $(filter cortex-m,$(ARCH)),src/proto/frame.c)
BOOT_OTA_DEFS :=
endif

# The application-staged image in the external OTA store, or not.
BOOT_STORE ?= 0
ifeq ($(BOOT_STORE),1)
ifeq ($(OTA_STORE),)
$(error BOOT_STORE=1 but $(BOARD_DIR)/profile.mk names no OTA_STORE -- this board has no external store)
endif
BOOT_STORE_SRCS := boot/boot_stage.c boot/boot_cose.c $(BOOT_HAL_DIR)/boot_ota_store_$(ARCH).c
else
BOOT_STORE_SRCS := boot/boot_stage_none.c
endif

# The 2560's swap-move, on a board with two slots (BOOT_AB in profile.mk) and only where an image lands in the secondary: the store's copy, or the serial session's (TFW_OTA_STAGE_DEST).
BOOT_PREV ?= 1
#
# boot_swap.c verifies OTA_CONFIRM's signature with the Sig_structure boot_cose.c builds, so it brings boot_cose.c along on the serial row too; the store row already links it, and BOOT_SRCS's sort drops the second name.
BOOT_SWAP_SRC := boot/boot_swap_none.c
ifeq ($(BOOT_AB),1)
ifneq ($(BOOT_OTA)$(BOOT_STORE),00)
BOOT_SWAP_SRC := boot/boot_swap.c boot/boot_cose.c
endif
else ifeq ($(BOOT_STORE),1)
ifeq ($(BOOT_PREV),1)
BOOT_SWAP_SRC := boot/boot_prev.c
endif
endif
BOOT_STORE_SRCS += $(BOOT_SWAP_SRC)

# Sorted, for the same reason mk/firmware.mk sorts: a wildcard's order depends on the filesystem, and link order affects the image, so an unsorted list makes the reproducibility check pass on one machine and fail on another.
BOOT_CFG     := $(BOOT_TIER)-$(BOOT_MEASURE)-$(BOOT_SIG)-ota$(BOOT_OTA)-store$(BOOT_STORE)$(AB_SUFFIX)$(if \
                $(and $(filter 1,$(BOOT_STORE)),$(if $(filter 1,$(BOOT_AB)),,1),$(if $(filter 1,$(BOOT_PREV)),,1)),-noprev)$(if \
                $(and $(filter p256-soft,$(BOOT_SIG)),$(filter-out 0,$(UECC_OPT))),-uecc$(UECC_OPT))$(if \
                $(strip $(BOOT_OPT_EXTRA)),-x$(firstword $(shell printf '%s' '$(subst ','\'',$(strip $(BOOT_OPT_EXTRA)))' | cksum)))$(AB_SUFFIX)
BOOT_DIR_OUT := $(BUILD_DIR)/$(BOARD)/boot/$(BOOT_CFG)
BOOT_TARGET  := $(BOOT_DIR_OUT)/tfw-boot

# Cortex-M's jump to the application, apart from boot_hal_cortex-m.c because its msr and bx are the one Arm instruction there, and the host compiles that file.
BOOT_JUMP_SRCS := $(if $(filter cortex-m,$(ARCH)),$(BOOT_HAL_DIR)/boot_jump.c)

BOOT_SRCS := $(sort \
  boot/boot_main.c boot/boot_image.c boot/boot_handoff.c boot/boot_store.c \
  boot/boot_request.c boot/boot_entry.c \
  boot/boot_platform.c boot/boot_policy.c boot/boot_trial.c \
  $(BOOT_HAL_DIR)/boot_hal_$(ARCH).c $(BOOT_SOC_SRCS) $(ARCH_BOOT_SRCS) $(BOOT_JUMP_SRCS) \
  src/proto/crc16.c src/proto/crc16_update.c \
  $(HAL_DIR)/hal_early.c src/core/secure_zero.c \
  $(BOOT_MEASURE_SRCS) $(BOOT_OTA_SRCS) $(BOOT_STORE_SRCS) $(BOOT_TIER_SRCS) $(BOOT_SIG_SRCS))

# The architecture's startup, assembly, kept out of BOOT_SRCS because every loop over those compiles C. mk/arch/avr.mk says why the boot has its own.
BOOT_START_OBJ := $(patsubst %.S,$(BOOT_DIR_OUT)/%.o,$(ARCH_BOOT_START_SRC))
BOOT_OBJS := $(patsubst %.c,$(BOOT_DIR_OUT)/%.o,$(BOOT_SRCS)) $(BOOT_START_OBJ)

# The same reproducibility flags as the application, and for a stronger reason: the bootloader is what a verifier must be able to identify, and a bootloader nobody can rebuild byte-for-byte is a root of trust nobody can audit.
BOOT_CFLAGS := -std=c11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) \
               -Wall -Wextra -Werror -Wconversion -Wshadow \
               -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
               -ffile-prefix-map=$(CURDIR)=. \
               -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Isrc/crypto/atecc608a \
               -I$(HAL_DIR) -Isrc/third_party/ed25519-donna -Isrc/third_party/micro-ecc \
               -Iboot -Iboot/verify -I$(BOOT_HAL_DIR) -I$(BOARD_DIR) \
               -DTFW_BOARD_HEADER='"board_profile.h"' $(BOOT_OTA_DEFS) $(BOARD_DEFS) \
               $(ARCH_BOOT_OPT_CFLAGS) $(BOOT_SIG_OPT_CFLAGS) $(BOOT_OPT_EXTRA)

# The vendored tree, built without -Werror and without our warning set.
DONNA_CFLAGS := -std=gnu11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) \
                -ffile-prefix-map=$(CURDIR)=. \
                -DED25519_REFHASH -DED25519_CUSTOMRANDOM -DED25519_NO_INLINE_ASM \
                -Iboot/verify/shim -Isrc/third_party/ed25519-donna \
                -include boot/verify/donna_shim.h $(BOOT_OPT_EXTRA)

# micro-ecc (BOOT_SIG=p256-soft), vendored and built the same way, configured only by its own macros: secp256r1 only, no compressed points, verify is all tfw-boot calls (--gc-sections drops the rest).
UECC_OPT  ?= 0
BOOT_OPT_EXTRA ?=
UECC_DEFS := -DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 \
             -DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0 \
             -DuECC_SUPPORTS_secp256r1=1 -DuECC_SUPPORT_COMPRESSED_POINT=0 \
             -DuECC_OPTIMIZATION_LEVEL=$(UECC_OPT)
UECC_BOOT_CFLAGS := -std=gnu11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) \
                    -ffile-prefix-map=$(CURDIR)=. $(UECC_DEFS) $(ARCH_UECC_CFLAGS) \
                    -Isrc/third_party/micro-ecc $(ARCH_BOOT_OPT_CFLAGS) $(BOOT_SIG_OPT_CFLAGS) $(BOOT_OPT_EXTRA)

# Where .text starts and where the handoff block and the stack go are the architecture's to say, ARCH_BOOT_PLACEMENT_LDFLAGS, which also explains why a board with no boot section still gets through every unrelated make invocation.
BOOT_LDFLAGS := $(ARCH_LDFLAGS) $(ARCH_BOOT_PLACEMENT_LDFLAGS) $(BOOT_OPT_EXTRA)

$(BOOT_DIR_OUT)/src/third_party/ed25519-donna/%.o: src/third_party/ed25519-donna/%.c
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(DONNA_CFLAGS) -MMD -MP -frandom-seed=$(notdir $<) -c -o $@ $<

$(BOOT_DIR_OUT)/src/third_party/micro-ecc/%.o: src/third_party/micro-ecc/%.c
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(UECC_BOOT_CFLAGS) -MMD -MP -frandom-seed=$(notdir $<) -c -o $@ $<

$(BOOT_DIR_OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(BOOT_CFLAGS) -MMD -MP -frandom-seed=$(notdir $<) -c -o $@ $<

$(BOOT_DIR_OUT)/%.o: %.S
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(ARCH_MACH_CFLAGS) -MMD -MP -c -o $@ $<

# See the same line in mk/firmware.mk: the headers each object read.
-include $(BOOT_OBJS:.o=.d)

# A failed link is REPORTED, not dumped.
$(BOOT_TARGET).elf: $(BOOT_OBJS)
	@$(ARCH_CC) $(BOOT_LDFLAGS) -o $@ $(BOOT_OBJS) 2>$(BOOT_DIR_OUT)/link.err || { \
	  sh scripts/boot_link_report.sh $(BOOT_DIR_OUT)/link.err '$(BOARD)' || exit 1; \
	  echo; \
	  $(MAKE) --no-print-directory boot-core-size || true; \
	  echo; \
	  echo "See docs/secure-boot/engineering.md. The budget is"; \
	  echo "$(BOOT_BUDGET) bytes because BOOTSZ $(BOOT_SECTION) is this part's largest"; \
	  echo "setting -- there is nothing to raise it to."; \
	  exit 1; }

# objcopy -R .eeprom only: mk/handoff.ld makes `.handoff` NOLOAD, so it carries no bytes and no derived image includes it.
$(BOOT_TARGET).hex: $(BOOT_TARGET).elf
	@$(ARCH_OBJCOPY) -O ihex -R .eeprom $< $@


# ---------------------------------------------------------------- host tests
#
# The bootloader's logic on x86 against test/mock/boot_hal_mock.c, exactly as
# the application's layers run against test/mock/hal_mock.c.
#
# Their own rules rather than mk/host.mk's pattern, and they live in
# test/unit/boot/ so that pattern's wildcard does not claim them: each links a
# DIFFERENT set of sources. There is no one library here -- test_boot_policy
# links the real Ed25519 and its own key slot, test_boot_ota links no hardware
# at all -- and a single link line covering all of them would drag donna's 150 KB
# into suites that have no business with it.
## bootloader-check: build every SUPPORTED bootloader configuration
#
# The supported set, and it is short because the boot section is small. Each row
# is a configuration that fits on that board; anything not listed is not
# supported, and `make bootloader` and `make boot-flash` refuse it before they
# build anything (below). `make bootloader-budget` measures any configuration;
# `make bootloader-candidate` builds and sizes a row before it joins (below).
#
#   328P / 32U4  4 KB section  element hash + element verify, no OTA in boot
#   328P         4 KB section  the same plus the store: the copy and the
#                              activation's signature check, no PREV restore and
#                              no confirm (BOOT_PREV=0, owner queue 176 (a));
#                              measured 1498 bytes over: not a supported row
#   2560         8 KB section  the same, plus OTA in boot or the external store
#   mps2-an505   32 KB region  soft hash + P-256 in software, no OTA, no store
#                              (#161, QEMU's Cortex-M33; mk/arch/cortex-m.mk);
#                              the same with OTA in boot, single-slot, and
#                              the same with the memory store (OTA_STORE :=
#                              mem, the copy and PREV of boot_prev.c), which
#                              the QEMU wire run boots under the OTA_STAGE=1
#                              image and the OTA cycle row
#   mps2-an500   32 KB region  the base row and the store row on QEMU's
#                              Cortex-M7, at address 0, both booted as on
#                              mps2-an505
#   both MPS2    A/B           the store row with two slots (a trailing :ab,
#                              BOOT_AB=1): swap-move, the one trial, confirm
#                              and rollback as on the 2560 (boot_swap.c), the
#                              slots in board_profile.h, which the QEMU wire
#                              run boots under the A/B OTA_STAGE=1 image
#   stm32h573i-dk 32 KB region mps2-an505's base row (#14); its writes to the slot and the
#                              platform block go through the flash interface
#                              (boot/hal/cortex-m/stm32h5/h5_flash.c)
#   rp2350-pico2w 32 KB region the same; platform writes through the boot ROM
#                              (row rp2350, boot/hal/cortex-m/rp2350/)
#   nrf54l15-dk  32 KB region  the same on the nRF54L15's Cortex-M33 (#17); its
#                              platform-block writes go through the RRAM
#                              controller (boot/hal/cortex-m/nrf54l/boot_rram.c)
#   frdm-mcxn947 32 KB region  the same on the MCXN947's core 0 (row mcxn947);
#                              its writes go through the boot ROM's flash
#                              driver (boot/hal/cortex-m/mcxn94x/mcxn_flash.c)
#
# The MPS2 OTA, store and A/B rows are not yet measured: no lane that added
# them had an Arm toolchain, so their bytes used are this target's first run.
#
# Flash used, avr-gcc 14.2.0 with ARCH_BOOT_OPT_CFLAGS (issue #75): 328P 3 746,
# 32U4 3 816, 2560 3 906; 2560 with OTA (session, swap-move, trial) 7 666, was
# 8 190; 2560 with the store 6 256.
#
# The last field is BOOT_STORE. The 2560 with both OTA and the store does not
# fit (8 916 / 8 192 on the element tier with ARCH_BOOT_OPT_CFLAGS; 9 616 at
# c319f07 without), so it is not a row, and each image takes one update path
# (#52): the gate below refuses the pair by name.
#
# The soft tier (#130) has no row without a signature check: every image is
# verified on every row (CA m453, owner item 79). An unverified OTA image is
# remote code execution. docs/dice-attestation/architecture.md, "The soft
# tfw-boot", has the design and the measurements.
#
# Soft rows, BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft at the default
# UECC_OPT, measured by lane-verify at 935494f with ld's own overflow message:
#
#   2560, no OTA, no store   fits, 8 190 / 8 192 -- 2 bytes spare. A row.
#                            8 064 / 8 192 with the DeviceID seed, after #173's
#                            rewrites and BOOT_SIG_OPT_CFLAGS (avr-gcc 14.2).
#   2560 with the store      2 680 bytes over. Not a row.
#   2560 with OTA            3 836 bytes over. Not a row.
#   328P, no OTA, no store   4 030 bytes over. Not a row.
#   32U4                     not a row.
#
# The 2560's store and OTA rows shed OTA, never verification (CA m467): with
# OTA shed they are the base row.
#
# A row that stops fitting is a build failure here, which is the point: the
# margin on a 4 KB part is 1 728 bytes and it is the kind of thing that
# disappears one commit at a time.
BOOT_SUPPORTED := \
  atmega328p:atecc608a:atecc608a:atecc608a:0:0 \
  atmega32u4:atecc608a:atecc608a:atecc608a:0:0 \
  atmega2560:atecc608a:atecc608a:atecc608a:0:0 \
  atmega2560:atecc608a:atecc608a:atecc608a:1:0 \
  atmega2560:atecc608a:atecc608a:atecc608a:0:1 \
  atmega2560:soft:soft:p256-soft:0:0 \
  mps2-an505:soft:soft:p256-soft:0:0 \
  mps2-an505:soft:soft:p256-soft:1:0 \
  mps2-an505:soft:soft:p256-soft:0:1 \
  mps2-an505:soft:soft:p256-soft:0:1:ab \
  mps2-an500:soft:soft:p256-soft:0:0 \
  mps2-an500:soft:soft:p256-soft:0:1 \
  mps2-an500:soft:soft:p256-soft:0:1:ab \
  stm32h573i-dk:soft:soft:p256-soft:0:0 \
  rp2350-pico2w:soft:soft:p256-soft:0:0 \
  nrf54l15-dk:soft:soft:p256-soft:0:0 \
  frdm-mcxn947:soft:soft:p256-soft:0:0

# A row's optional seventh field, ab, is BOOT_AB=1; a row without it leaves BOOT_AB to the board's profile.mk (1 on the 2560, 0 on the MPS2 boards).
bootloader-check: cm-frame-bound-check
	@fail=0; \
	for row in $(BOOT_SUPPORTED); do \
	  b=$${row%%:*}; r=$${row#*:}; \
	  t=$${r%%:*}; r=$${r#*:}; m=$${r%%:*}; r=$${r#*:}; \
	  g=$${r%%:*}; r=$${r#*:}; o=$${r%%:*}; r=$${r#*:}; s=$${r%%:*}; \
	  ab=; case $$r in *:ab) ab=BOOT_AB=1;; esac; \
	  if ! $(MAKE) --no-print-directory bootloader BOARD=$$b \
	        BOOT_TIER=$$t BOOT_MEASURE=$$m BOOT_SIG=$$g BOOT_OTA=$$o \
	        BOOT_STORE=$$s $$ab; then \
	    echo "SUPPORTED CONFIGURATION NO LONGER FITS: $$row"; fail=1; \
	  elif ! $(MAKE) --no-print-directory boot-load-end-check BOARD=$$b \
	        BOOT_TIER=$$t BOOT_MEASURE=$$m BOOT_SIG=$$g BOOT_OTA=$$o \
	        BOOT_STORE=$$s $$ab; then \
	    echo "SUPPORTED CONFIGURATION RUNS PAST FLASH: $$row"; fail=1; \
	  fi; \
	done; \
	exit $$fail

# The built boot's load end, __data_load_end, the last flash byte .text and .data's initial values occupy, is at most the part's flash.
# region's end address as ARCH_BOOT_LOAD_LIMIT (mk/arch/cortex-m.mk).
BOOT_LOAD_LIMIT := $(or $(ARCH_BOOT_LOAD_LIMIT),$(FLASH_TOTAL))
boot-load-end-check:
	@end=$$($(ARCH_NM) -P $(BOOT_TARGET).elf | awk '$$1 == "__data_load_end" { print $$3 }'); \
	 if [ -z "$$end" ]; then \
	   echo "$(BOOT_TARGET).elf has no __data_load_end"; exit 1; fi; \
	 if [ $$(( 0x$$end )) -gt $$(( $(BOOT_LOAD_LIMIT) )) ]; then \
	   echo "$(BOOT_TARGET).elf ends at 0x$$end, past the part's flash $(BOOT_LOAD_LIMIT)"; exit 1; fi

.PHONY: bootloader-check boot-load-end-check

BOOT_TEST_DIR   := $(HOST_DIR)/boot
BOOT_TEST_SRCS  := $(wildcard test/unit/boot/test_*.c)
BOOT_TEST_BINS  := $(patsubst test/unit/boot/%.c,$(BOOT_TEST_DIR)/%,$(BOOT_TEST_SRCS))

# DED25519_FORCE_32BIT so the host exercises the same 32-bit field arithmetic the AVR build compiles.
BOOT_TEST_CFLAGS := $(HOST_CFLAGS) -Iboot -Iboot/verify -Itest/mock \
                    -Isrc/third_party/ed25519-donna -DF_CPU=16000000UL
BOOT_DONNA_HOST  := -DED25519_REFHASH -DED25519_CUSTOMRANDOM -DED25519_NO_INLINE_ASM \
                    -DED25519_FORCE_32BIT -include boot/verify/donna_shim.h

# boot_policy.c is not here: it is the one unit that calls the verify seam, so every suite that linked it would have to provide a signature backend.
BOOT_LOGIC_SRCS := boot/boot_image.c boot/measure/measure_soft.c boot/boot_handoff.c boot/boot_ota.c \
                   boot/boot_platform.c boot/boot_session.c boot/boot_trial.c \
                   boot/boot_request.c boot/boot_entry.c \
                   src/crypto/sha256.c src/proto/crc16.c src/proto/crc16_update.c \
                   test/mock/boot_hal_mock.c

$(BOOT_TEST_DIR):
	@mkdir -p $(BOOT_TEST_DIR)

# The vendored unit, compiled once without our warning set, the same arrangement mk/host.mk uses for Unity and for the same reason.
$(BOOT_TEST_DIR)/donna_shim.o: boot/verify/donna_shim.c | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

$(BOOT_TEST_DIR)/donna.o: src/third_party/ed25519-donna/ed25519.c | $(BOOT_TEST_DIR)
	@$(CC) -std=gnu11 -g -O1 $(BOOT_DONNA_HOST) \
	   -Isrc/third_party/ed25519-donna $(EXTRA_CFLAGS) -c -o $@ $<

# The element-backed backends, against the software element.
$(BOOT_TEST_DIR)/test_boot_element: test/unit/boot/test_boot_element.c \
        boot/measure/measure_atecc608a.c boot/verify/verify_atecc608a.c \
        boot/boot_handoff.c src/crypto/atecc608a/atecc.c src/crypto/sha256.c \
        src/core/secure_zero.c src/proto/crc16.c src/proto/crc16_update.c \
        $(ELEMENT_MODEL) test/mock/boot_hal_mock.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# test_measure_atecc608a_mutate drives the element measurement on its own.
$(BOOT_TEST_DIR)/test_measure_atecc608a_mutate: test/unit/boot/test_measure_atecc608a_mutate.c \
        boot/measure/measure_atecc608a.c src/crypto/atecc608a/atecc.c src/core/secure_zero.c src/crypto/sha256.c \
        $(ELEMENT_MODEL) test/mock/boot_hal_mock.c boot/boot_handoff.c \
        src/proto/crc16.c src/proto/crc16_update.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# verify_atecc608a.c under mutation: the same element, the software hash, so the Nonce and the Verify are the only commands the part sees.
$(BOOT_TEST_DIR)/test_boot_verify_atecc_mutate: test/unit/boot/test_boot_verify_atecc_mutate.c \
        boot/verify/verify_atecc608a.c boot/measure/measure_soft.c src/crypto/atecc608a/atecc.c src/core/secure_zero.c \
        src/crypto/sha256.c $(ELEMENT_MODEL) test/mock/boot_hal_mock.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

$(BOOT_TEST_DIR)/test_boot_ed25519: test/unit/boot/test_boot_ed25519.c \
        boot/verify/verify_ed25519_donna.c $(BOOT_TEST_DIR)/donna.o $(BOOT_TEST_DIR)/donna_shim.o \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same backend with its key slot patched after compilation, as the jig patches it: RFC 8032 test 1's public key, so the read-out path runs.
ED25519_TEST_SLOT := \327\132\230\001\202\261\012\267\325\113\376\323\311\144\007\072\016\341\162\363\332\246\043\045\257\002\032\150\367\007\121\032

$(BOOT_TEST_DIR)/verify_ed25519_slot.o: boot/verify/verify_ed25519_donna.c | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -fdata-sections -c -o $@ $<
	@printf '$(ED25519_TEST_SLOT)' > $@.slot
	@objcopy --update-section .rodata.pubkey_slot=$@.slot $@

$(BOOT_TEST_DIR)/test_boot_ed25519_slot: test/unit/boot/test_boot_ed25519_slot.c \
        $(BOOT_TEST_DIR)/verify_ed25519_slot.o $(BOOT_TEST_DIR)/donna.o $(BOOT_TEST_DIR)/donna_shim.o \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

$(BOOT_TEST_DIR)/test_boot_policy: test/unit/boot/test_boot_policy.c \
        boot/boot_policy.c $(BOOT_LOGIC_SRCS) $(BOOT_TEST_DIR)/donna.o $(BOOT_TEST_DIR)/donna_shim.o \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same policy under mutation, with a recording stand-in for verify.
$(BOOT_TEST_DIR)/test_boot_policy_mutate: test/unit/boot/test_boot_policy_mutate.c \
        boot/boot_policy.c $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The application's SHA-256 suite, against the boot's size-first object: the same vectors, so sha256.c and sha256_small.c cannot disagree.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_sha256_small
$(BOOT_TEST_DIR)/test_sha256_small: test/unit/test_sha256.c src/crypto/sha256_small.c src/core/secure_zero.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same object against seeded mutants: the padding edges hashed every way, the length field's high word, the HMAC key lengths either side of a block.
$(BOOT_TEST_DIR)/test_sha256_small_mutate: test/unit/boot/test_sha256_small_mutate.c \
        src/crypto/sha256_small.c src/core/secure_zero.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)


# micro-ecc for BOOT_SIG=p256-soft, with the boot's UECC_DEFS.
UECC_WORD ?= 1
UECC_HOST_DEFS := $(UECC_DEFS) $(if $(UECC_WORD),-DuECC_WORD_SIZE=$(UECC_WORD))
$(BOOT_TEST_DIR)/uecc.o: src/third_party/micro-ecc/uECC.c | $(BOOT_TEST_DIR)
	@$(CC) -std=gnu11 -g -O1 $(UECC_HOST_DEFS) $(EXTRA_CFLAGS) -c -o $@ $<

# Every Wycheproof P-256 P1363 vector against micro-ecc, and the shipping backend's contract, and a trailer v1 signed as the element tier signs.
$(BOOT_TEST_DIR)/test_boot_p256_soft: test/unit/boot/test_boot_p256_soft.c \
        boot/verify/verify_p256_soft.c boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c \
        boot/boot_image.c boot/boot_platform.c src/proto/crc16.c src/proto/crc16_update.c \
        test/mock/boot_hal_mock.c $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(UECC_HOST_DEFS) \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same backend with the key slot that ships, erased: refuses every image.
$(BOOT_TEST_DIR)/test_boot_p256_slot: test/unit/boot/test_boot_p256_slot.c \
        boot/verify/verify_p256_soft.c boot/verify/p256_key_slot.c \
        boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c \
        test/mock/boot_hal_mock.c $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(UECC_HOST_DEFS) \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The slot's own mutants: the suite compiles p256_key_slot.c into its unit, so the slot's size is in view; the unit is data, with no line to cover.
$(BOOT_TEST_DIR)/test_boot_p256_key_slot_mutate: test/unit/boot/test_boot_p256_key_slot_mutate.c \
        boot/verify/p256_key_slot.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $(filter-out boot/verify/p256_key_slot.c,$^) \
	   $(EXTRA_LDFLAGS)

# The ESP32 layer 0's ES256 gate: trailer v1 and the SUIT manifest after an ESP-IDF image, against the mock HAL, with p256-soft and the shared policy primitives the bootloader links (boot/hal/esp32/bootloader_components/main/CMakeLists.txt).
L0_SUIT_HOST   := src/core/suit.c boot/hal/esp32/l0_crypto.c tools/suit-emit/emit.c
L0_TEST_CFLAGS := $(filter-out $(HOST_EXTRA_DEFS),\
                    $(subst -Iboards/$(HOST_BOARD) ,-Iboards/esp32-qemu ,$(BOOT_TEST_CFLAGS)))
$(BOOT_TEST_DIR)/test_boot_l0_esp32: test/unit/boot/test_boot_l0_esp32.c \
        boot/hal/esp32/l0_gate.c boot/boot_cose.c boot/boot_policy.c boot/boot_image.c boot/boot_platform.c \
        src/proto/crc16.c src/proto/crc16_update.c boot/verify/verify_p256_soft.c boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c \
        $(L0_SUIT_HOST) boot/hal/esp32/verify_digest_soft.c \
        test/mock/boot_hal_mock.c $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(L0_TEST_CFLAGS) $(UECC_HOST_DEFS) -Iboot/hal/esp32 -Itools/suit-emit \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same gate, seeded: linked exactly as above.
$(BOOT_TEST_DIR)/test_boot_l0_gate_mutate: test/unit/boot/test_boot_l0_gate_mutate.c \
        boot/hal/esp32/l0_gate.c boot/boot_cose.c boot/boot_policy.c boot/boot_image.c boot/boot_platform.c \
        src/proto/crc16.c src/proto/crc16_update.c boot/verify/verify_p256_soft.c boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c \
        $(L0_SUIT_HOST) boot/hal/esp32/verify_digest_soft.c \
        test/mock/boot_hal_mock.c $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(L0_TEST_CFLAGS) $(UECC_HOST_DEFS) -Iboot/hal/esp32 -Itools/suit-emit \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The P4's backend, the ROM's ECDSA (boot/hal/esp32/verify_p256_rom.c), against a host ROM over micro-ecc that takes the key and signature little-endian as ESP-IDF documents (test/mock/esp_rom_ecdsa_mock.c): every Wycheproof vector, then the same gate suite as above, so the P4's link is the one tested.
ESP_ROM_HOST := boot/hal/esp32/verify_p256_rom.c test/mock/esp_rom_ecdsa_mock.c
$(BOOT_TEST_DIR)/test_boot_p256_rom: test/unit/boot/test_boot_p256_rom.c $(ESP_ROM_HOST) \
        boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c test/mock/boot_hal_mock.c \
        $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(UECC_HOST_DEFS) \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The p256-soft row's digest verify, seeded: boot/hal/esp32/verify_digest_soft.c alone.
$(BOOT_TEST_DIR)/test_boot_verify_digest_soft_mutate: test/unit/boot/test_boot_verify_digest_soft_mutate.c \
        boot/hal/esp32/verify_digest_soft.c src/crypto/sha256_small.c src/core/secure_zero.c \
        $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(UECC_HOST_DEFS) \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_l0_esp32_rom
$(BOOT_TEST_DIR)/test_boot_l0_esp32_rom: test/unit/boot/test_boot_l0_esp32.c \
        boot/hal/esp32/l0_gate.c boot/boot_cose.c boot/boot_policy.c boot/boot_image.c boot/boot_platform.c \
        src/proto/crc16.c src/proto/crc16_update.c $(ESP_ROM_HOST) boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c \
        $(L0_SUIT_HOST) test/mock/boot_hal_mock.c $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(L0_TEST_CFLAGS) $(UECC_HOST_DEFS) -Iboot/hal/esp32 -Itools/suit-emit \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The ESP32 boot HAL's aligned read windows: the arithmetic boot_hal_esp32.c reads every piece through, because bootloader_flash_read refuses an unaligned source, length or destination.
$(BOOT_TEST_DIR)/test_boot_flash_span: test/unit/boot/test_boot_flash_span.c \
        boot/hal/esp32/flash_span.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/esp32 $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# Tfw-boot's STM32H5 flash driver, boot/hal/cortex-m/stm32h5/h5_flash.c, on a flash interface and a flash of the test's own: the test is the hardware, acting at the wait the part's boot_flash.c polls, so boot_flash.c is not here.
$(BOOT_TEST_DIR)/test_boot_stm32h5_flash: test/unit/boot/test_boot_stm32h5_flash.c \
        boot/hal/cortex-m/stm32h5/h5_flash.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Isrc/hal/cortex-m $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
# The STM32H5 SoC bring-up both images link, src/hal/cortex-m/stm32h5/hal_soc.c, over a register model under the stm32h573i-dk profile: the clock raise's wait states before the divider, the watchdog's keys.
BOOT_TEST_STM32H5_CFLAGS = $(subst -Iboards/$(HOST_BOARD),-Iboards/stm32h573i-dk,\
                             $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_h5_hal_soc
$(BOOT_TEST_DIR)/test_h5_hal_soc: src/hal/cortex-m/stm32h5/test/test_h5_hal_soc.c \
        src/hal/cortex-m/stm32h5/hal_soc.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_STM32H5_CFLAGS) -Iboot/hal/cortex-m/rp2350/test -Isrc/hal/cortex-m/stm32h5 \
	   -Isrc/hal/cortex-m $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
# The RP2350's sector read-modify-write (row rp2350): the arithmetic every tfw-boot flash write goes through, over a RAM flash the test supplies in place of rp_flash_rom.c's boot ROM calls.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_rp2350_flash
$(BOOT_TEST_DIR)/test_boot_rp2350_flash: boot/hal/cortex-m/rp2350/test/test_boot_rp2350_flash.c \
        boot/hal/cortex-m/rp2350/rp_flash.c boot/hal/cortex-m/rp2350/boot_flash.c \
        src/core/secure_zero.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_RP2350_CFLAGS) -Iboot/hal/cortex-m/rp2350 $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
# The MCXN947's flash writes (row mcxn947), boot/hal/cortex-m/mcxn94x/mcxn_flash.c, over a model of the boot ROM's flash driver in place of the ROM's: the test is the driver, so boot_flash.c, which finds the ROM's, is not here.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_mcxn_flash $(BOOT_TEST_DIR)/test_mcxn_uart
$(BOOT_TEST_DIR)/test_mcxn_flash: boot/hal/cortex-m/mcxn94x/test/test_mcxn_flash.c \
        boot/hal/cortex-m/mcxn94x/mcxn_flash.c src/core/secure_zero.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/cortex-m/mcxn94x $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
# Its polled LPUART, boot/hal/cortex-m/mcxn94x/mcxn_uart.c, over a register model of the LP_FLEXCOMM unit; boot_uart.c, which hands it LP_FLEXCOMM4 and sets the clock and the pins, is test_mcxn_boot_uart below.
$(BOOT_TEST_DIR)/test_mcxn_uart: boot/hal/cortex-m/mcxn94x/test/test_mcxn_uart.c \
        boot/hal/cortex-m/mcxn94x/mcxn_uart.c src/hal/cortex-m/mcxn94x/lpuart_baud.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/cortex-m/mcxn94x -Isrc/hal/cortex-m $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
# boot_uart.c itself under the FRDM-MCXN947's profile, which names the link's port and pins: the RP2350's test cm_regs.h comes ahead of the real one, so every SYSCON and PORT access lands in the test's register model, and the test stands in for mcxn_uart.c.
BOOT_TEST_MCXN_CFLAGS = $(subst -Iboards/$(HOST_BOARD),-Iboards/frdm-mcxn947,\
                          $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_mcxn_boot_uart
$(BOOT_TEST_DIR)/test_mcxn_boot_uart: boot/hal/cortex-m/mcxn94x/test/test_mcxn_boot_uart.c \
        boot/hal/cortex-m/mcxn94x/boot_uart.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_MCXN_CFLAGS) -Iboot/hal/cortex-m/rp2350/test -Iboot/hal/cortex-m/mcxn94x \
	  -Isrc/hal/cortex-m $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The Pico 2 W's flash map by literal, under its own profile: the regions tfw-boot writes, and the 4 MiB flash they must fit (pico-sdk's pico2_w.h).
BOOT_TEST_RP2350_CFLAGS = $(subst -Iboards/$(HOST_BOARD),-Iboards/rp2350-pico2w,\
                            $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))
$(BOOT_TEST_DIR)/test_boot_rp2350_map: test/unit/boot/test_boot_rp2350_map.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_RP2350_CFLAGS) -Iboot/hal/cortex-m/rp2350 $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# Tfw-boot's RP2350 UART over a register model.
RP2350_BOOT_TEST_DIR := boot/hal/cortex-m/rp2350/test
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_rp2350_uart
$(BOOT_TEST_DIR)/test_boot_rp2350_uart: $(RP2350_BOOT_TEST_DIR)/test_boot_rp2350_uart.c \
        boot/hal/cortex-m/rp2350/boot_uart.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_RP2350_CFLAGS) -I$(RP2350_BOOT_TEST_DIR) -Isrc/hal/cortex-m $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
# Its sector erase and program through the boot ROM, the same way: rp_flash_rom.c whole, over the test's ROM table model in place of rp_rom_lookup.c and its interrupt mask in place of cm_regs.h's.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_rp2350_rom
$(BOOT_TEST_DIR)/test_boot_rp2350_rom: $(RP2350_BOOT_TEST_DIR)/test_boot_rp2350_rom.c \
        boot/hal/cortex-m/rp2350/rp_flash_rom.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_RP2350_CFLAGS) -I$(RP2350_BOOT_TEST_DIR) -Iboot/hal/cortex-m/rp2350 $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
# The SoC half both images link, src/hal/cortex-m/rp2350/hal_soc.c: the clock bring-up, the watchdog and the reset cause over the same register model.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_rp2350_hal_soc
$(BOOT_TEST_DIR)/test_rp2350_hal_soc: $(RP2350_BOOT_TEST_DIR)/test_rp2350_hal_soc.c \
        src/hal/cortex-m/rp2350/hal_soc.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_RP2350_CFLAGS) -I$(RP2350_BOOT_TEST_DIR) -Isrc/hal/cortex-m \
	  -Isrc/hal/cortex-m/rp2350 $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# Tfw-boot's mps2 UART the RP2350's way, under mps2-an505's profile: its own cm_regs.h in boot/hal/cortex-m/mps2/test/comes ahead of src/hal/cortex-m, so every TFW_CM_REG goes to the test's model of UART0, which sees each store.
MPS2_BOOT_TEST_DIR := boot/hal/cortex-m/mps2/test
BOOT_TEST_MPS2_AN505_CFLAGS = $(subst -Iboards/$(HOST_BOARD),-Iboards/mps2-an505,\
                                $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_mps2_uart
$(BOOT_TEST_DIR)/test_boot_mps2_uart: $(MPS2_BOOT_TEST_DIR)/test_boot_mps2_uart.c \
        boot/hal/cortex-m/mps2/boot_uart.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_MPS2_AN505_CFLAGS) -I$(MPS2_BOOT_TEST_DIR) -Isrc/hal/cortex-m $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# Tfw-boot's nRF54L15 UART and RRAM write, the RP2350's way, under the DK's profile, over the nRF54L register model (test/model/nrf54l_model.c): test/model/cortex-m/nrf54l/comes ahead of src/hal/cortex-m/ so its cm_regs.h sends every TFW_CM_REG to the model.
BOOT_TEST_NRF54L_CFLAGS = -Itest/model/cortex-m/nrf54l -Itest/model -Isrc/hal/cortex-m \
                          $(subst -Iboards/$(HOST_BOARD),-Iboards/nrf54l15-dk,\
                            $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))
NRF54L_BOOT_TEST_DIR := boot/hal/cortex-m/nrf54l/test
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_nrf54l_uart
$(BOOT_TEST_DIR)/test_boot_nrf54l_uart: $(NRF54L_BOOT_TEST_DIR)/test_boot_nrf54l_uart.c \
        boot/hal/cortex-m/nrf54l/boot_uart.c src/hal/cortex-m/nrf54l/soc_tables.c \
        test/model/nrf54l_model.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_NRF54L_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_nrf54l_rram
$(BOOT_TEST_DIR)/test_boot_nrf54l_rram: $(NRF54L_BOOT_TEST_DIR)/test_boot_nrf54l_rram.c \
        boot/hal/cortex-m/nrf54l/boot_rram.c src/hal/cortex-m/nrf54l/rram_write.h \
        src/hal/cortex-m/nrf54l/soc_tables.c test/model/nrf54l_model.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_NRF54L_CFLAGS) $(EXTRA_CFLAGS) -o $@ \
	  $(filter-out %/boot_rram.c %.h,$^) $(EXTRA_LDFLAGS)

# The MCXN947's early clock and WWDT0, src/hal/cortex-m/mcxn94x/hal_soc.c, which both images link, the RP2350's way under the FRDM-MCXN947's profile: the RP2350's test cm_regs.h comes ahead of the real one, so every SYSCON, SCG and WWDT access lands in the test's register model and the interrupt mask is the test's; the test stands in for soc_reset.c.
MCXN_SOC_TEST_CFLAGS = $(subst -Iboards/$(HOST_BOARD),-Iboards/frdm-mcxn947,\
                         $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_mcxn_hal_soc
$(BOOT_TEST_DIR)/test_mcxn_hal_soc: src/hal/cortex-m/mcxn94x/test/test_mcxn_hal_soc.c \
        src/hal/cortex-m/mcxn94x/hal_soc.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(MCXN_SOC_TEST_CFLAGS) -I$(RP2350_BOOT_TEST_DIR) -Isrc/hal/cortex-m/mcxn94x \
	  -Isrc/hal/cortex-m $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# host-test runs the register-model suites of tfw-boot's RP2350, nRF54L and mps2 halves and the MCXN947's boot UART as well as boot-test does.
BOOT_SOC_HOST_TEST_BINS := $(BOOT_TEST_DIR)/test_boot_rp2350_uart $(BOOT_TEST_DIR)/test_boot_rp2350_rom \
                           $(BOOT_TEST_DIR)/test_rp2350_hal_soc \
                           $(BOOT_TEST_DIR)/test_boot_nrf54l_uart $(BOOT_TEST_DIR)/test_boot_nrf54l_rram \
                           $(BOOT_TEST_DIR)/test_boot_mps2_uart \
                           $(BOOT_TEST_DIR)/test_mcxn_boot_uart
.PHONY: boot-soc-host-test
boot-soc-host-test: $(BOOT_SOC_HOST_TEST_BINS) $(addsuffix .run,$(BOOT_SOC_HOST_TEST_BINS))
	@$(call test-verdict,$(BOOT_SOC_HOST_TEST_BINS))
host-test: boot-soc-host-test

# The ESP32 boot HAL itself, boot/hal/esp32/boot_hal_esp32.c, over stand-ins for the ESP-IDF bootloader calls it makes, which live beside its test (test/unit/boot/boot_hal_esp32/).
BOOT_HAL_ESP32_HOST := boot/hal/esp32/boot_hal_esp32.c boot/hal/esp32/flash_span.c \
        boot/hal/esp32/l0_gate.c boot/boot_cose.c boot/boot_policy.c boot/boot_image.c boot/boot_platform.c \
        src/proto/crc16.c src/proto/crc16_update.c boot/verify/verify_p256_soft.c boot/verify/p256_key_slot.c \
        boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c $(L0_SUIT_HOST) boot/hal/esp32/verify_digest_soft.c \
        $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o
$(BOOT_TEST_DIR)/test_boot_hal_esp32_mutate: test/unit/boot/test_boot_hal_esp32_mutate.c \
        $(BOOT_HAL_ESP32_HOST) | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(L0_TEST_CFLAGS) $(UECC_HOST_DEFS) -Itest/unit/boot/boot_hal_esp32 -Iboot/hal/esp32 \
	   -Itools/suit-emit -Isrc/third_party/micro-ecc -DCONFIG_IDF_TARGET_ESP32P4=0 \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_hal_esp32_mutate_p4
$(BOOT_TEST_DIR)/test_boot_hal_esp32_mutate_p4: test/unit/boot/test_boot_hal_esp32_mutate.c \
        $(BOOT_HAL_ESP32_HOST) | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(L0_TEST_CFLAGS) $(UECC_HOST_DEFS) -Itest/unit/boot/boot_hal_esp32 -Iboot/hal/esp32 \
	   -Itools/suit-emit -Isrc/third_party/micro-ecc -DCONFIG_IDF_TARGET_ESP32P4=1 \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)


# The ESP32 layer 0's hash, boot/hal/esp32/measure_esp32.c, over the bootloader SHA as the C3 and the P4 build it and, with CONFIG_IDF_TARGET_ESP32, over the ROM SHA as the classic ESP32 does; test/mock/esp_sha_mock.c holds each call to its silicon's contract.
ESP_SHA_HOST := boot/hal/esp32/measure_esp32.c test/mock/esp_sha_mock.c src/crypto/sha256_small.c src/core/secure_zero.c
$(BOOT_TEST_DIR)/test_boot_measure_esp32: test/unit/boot/test_boot_measure_esp32.c $(ESP_SHA_HOST) \
        test/mock/boot_hal_mock.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Itest/mock/esp32 -DCONFIG_IDF_TARGET_ESP32=0 \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_measure_esp32_classic
$(BOOT_TEST_DIR)/test_boot_measure_esp32_classic: test/unit/boot/test_boot_measure_esp32.c $(ESP_SHA_HOST) \
        test/mock/boot_hal_mock.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Itest/mock/esp32 -DCONFIG_IDF_TARGET_ESP32=1 \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_l0_esp32_sha
$(BOOT_TEST_DIR)/test_boot_l0_esp32_sha: test/unit/boot/test_boot_l0_esp32.c \
        boot/hal/esp32/l0_gate.c boot/boot_cose.c boot/boot_policy.c boot/boot_image.c boot/boot_platform.c \
        src/proto/crc16.c src/proto/crc16_update.c boot/verify/verify_p256_soft.c $(ESP_SHA_HOST) \
        $(L0_SUIT_HOST) boot/hal/esp32/verify_digest_soft.c \
        test/mock/boot_hal_mock.c $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(L0_TEST_CFLAGS) $(UECC_HOST_DEFS) -Iboot/hal/esp32 -Itools/suit-emit \
	   -Itest/mock/esp32 -DCONFIG_IDF_TARGET_ESP32=0 \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# l0_crypto.c alone: over measure_soft.c's hash, then over measure_esp32.c's, the hash the C3 and the P4 link.
$(BOOT_TEST_DIR)/test_boot_l0_crypto_mutate: test/unit/boot/test_boot_l0_crypto_mutate.c \
        boot/hal/esp32/l0_crypto.c boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c \
        test/mock/boot_hal_mock.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_l0_crypto_mutate_sha
$(BOOT_TEST_DIR)/test_boot_l0_crypto_mutate_sha: test/unit/boot/test_boot_l0_crypto_mutate.c \
        boot/hal/esp32/l0_crypto.c $(ESP_SHA_HOST) test/mock/boot_hal_mock.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Itest/mock/esp32 -DCONFIG_IDF_TARGET_ESP32=0 \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# boot_ota_store_avr.c alone: register-level over test/mock/avr_regs's <avr/io.h>, under the host board's pins, then under the 2560's.
$(BOOT_TEST_DIR)/test_boot_ota_store_avr_mutate: test/unit/boot/test_boot_ota_store_avr_mutate.c \
        boot/hal/avr/boot_ota_store_avr.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Itest/mock/avr_regs $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_ota_store_avr_mutate_2560
$(BOOT_TEST_DIR)/test_boot_ota_store_avr_mutate_2560: test/unit/boot/test_boot_ota_store_avr_mutate.c \
        boot/hal/avr/boot_ota_store_avr.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) -Itest/mock/avr_regs $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# recovery_halt.c alone: cli and power-down over test/mock/avr_regs.
$(BOOT_TEST_DIR)/test_boot_recovery_halt_avr_mutate: test/unit/boot/test_boot_recovery_halt_avr_mutate.c \
        boot/hal/avr/recovery_halt.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Itest/mock/avr_regs $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# boot/hal/cortex-m/recovery_halt.c alone, with the framer it sends its EVENT through, under the MPS2 AN505's profile.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_cm_recovery_halt
$(BOOT_TEST_DIR)/test_boot_cm_recovery_halt: boot/hal/cortex-m/test/test_boot_cm_recovery_halt.c \
        boot/hal/cortex-m/recovery_halt.c src/proto/frame.c src/proto/crc16.c src/proto/crc16_update.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) -Iboot -Itest/model/cortex-m -Itest/model -Isrc/hal/cortex-m -Isrc/proto \
	   $(subst -Iboards/$(HOST_BOARD),-Iboards/mps2-an505,$(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS))) \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# boot_hal_avr.c alone: the part's FLASHEND, SPM_PAGESIZE and, on the 2560, EIND come from -D, as avr-libc brings them from the part's header.
BOOT_HAL_AVR_TEST_CFLAGS = -Itest/mock/avr_regs -Isrc/hal/avr -Wno-int-to-pointer-cast
$(BOOT_TEST_DIR)/test_boot_hal_avr_mutate: test/unit/boot/test_boot_hal_avr_mutate.c \
        boot/hal/avr/boot_hal_avr.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(BOOT_HAL_AVR_TEST_CFLAGS) -DFLASHEND=0x7FFFul -DSPM_PAGESIZE=128 \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_hal_avr_mutate_2560
$(BOOT_TEST_DIR)/test_boot_hal_avr_mutate_2560: test/unit/boot/test_boot_hal_avr_mutate.c \
        boot/hal/avr/boot_hal_avr.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(BOOT_HAL_AVR_TEST_CFLAGS) -DFLASHEND=0x3FFFFul -DSPM_PAGESIZE=256 \
	   '-DEIND=(*tfw_avr_reg(TFW_AVR_EIND))' $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# boot_hal_cortex-m.c alone, under the MPS2 AN505's profile, over boot/hal/cortex-m/test/'s stubs for the SoC's flash write, the factory copy and the early latch.
CM_BOOT_TEST_DIR := boot/hal/cortex-m/test
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_hal_cortex-m
$(BOOT_TEST_DIR)/test_boot_hal_cortex-m: $(CM_BOOT_TEST_DIR)/test_boot_hal_cortex-m.c \
        $(CM_BOOT_TEST_DIR)/boot_hal_cortex-m_stubs.c boot/hal/cortex-m/boot_hal_cortex-m.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) -Iboot/hal/cortex-m -Itest/model/cortex-m -Isrc/hal/cortex-m \
	   $(subst -Iboards/$(HOST_BOARD),-Iboards/mps2-an505,$(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS))) \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The two DICE layer 0 tiers (BOOT_TIER_SRCS above). test_boot_cdi_soft links cdi_soft.c as built, its UDS slot the erased placeholder.
CDI_SOFT_TEST_UDS := cdi_soft.c TEST UDS, not a key!!

$(BOOT_TEST_DIR)/cdi_soft_uds.o: boot/tier/cdi_soft.c | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -fdata-sections -c -o $@ $<
	@printf '%s' '$(CDI_SOFT_TEST_UDS)' > $@.uds
	@objcopy --update-section .rodata.uds_slot=$@.uds $@

$(BOOT_TEST_DIR)/test_boot_cdi_soft: test/unit/boot/test_boot_cdi_soft.c \
        boot/tier/cdi_soft.c src/crypto/sha256_small.c src/core/secure_zero.c test/mock/boot_hal_mock.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

$(BOOT_TEST_DIR)/test_boot_cdi_soft_uds: test/unit/boot/test_boot_cdi_soft_uds.c \
        $(BOOT_TEST_DIR)/cdi_soft_uds.o src/crypto/sha256_small.c src/core/secure_zero.c test/mock/boot_hal_mock.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# Test_boot_cdi_soft_uds's link with uds_slot made global, so the mutation test can flip the patched slot's bits in place.
$(BOOT_TEST_DIR)/cdi_soft_mut.o: $(BOOT_TEST_DIR)/cdi_soft_uds.o
	@objcopy --globalize-symbol=uds_slot $< $@

$(BOOT_TEST_DIR)/test_boot_cdi_soft_mutate: test/unit/boot/test_boot_cdi_soft_mutate.c \
        $(BOOT_TEST_DIR)/cdi_soft_mut.o src/crypto/sha256_small.c src/core/secure_zero.c test/mock/boot_hal_mock.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

$(BOOT_TEST_DIR)/test_boot_cdi_atecc: test/unit/boot/test_boot_cdi_atecc.c \
        boot/tier/cdi_atecc.c src/crypto/atecc608a/atecc.c src/crypto/sha256.c \
        src/core/secure_zero.c src/proto/crc16.c src/proto/crc16_update.c \
        $(ELEMENT_MODEL) test/mock/boot_hal_mock.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The element tier's mutation tests: the test file is the driver's wake, exec and sleep and the I2C init, so the driver and the model stay out.
$(BOOT_TEST_DIR)/test_boot_cdi_atecc_mutate: test/unit/boot/test_boot_cdi_atecc_mutate.c \
        boot/tier/cdi_atecc.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/crypto/atecc608a \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The ESP32 layer 0's own unit, boot_l0.c, driven through call_start_cpu0.
$(BOOT_TEST_DIR)/test_boot_l0_esp32_mutate: test/unit/boot/test_boot_l0_esp32_mutate.c \
        boot/hal/esp32/boot_l0.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) -Itest/unit/boot/boot_l0_esp32 $(L0_TEST_CFLAGS) -Iboot/hal/esp32 $(EXTRA_CFLAGS) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# HW Req 6.4: no copy of the CDI left once a tier has handed it over.
$(BOOT_TEST_DIR)/test_boot_cdi_soft_clear: test/unit/boot/test_boot_cdi_soft_clear.c \
        $(BOOT_TEST_DIR)/cdi_soft_uds.o src/crypto/sha256_small.c test/mock/cdi_residue.c \
        test/mock/boot_hal_mock.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

$(BOOT_TEST_DIR)/test_boot_cdi_atecc_clear: test/unit/boot/test_boot_cdi_atecc_clear.c \
        boot/tier/cdi_atecc.c src/crypto/atecc608a/atecc.c src/crypto/sha256_small.c \
        test/mock/cdi_residue.c src/proto/crc16.c src/proto/crc16_update.c \
        $(ELEMENT_MODEL) test/mock/boot_hal_mock.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The hand-over itself: boot_main.c as built, its main renamed so Unity's is the program's, over the soft tier with the patched slot and the boot's hash in place of the application's.
$(BOOT_TEST_DIR)/boot_main_ho.o: boot/boot_main.c | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/avr $(EXTRA_CFLAGS) -c -o $@ $<
	@objcopy --redefine-sym main=tfw_boot_main_not_linked $@

# Both hand-over tests wrap the derive so they read the stack the moment it returns: the calls the hand-over makes after it, at the same depth, overwrite its frame, and a scan after the jump finds the UDS's copy gone whether or not the derive cleared it.
DERIVE_WRAP := -Wl,--wrap=tfw_boot_cdi_derive

$(BOOT_TEST_DIR)/test_boot_hand_over_clear: test/unit/boot/test_boot_hand_over_clear.c \
        $(BOOT_TEST_DIR)/boot_main_ho.o $(BOOT_TEST_DIR)/cdi_soft_uds.o \
        src/crypto/sha256_small.c test/mock/cdi_residue.c \
        boot/recovery/recovery_ota.c boot/boot_store.c boot/boot_swap_none.c \
        boot/boot_stage_none.c src/proto/frame.c \
        $(filter-out src/crypto/sha256.c,$(BOOT_LOGIC_SRCS)) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/host $(EXTRA_CFLAGS) -o $@ $^ -Wl,-z,now $(DERIVE_WRAP) $(EXTRA_LDFLAGS)

# The same hand-over on an unprovisioned device: cdi_soft.c as it ships, its UDS slot erased.
$(BOOT_TEST_DIR)/test_boot_hand_over_erased: test/unit/boot/test_boot_hand_over_erased.c \
        $(BOOT_TEST_DIR)/boot_main_ho.o boot/tier/cdi_soft.c \
        src/crypto/sha256_small.c test/mock/cdi_residue.c \
        boot/recovery/recovery_ota.c boot/boot_store.c boot/boot_swap_none.c \
        boot/boot_stage_none.c src/proto/frame.c \
        $(filter-out src/crypto/sha256.c,$(BOOT_LOGIC_SRCS)) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/host $(EXTRA_CFLAGS) -o $@ $^ -Wl,-z,now $(DERIVE_WRAP) $(EXTRA_LDFLAGS)

# The session loop with tfw_boot_evaluate and tfw_boot_hand_over supplied by the test, so boot_policy.c and the signature backend stay out of this link.
$(BOOT_TEST_DIR)/test_boot_recovery_session: test/unit/boot/test_boot_recovery_session.c \
        boot/recovery/recovery_ota.c boot/boot_store.c boot/boot_swap_none.c src/proto/frame.c \
        $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/host $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The faults the session suite let through, planted in recovery_ota.c: linked exactly as above.
$(BOOT_TEST_DIR)/test_boot_recovery_ota_mutate: test/unit/boot/test_boot_recovery_ota_mutate.c \
        boot/recovery/recovery_ota.c boot/boot_store.c boot/boot_swap_none.c src/proto/frame.c \
        $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/host $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The settle against the floor's authority: the real boot_store.c over the mock EEPROM, with the element's counter supplied by the test.
$(BOOT_TEST_DIR)/test_boot_settle: test/unit/boot/test_boot_settle.c \
        boot/boot_store.c $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same link under a block and a request rewritten at random.
$(BOOT_TEST_DIR)/test_boot_state_mutate: test/unit/boot/test_boot_state_mutate.c \
        boot/boot_store.c $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# Boot_store.c's settle under a refused block write, a part-way counter and seeded floors: the same link again.
$(BOOT_TEST_DIR)/test_boot_store_mutate: test/unit/boot/test_boot_store_mutate.c \
        boot/boot_store.c $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The external store's read, check and copy, against the software nor part on the mock's store bus.
BOOT_STAGE_TEST_SRCS := test/unit/boot/test_boot_stage.c boot/boot_stage.c boot/boot_cose.c \
                        test/model/spi_nor_model.c test/mock/boot_verify_mock.c $(BOOT_LOGIC_SRCS)

$(BOOT_TEST_DIR)/test_boot_stage: $(BOOT_STAGE_TEST_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# Again under the 2560's profile, where STAGE (64 KB) is smaller than the application region and the trailer that ends the staged stream is copied to the region's end, far past it.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_stage_2560
BOOT_TEST_2560_CFLAGS = $(subst -Iboards/$(HOST_BOARD),-Iboards/atmega2560,\
                          $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))

$(BOOT_TEST_DIR)/test_boot_stage_2560: $(BOOT_STAGE_TEST_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The geometry suite under the 2560's profile, the pattern rule's link: its boot section, and the write end the A/B secondary sets, are that profile's.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_geometry_2560
$(BOOT_TEST_DIR)/test_boot_geometry_2560: test/unit/boot/test_boot_geometry.c $(BOOT_LOGIC_SRCS) \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The unprovisioned hand-over under the 2560's profile, the soft row's: the link above, with boot_main.c compiled against that profile too.
$(BOOT_TEST_DIR)/boot_main_ho_2560.o: boot/boot_main.c | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) -Iboot/hal/avr $(EXTRA_CFLAGS) -c -o $@ $<
	@objcopy --redefine-sym main=tfw_boot_main_not_linked $@

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_hand_over_erased_2560
$(BOOT_TEST_DIR)/test_boot_hand_over_erased_2560: test/unit/boot/test_boot_hand_over_erased.c \
        $(BOOT_TEST_DIR)/boot_main_ho_2560.o boot/tier/cdi_soft.c \
        src/crypto/sha256_small.c test/mock/cdi_residue.c \
        boot/recovery/recovery_ota.c boot/boot_store.c boot/boot_swap_none.c \
        boot/boot_stage_none.c src/proto/frame.c \
        $(filter-out src/crypto/sha256.c,$(BOOT_LOGIC_SRCS)) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) -Iboot/hal/host $(EXTRA_CFLAGS) -o $@ $^ -Wl,-z,now $(DERIVE_WRAP) $(EXTRA_LDFLAGS)

# And under the 32U4's, the third tfw-boot row in BOOT_SUPPORTED.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_geometry_32u4
BOOT_TEST_32U4_CFLAGS = $(subst -Iboards/$(HOST_BOARD),-Iboards/atmega32u4,\
                          $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))

$(BOOT_TEST_DIR)/test_boot_geometry_32u4: test/unit/boot/test_boot_geometry.c $(BOOT_LOGIC_SRCS) \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_32U4_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The MPS2 A/B slot geometry, boot/hal/cortex-m/boot_slots.h, under each MPS2 profile with TFW_CM_AB as its A/B rows build.
BOOT_TEST_MPS2_BINS := $(BOOT_TEST_DIR)/test_boot_slots_mps2-an505 $(BOOT_TEST_DIR)/test_boot_slots_mps2-an500
BOOT_TEST_BINS := $(filter-out $(BOOT_TEST_DIR)/test_boot_slots_mps2,$(BOOT_TEST_BINS)) $(BOOT_TEST_MPS2_BINS)
BOOT_TEST_MPS2_CFLAGS = -Iboot/hal/cortex-m $(subst -Iboards/$(HOST_BOARD),-Iboards/$(1),\
                          $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS))) -DTFW_CM_AB=1

# A static pattern, so the binaries' .run targets are not taken for a board.
$(BOOT_TEST_MPS2_BINS): $(BOOT_TEST_DIR)/test_boot_slots_mps2-%: test/unit/boot/test_boot_slots_mps2.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(call BOOT_TEST_MPS2_CFLAGS,mps2-$*) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The factory copy of the A/B rows, on host memory mapped at mps2-an505's addresses; tfw_cm_dsb comes through the rp2350 test cm_regs.h.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_cm_boot_factory
# host-test runs it too, through boot-soc-host-test: the rule above expanded its prerequisites when it was read, so the binary joins them here; the verdict reads BOOT_SOC_HOST_TEST_BINS when it runs and so checks its exit status.
BOOT_SOC_HOST_TEST_BINS += $(BOOT_TEST_DIR)/test_cm_boot_factory
boot-soc-host-test: $(BOOT_TEST_DIR)/test_cm_boot_factory $(BOOT_TEST_DIR)/test_cm_boot_factory.run
$(BOOT_TEST_DIR)/test_cm_boot_factory: boot/hal/cortex-m/test/test_cm_boot_factory.c \
        boot/hal/cortex-m/boot_factory.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) -I$(RP2350_BOOT_TEST_DIR) $(call BOOT_TEST_MPS2_CFLAGS,mps2-an505) $(EXTRA_CFLAGS) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The SoC file both MPS2 images link, src/hal/cortex-m/mps2/hal_soc.c, the RP2350 UART's way: its test's cm_regs.h comes ahead of src/hal/cortex-m, so every TFW_CM_REG lands in the test's access log.
MPS2_SOC_TEST_DIR := src/hal/cortex-m/mps2/test
BOOT_TEST_MPS2_SOC_BINS := $(BOOT_TEST_DIR)/test_mps2_soc_wdog-an505 $(BOOT_TEST_DIR)/test_mps2_soc_wdog-an500
BOOT_TEST_BINS += $(BOOT_TEST_MPS2_SOC_BINS)
MPS2_WDOG_BASE_an505 := 0x50081000u
MPS2_WDOG_BASE_an500 := 0x40008000u
# The real SSP and SBCon accessors ride the same binaries, their bases by literal too (shield 0's PL022 and SBCon, the FPGA I/O block).
MPS2_SPI_BASE_an505 := 0x4020A000u
MPS2_SPI_BASE_an500 := 0x40026000u
MPS2_I2C_BASE_an505 := 0x4020C000u
MPS2_I2C_BASE_an500 := 0x40029000u
MPS2_FPGAIO_BASE_an505 := 0x40302000u
MPS2_FPGAIO_BASE_an500 := 0x40028000u
BOOT_TEST_MPS2_SOC_CFLAGS = -I$(RP2350_BOOT_TEST_DIR) -Isrc/hal/cortex-m/mps2 -Isrc/hal/cortex-m \
                            $(subst -Iboards/$(HOST_BOARD),-Iboards/mps2-$(1),\
                              $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS))) \
                            -DTEST_WDOG_BASE=$(MPS2_WDOG_BASE_$(1)) -DTEST_SPI_BASE=$(MPS2_SPI_BASE_$(1)) \
                            -DTEST_I2C_BASE=$(MPS2_I2C_BASE_$(1)) -DTEST_FPGAIO_BASE=$(MPS2_FPGAIO_BASE_$(1))
$(BOOT_TEST_MPS2_SOC_BINS): $(BOOT_TEST_DIR)/test_mps2_soc_wdog-%: $(MPS2_SOC_TEST_DIR)/test_mps2_soc_wdog.c \
        src/hal/cortex-m/mps2/hal_soc.c src/hal/cortex-m/mps2/pl022.c src/hal/cortex-m/mps2/sbcon.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(call BOOT_TEST_MPS2_SOC_CFLAGS,$*) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same take over a store rewritten at random: test_boot_stage's link.
$(BOOT_TEST_DIR)/test_boot_stage_mutate: test/unit/boot/test_boot_stage_mutate.c boot/boot_stage.c boot/boot_cose.c \
        test/model/spi_nor_model.c test/mock/boot_verify_mock.c $(BOOT_LOGIC_SRCS) \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The MPS2 boards' memory image store, the application's driver and tfw-boot's together over one host array: the model header is forced into every unit, so both address the array as they address the board's RAM.
$(BOOT_TEST_DIR)/test_store_mem: test/unit/boot/test_store_mem.c src/store/mem.c \
        boot/hal/cortex-m/boot_ota_store_cortex-m.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -Isrc/core \
	   -include test/model/store_mem_model.h -o $@ $^ $(EXTRA_LDFLAGS)

# The OTA_ACTIVATE Sig_structure against literal bytes: boot_cose.c alone, the algorithm byte the test's.
$(BOOT_TEST_DIR)/test_boot_cose_mutate: test/unit/boot/test_boot_cose_mutate.c boot/boot_cose.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The swap-move under the 2560's geometry (boot/hal/avr/boot_slots.h; the MPS2 boards' is pinned by test_boot_slots_mps2 and runs under QEMU in CI).
$(BOOT_TEST_DIR)/test_boot_swap: test/unit/boot/test_boot_swap.c boot/boot_swap.c \
                                 boot/recovery/recovery_ota.c src/proto/frame.c \
                                 boot/boot_stage.c boot/boot_cose.c test/model/spi_nor_model.c test/mock/boot_verify_mock.c \
                                 $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) -Iboot/hal/host $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same link under a journal and a confirm request rewritten at random.
$(BOOT_TEST_DIR)/test_boot_swap_mutate: test/unit/boot/test_boot_swap_mutate.c boot/boot_swap.c \
                                        boot/recovery/recovery_ota.c src/proto/frame.c \
                                        boot/boot_stage.c boot/boot_cose.c test/model/spi_nor_model.c test/mock/boot_verify_mock.c \
                                        $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) -Iboot/hal/host $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

$(BOOT_TEST_DIR)/test_boot_recovery_ota_swap_mutate: test/unit/boot/test_boot_recovery_ota_swap_mutate.c boot/boot_swap.c \
                                        boot/recovery/recovery_ota.c src/proto/frame.c \
                                        boot/boot_stage.c boot/boot_cose.c test/model/spi_nor_model.c test/mock/boot_verify_mock.c \
                                        $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) -Iboot/hal/host $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The 328P's fallback: PREV copied back by the store's copy loop, under the host board's profile, which is the 328P's.
$(BOOT_TEST_DIR)/test_boot_prev: test/unit/boot/test_boot_prev.c boot/boot_prev.c \
                                 boot/boot_stage.c boot/boot_cose.c test/model/spi_nor_model.c test/mock/boot_verify_mock.c \
                                 $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same fallback under a hostile store, seeded: linked exactly as above.
$(BOOT_TEST_DIR)/test_boot_prev_mutate: test/unit/boot/test_boot_prev_mutate.c boot/boot_prev.c \
                                        boot/boot_stage.c boot/boot_cose.c test/model/spi_nor_model.c test/mock/boot_verify_mock.c \
                                        $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The simavr harness's store select and UART, from each store board's profile: the host board's (the 328P's), then the 2560's and the 32U4's.
BOOT_HARNESS_IO_SRCS := test/unit/boot/test_boot_harness_io.c $(HOST_DIR)/unity.o
BOOT_HARNESS_IO_CFLAGS = $(BOOT_TEST_CFLAGS) -Itest/integration/simavr
BOOT_TEST_32U4_CFLAGS = $(subst -Iboards/$(HOST_BOARD),-Iboards/atmega32u4,\
                          $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))

$(BOOT_TEST_DIR)/test_boot_harness_io: $(BOOT_HARNESS_IO_SRCS) | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_HARNESS_IO_CFLAGS) -DEXPECT_STORE_CS_PORT="'B'" -DEXPECT_STORE_CS_BIT=0u \
	   -DEXPECT_UART="'0'" -DEXPECT_UCSRA=0xC0u $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_harness_io_2560
$(BOOT_TEST_DIR)/test_boot_harness_io_2560: $(BOOT_HARNESS_IO_SRCS) | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) -Itest/integration/simavr \
	   -DEXPECT_STORE_CS_PORT="'H'" -DEXPECT_STORE_CS_BIT=5u \
	   -DEXPECT_UART="'0'" -DEXPECT_UCSRA=0xC0u $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_harness_io_32u4
$(BOOT_TEST_DIR)/test_boot_harness_io_32u4: $(BOOT_HARNESS_IO_SRCS) | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_32U4_CFLAGS) -Itest/integration/simavr \
	   -DEXPECT_STORE_CS_PORT="'B'" -DEXPECT_STORE_CS_BIT=4u \
	   -DEXPECT_UART="'1'" -DEXPECT_UCSRA=0xC8u $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The rows with no staging and no swap: the two stand-ins alone, under a seeded hostile block.
$(BOOT_TEST_DIR)/test_boot_none_mutate: test/unit/boot/test_boot_none_mutate.c \
                                        boot/boot_stage_none.c boot/boot_swap_none.c \
                                        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# Boot_main.c's main as tfw_boot_main_ut, against the test's own stand-ins for every unit it calls: the sequence, not the callees.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_main_seq
$(BOOT_TEST_DIR)/test_boot_main_seq.o: boot/boot_main.c | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -Iboot/hal/avr -Dmain=tfw_boot_main_ut -c -o $@ $<

$(BOOT_TEST_DIR)/test_boot_main_seq: test/unit/boot/boot_main/test_boot_main.c \
                                     $(BOOT_TEST_DIR)/test_boot_main_seq.o \
                                     $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -Iboot/hal/avr -o $@ $^ $(EXTRA_LDFLAGS)

# Tfw-boot's own main on the host board's in-place row, renamed here so the suite's main can call it: the order of the trial's writes, the watchdog and the jump is tested in the file the device links, and nothing is added to tfw-boot for it.
$(BOOT_TEST_DIR)/test_boot_main: test/unit/boot/test_boot_main.c boot/boot_main.c \
                                 boot/boot_policy.c boot/boot_store.c \
                                 boot/boot_stage_none.c boot/boot_swap_none.c \
                                 src/core/secure_zero.c test/mock/boot_verify_mock.c \
                                 $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/avr $(EXTRA_CFLAGS) -c -Dmain=tfw_boot_main \
	   -o $(BOOT_TEST_DIR)/boot_main_host.o boot/boot_main.c
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ \
	   $(filter-out boot/boot_main.c,$(filter %.c %.o,$^)) $(BOOT_TEST_DIR)/boot_main_host.o \
	   $(EXTRA_LDFLAGS)

$(BOOT_TEST_DIR)/%: test/unit/boot/%.c $(BOOT_LOGIC_SRCS) \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

## boot-test: run the bootloader's host unit tests
boot-test: $(BOOT_TEST_BINS) $(addsuffix .run,$(BOOT_TEST_BINS))
	@$(call test-verdict,$(BOOT_TEST_BINS))

## bootloader: build tfw-boot for BOARD and enforce the boot-section budget
bootloader: $(BOOT_TARGET).hex boot-size-check
	@echo "built $(BOOT_TARGET).hex"

## boot-dir: print the directory `bootloader` builds tfw-boot in, for the flags given (used by scripts/test_build_dirs.py)
boot-dir:
	@echo $(BOOT_DIR_OUT)

## boot-integration-dir: print the directory `integration-test-boot` builds its harness in, for the flags given (used by scripts/test_build_dirs.py)
boot-integration-dir:
	@echo $(BOOT_INT_DIR)

.PHONY: boot-dir boot-integration-dir

## boot-size-check: fail when tfw-boot does not fit in the boot section
boot-size-check: $(BOOT_TARGET).elf
	@flash=$$($(ARCH_SIZE) --format=berkeley $< | awk 'NR==2{print $$1+$$2}'); \
	 ram=$$($(ARCH_SIZE)  --format=berkeley $< | awk 'NR==2{print $$2+$$3}'); \
	 printf 'tfw-boot %-10s flash %6s / %-6s   ram %5s / %-5s (%s tier, %s)\n' \
	   "$(BOARD)" "$$flash" "$(BOOT_BUDGET)" "$$ram" "$(RAM_TOTAL)" \
	   "$(BOOT_TIER)" "$(BOOT_SIG)"; \
	 if [ "$$flash" -gt "$(BOOT_BUDGET)" ]; then \
	   echo "BOOT SECTION BUDGET EXCEEDED: $$flash > $(BOOT_BUDGET) by $$((flash-$(BOOT_BUDGET))) bytes"; \
	   echo "run 'make bootloader-budget' for the per-component breakdown"; \
	   exit 1; fi

## bootloader-budget: what each part of tfw-boot costs, measured
BOOT_MEAS_DIR := $(BOOT_DIR_OUT)/measure
BOOT_CORE_SRCS := $(filter-out $(BOOT_SIG_SRCS),$(BOOT_SRCS))

bootloader-budget:
	@mkdir -p $(BOOT_MEAS_DIR)
	@echo "tfw-boot component sizes -- $(BOARD), $(BOOT_TIER) tier, $(BOOT_SIG)"
	@echo "budget: $(BOOT_BUDGET) bytes (BOOTSZ $(BOOT_SECTION) bytes, part flash $(FLASH_TOTAL), part SRAM $(RAM_TOTAL))"
	@echo
	@core=0; \
	 for src in $(BOOT_CORE_SRCS); do \
	   o=$(BOOT_MEAS_DIR)/$$(echo $$src | tr / _).o; \
	   $(ARCH_CC) $(BOOT_CFLAGS) -fno-lto -c -o $$o $$src 2>/dev/null; \
	   n=$$($(ARCH_SIZE) --format=berkeley $$o | awk 'NR==2{print $$1+$$2}'); \
	   printf '  %-52s %7s\n' "$$src" "$$n"; core=$$((core+n)); \
	 done; \
	 printf '  %-52s %7s\n' "-- core, summed before --gc-sections" "$$core"; \
	 echo; \
	 for src in $(BOOT_SIG_SRCS); do \
	   o=$(BOOT_MEAS_DIR)/$$(echo $$src | tr / _).o; \
	   $(ARCH_CC) $(if $(findstring third_party,$(src)),$(DONNA_CFLAGS),$(BOOT_CFLAGS)) -fno-lto -c -o $$o $$src 2>/dev/null || \
	     $(ARCH_CC) $(DONNA_CFLAGS) -fno-lto -c -o $$o $$src 2>/dev/null || \
	     $(ARCH_CC) $(UECC_BOOT_CFLAGS) -fno-lto -c -o $$o $$src 2>/dev/null; \
	   n=$$($(ARCH_SIZE) --format=berkeley $$o | awk 'NR==2{print $$1+$$2}'); \
	   printf '  %-52s %7s\n' "$$src" "$$n"; \
	 done
	@echo
	@$(MAKE) --no-print-directory boot-core-size
	@echo
	@echo "signature backend, linked and gc-sectioned (verify reachable only):"
	@$(ARCH_CC) $(BOOT_SIG_SU_CFLAGS) -fno-lto -fstack-usage -c \
	   -o $(BOOT_MEAS_DIR)/su.o $(BOOT_SIG_SU_SRC) 2>/dev/null; \
	 mv $(BOOT_MEAS_DIR)/su.su $(BOOT_MEAS_DIR)/sig.su 2>/dev/null || \
	   mv su.su $(BOOT_MEAS_DIR)/sig.su 2>/dev/null || \
	   mv $(basename $(notdir $(BOOT_SIG_SU_SRC))).su $(BOOT_MEAS_DIR)/sig.su 2>/dev/null || true; \
	 awk -F'\t' '$$1 ~ /$(BOOT_SIG_SU_FN)$$/ {printf "  peak stack frame  %7s bytes   (part SRAM %s)\n", $$2, "$(RAM_TOTAL)"}' \
	   $(BOOT_MEAS_DIR)/sig.su 2>/dev/null || true

## boot-core-size: tfw-boot WITHOUT a signature backend, linked and measured
boot-core-size:
	@mkdir -p $(BOOT_MEAS_DIR)
	@for src in $(BOOT_CORE_SRCS); do \
	   o=$(BOOT_MEAS_DIR)/lto_$$(echo $$src | tr / _).o; \
	   $(ARCH_CC) $(BOOT_CFLAGS) -frandom-seed=$$(basename $$src) -c -o $$o $$src; \
	 done
	@# The image's own startup and no crt, so the core is the size it ships at.
	@$(if $(ARCH_BOOT_START_SRC),$(ARCH_CC) $(ARCH_MACH_CFLAGS) -c \
	   -o $(BOOT_MEAS_DIR)/lto_start.o $(ARCH_BOOT_START_SRC))
	@# Linked at address zero, NOT at the boot section's start. The question is
	@# how many bytes the core is, and placing it at 0x7000 makes an image that
	@# is merely too big fail to link instead of reporting its size -- which is
	@# the one thing this target exists to do.
	@$(ARCH_CC) $(ARCH_LDFLAGS) $(ARCH_BOOT_START_LDFLAGS) $(BOOT_OPT_EXTRA) \
	   $(if $(ARCH_BOOT_STACK_TOP),-Wl$(BOOT_COMMA)--defsym=__stack=$(ARCH_BOOT_STACK_TOP)) \
	   -Wl,--defsym=tfw_boot_verify=0 -Wl,--defsym=tfw_boot_pubkey=0 \
	   -Wl,--defsym=tfw_boot_cose_alg=0 \
	   -o $(BOOT_MEAS_DIR)/core.elf $(BOOT_MEAS_DIR)/lto_*.o
	@flash=$$($(ARCH_SIZE) --format=berkeley $(BOOT_MEAS_DIR)/core.elf | awk 'NR==2{print $$1+$$2}'); \
	 ram=$$($(ARCH_SIZE) --format=berkeley $(BOOT_MEAS_DIR)/core.elf | awk 'NR==2{print $$2+$$3}'); \
	 printf 'tfw-boot core, linked (measure + verify seam + rollback + EEPROM + CDI + OTA + UART + framing):\n'; \
	 printf '  flash %6s / %-6s   ram %5s / %-5s\n' "$$flash" "$(BOOT_BUDGET)" "$$ram" "$(RAM_TOTAL)"; \
	 printf '  a signature backend has %s bytes of flash and %s bytes of SRAM to fit in\n' \
	   "$$(($(BOOT_BUDGET)-flash))" "$$(($(RAM_TOTAL)-ram))"

## bootloader-reproducible: two tfw-boot builds from clean copies of HEAD, diffed
bootloader-reproducible:
	@python3 scripts/repro_build.py --make "$(MAKE)" --image $(BOOT_TARGET).hex \
	   --dir /tmp/agboot-a --dir /tmp/agboot-b -- \
	   BOARD=$(BOARD) BOOT_TIER=$(BOOT_TIER) BOOT_MEASURE=$(BOOT_MEASURE) \
	   BOOT_SIG=$(BOOT_SIG) BOOT_OTA=$(BOOT_OTA) BOOT_STORE=$(BOOT_STORE) \
	   $(BOOT_TARGET).hex

## bootloader-reproducible-check: bootloader-reproducible on the 2560 soft base row
bootloader-reproducible-check:
	@$(MAKE) --no-print-directory bootloader-reproducible BOARD=atmega2560 \
	   BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=0

## boot-flash: write tfw-boot to a connected board (AVR: ISP; Cortex-M: the debug probe, after board-record)
BOOT_ROW  := $(BOARD):$(BOOT_TIER):$(BOOT_MEASURE):$(BOOT_SIG):$(BOOT_OTA):$(BOOT_STORE)$(if $(AB_SUFFIX),:ab)
BOOT_FITS := $(filter $(BOARD):%,$(BOOT_SUPPORTED))
# A literal comma, which $(if) would otherwise read as its argument separator.
BOOT_COMMA := ,
BOOT_GATED_GOAL := $(firstword $(filter bootloader boot-flash boot-bench boot-flash-uds,$(MAKECMDGOALS)))
ifneq ($(BOOT_GATED_GOAL),)
# One update path per image: tfw-boot opens a session over the UART or installs what the application staged in the external store, never both.
ifeq ($(BOOT_OTA)$(BOOT_STORE),11)
$(error $(BOOT_GATED_GOAL): BOOT_OTA=1 BOOT_STORE=1 is two update paths, and an image takes one. Choose BOOT_OTA=1 BOOT_STORE=0, OTA in tfw-boot, for an application built OTA_STAGE=0; or BOOT_OTA=0 BOOT_STORE=1, the external store, for an application built OTA_STAGE=1. See docs/ota/architecture.md$(BOOT_COMMA) "The 2560 with OTA and the store")
endif
ifeq ($(filter $(BOOT_ROW),$(BOOT_SUPPORTED)),)
$(error $(BOOT_GATED_GOAL): $(BOOT_ROW) (BOARD:BOOT_TIER:BOOT_MEASURE:BOOT_SIG:BOOT_OTA:BOOT_STORE) is not a supported configuration$(if $(BOOT_AB_OPTIONAL), (a trailing :ab is BOOT_AB=1)). $(if $(BOOT_FITS),For $(BOARD) the supported rows$(BOOT_COMMA) which fit$(BOOT_COMMA) are: $(BOOT_FITS),No tfw-boot configuration fits $(BOARD)). See BOOT_SUPPORTED in mk/boot.mk and docs/secure-boot/cli.md)
endif
endif

## bootloader-candidate: measure a row that is not in BOOT_SUPPORTED yet
BOOT_CANDIDATE ?=
BOOT_CAND_FIELDS := $(subst :, ,$(BOOT_CANDIDATE))
BOOT_CAND_BOARD  := $(word 1,$(BOOT_CAND_FIELDS))
ifneq ($(filter bootloader-candidate,$(MAKECMDGOALS)),)
ifneq ($(words $(BOOT_CAND_FIELDS)),6)
$(error bootloader-candidate: BOOT_CANDIDATE="$(BOOT_CANDIDATE)" is not a row. Name six fields, BOARD:BOOT_TIER:BOOT_MEASURE:BOOT_SIG:BOOT_OTA:BOOT_STORE, e.g. BOOT_CANDIDATE=atmega32u4:atecc608a:atecc608a:atecc608a:0:1)
endif
ifeq ($(wildcard boards/$(BOOT_CAND_BOARD)/profile.mk),)
$(error bootloader-candidate: no such board "$(BOOT_CAND_BOARD)" -- expected boards/$(BOOT_CAND_BOARD)/profile.mk, one of: $(notdir $(wildcard boards/*)))
endif
ifneq ($(filter $(BOOT_CANDIDATE),$(BOOT_SUPPORTED)),)
$(error bootloader-candidate: $(BOOT_CANDIDATE) is already in BOOT_SUPPORTED; make bootloader-check builds it)
endif
endif
BOOT_CAND_VARS = BOARD=$(BOOT_CAND_BOARD) BOOT_TIER=$(word 2,$(BOOT_CAND_FIELDS)) \
                 BOOT_MEASURE=$(word 3,$(BOOT_CAND_FIELDS)) BOOT_SIG=$(word 4,$(BOOT_CAND_FIELDS)) \
                 BOOT_OTA=$(word 5,$(BOOT_CAND_FIELDS)) BOOT_STORE=$(word 6,$(BOOT_CAND_FIELDS))

bootloader-candidate:
	@if $(MAKE) --no-print-directory boot-size-check $(BOOT_CAND_VARS) && \
	    $(MAKE) --no-print-directory boot-load-end-check $(BOOT_CAND_VARS); then \
	   echo "CANDIDATE FITS: $(BOOT_CANDIDATE) -- not a row until it is in BOOT_SUPPORTED"; \
	 else \
	   echo "CANDIDATE DOES NOT FIT: $(BOOT_CANDIDATE)"; exit 1; \
	 fi

.PHONY: bootloader-candidate

ifneq ($(filter boot-flash boot-flash-uds,$(MAKECMDGOALS)),)
ifeq ($(ARCH),avr)
ifeq ($(and $(BOOT_EESAVE_FUSE),$(BOOT_EESAVE_BIT)),)
$(error $(firstword $(filter boot-flash boot-flash-uds,$(MAKECMDGOALS))): boards/$(BOARD)/profile.mk names no BOOT_EESAVE_FUSE/BOOT_EESAVE_BIT, so boot-flash cannot check that the chip erase keeps the EEPROM)
endif
endif
endif

# On AVR the recipe reads EESAVE before the chip erase and refuses an erase that would take the EEPROM; see ARCH_BOOT_FLASH in mk/arch/$(ARCH).mk.
boot-flash: ISP_RERUN = BOOT_TIER=$(BOOT_TIER) BOOT_MEASURE=$(BOOT_MEASURE) BOOT_SIG=$(BOOT_SIG) \
                        BOOT_OTA=$(BOOT_OTA) BOOT_STORE=$(BOOT_STORE)
# BOOT_FLASH_HEX is the image it writes: the build's, unless mk/boot-bench.mk's boot-flash-uds names its temporary copy with the UDS in it.
BOOT_FLASH_HEX ?= $(BOOT_TARGET).hex
boot-flash: $(BOOT_FLASH_HEX)
	@$(ARCH_BOOT_FLASH)

## lock-check: read the lock bits back over ISP; refuse unless they lock the UDS
ifneq ($(filter lock-check,$(MAKECMDGOALS)),)
ifeq ($(BOOT_LOCK_PROGRAMMED),)
$(error lock-check: REFUSED. boards/$(BOARD)/profile.mk names no BOOT_LOCK_PROGRAMMED, so nothing says which lock bits protect the UDS on this part; the profile names the data sheet they come from)
endif
endif

lock-check:
	@$(ARCH_LOCK_CHECK)

.PHONY: boot-test bootloader boot-size-check bootloader-budget boot-core-size bootloader-reproducible bootloader-reproducible-check boot-flash lock-check

# simavr integration: the real tfw-boot and application images and a modelled ATECC608A.
IMAGE_FLAGS ?= 0x00

# Its own variable rather than mk/integration.mk's INT_DIR: that file is included after this one, so INT_DIR is still empty here and the rule target would expand to "/boot_harness", a path at the filesystem root, and a "no rule to make target" that names something nobody wrote.
BOOT_INT_DIR := $(BUILD_DIR)/integration/$(BOARD)$(if $(filter-out 0,$(UECC_OPT)),-uecc$(UECC_OPT))$(if \
                $(filter-out 0x00,$(IMAGE_FLAGS)),-flags$(IMAGE_FLAGS))

# Which row of BOOT_SUPPORTED this runs; it is not the default board.
BOOT_INT_BOARD := atmega2560
BOOT_INT_ROW   := BOOT_TIER=atecc608a BOOT_MEASURE=atecc608a BOOT_SIG=atecc608a BOOT_OTA=1 \
                  BOOT_STORE=0

# The harness's half that names the store's PREV: only a board whose profile has one links it, the board that tfw-boot's store row gives boot_prev.c above: an OTA_STORE, one slot (no BOOT_AB) and BOOT_PREV=1.
BOOT_INT_HAS_PREV := $(if $(OTA_STORE),$(if $(filter 1,$(BOOT_AB)),,$(if $(filter 1,$(BOOT_PREV)),1)))
BOOT_INT_PREV_SRC := test/integration/simavr/boot_harness_prev_none.c
ifeq ($(BOOT_INT_HAS_PREV),1)
BOOT_INT_PREV_SRC := test/integration/simavr/boot_harness_prev.c
endif

# The same warning set as the firmware.
BOOT_HARNESS_AB_SRC := test/integration/simavr/boot_harness_ab_none.c
ifeq ($(BOOT_AB),1)
BOOT_HARNESS_AB_SRC := test/integration/simavr/boot_harness_ab.c
endif

$(BOOT_INT_DIR)/boot_harness: test/integration/simavr/boot_harness.c $(BOOT_HARNESS_AB_SRC) $(BOOT_INT_PREV_SRC) \
        src/proto/crc16.c src/proto/crc16_update.c \
        src/crypto/sha256.c src/crypto/atecc608a/atecc.c src/core/secure_zero.c \
        boot/boot_image.c boot/boot_platform.c boot/boot_request.c \
        test/model/spi_nor_model.c test/model/w5x00_model.c $(BOOT_INT_DIR)/uECC.o
	@mkdir -p $(BOOT_INT_DIR)
	@$(HOST_CC) -std=c11 -g -O1 -Wall -Wextra -Werror -Wconversion -Wshadow \
	   -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Isrc/crypto/atecc608a -Iboot -Itest/model -Itest/integration/simavr \
	   -Isrc/third_party/micro-ecc $(UECC_DEFS) \
	   -I$(HAL_DIR) -I$(BOARD_DIR) -DTFW_BOARD_HEADER='"board_profile.h"' \
	   -DTFW_SIMAVR_MCU='"$(MCU)"' -DTFW_IMAGE_FLAGS='$(IMAGE_FLAGS)u' \
	   -o $@ $^ -lsimavr -lelf
	@echo "built $@"

# The soft row's signer: the one vendored micro-ecc, built for the host without our warning set, as every vendored tree is.
$(BOOT_INT_DIR)/uECC.o: src/third_party/micro-ecc/uECC.c
	@mkdir -p $(BOOT_INT_DIR)
	@$(HOST_CC) -std=gnu11 -O1 $(UECC_DEFS) -Isrc/third_party/micro-ecc -c -o $@ $<

## integration-test-boot: run tfw-boot under simavr
BOOT_INT_SOFT_ROW := BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 \
                     BOOT_STORE=0
BOOT_INT_STORE_ROW := BOOT_TIER=atecc608a BOOT_MEASURE=atecc608a BOOT_SIG=atecc608a BOOT_OTA=0 \
                      BOOT_STORE=1

# The first row's application is CRYPTO=soft by name: test_signed_image_boots (test/integration/simavr/boot_harness.c) proves a soft application refuses the element-derived CDI tfw-boot hands off.
integration-test-boot:
	@$(MAKE) --no-print-directory boot-harness-run \
	   BOARD=$(BOOT_INT_BOARD) $(BOOT_INT_ROW) ETH=0 CRYPTO=soft
	@$(MAKE) --no-print-directory boot-harness-run \
	   BOARD=$(BOOT_INT_BOARD) $(BOOT_INT_STORE_ROW) ETH=0 BOOT_HARNESS_MODE=store
	@$(MAKE) --no-print-directory boot-bridge-run \
	   BOARD=$(BOOT_INT_BOARD) $(BOOT_INT_STORE_ROW) ETH=0 OTA_STAGE=1 CRYPTO=soft
	@$(MAKE) --no-print-directory boot-harness-run \
	   BOARD=$(BOOT_INT_BOARD) $(BOOT_INT_STORE_ROW) ETH=1 BOOT_HARNESS_MODE=net CRYPTO=soft
	@$(MAKE) --no-print-directory boot-harness-run \
	   BOARD=$(BOOT_INT_BOARD) $(BOOT_INT_STORE_ROW) ETH=1 BOOT_HARNESS_MODE=net-gate CRYPTO=auto
	@$(MAKE) --no-print-directory boot-harness-run \
	   BOARD=$(BOOT_INT_BOARD) $(BOOT_INT_SOFT_ROW) ETH=0 BOOT_HARNESS_MODE=soft

## integration-test-boot-prev: BOARD's BOOT_STORE=1 restore from PREV under simavr
ifneq ($(filter integration-test-boot-prev,$(MAKECMDGOALS)),)
ifneq ($(BOOT_INT_HAS_PREV),1)
$(error integration-test-boot-prev: $(BOARD) has no PREV in a store (boards/$(BOARD)/profile.mk: $(if $(OTA_STORE),$(if $(filter 1,$(BOOT_AB)),BOOT_AB=1$(BOOT_COMMA) it falls back to its secondary slot,BOOT_PREV=0$(BOOT_COMMA) it has no boot-time rollback),no OTA_STORE)). Name a one-slot board with a store: BOARD=<board>)
endif
endif
integration-test-boot-prev:
	@$(MAKE) --no-print-directory boot-harness-run \
	   BOARD=$(BOARD) $(BOOT_INT_STORE_ROW) ETH=0 BOOT_HARNESS_MODE=prev

# The same store row from the host: the application built with the stager (OTA_STAGE=1, mk/firmware.mk), the harness in its bridge mode, and the Go SDK staging into it over the emulated UART.
boot-bridge-run: $(BOOT_INT_DIR)/boot_harness $(BOOT_TARGET).elf $(TARGET).elf
	@cd $(SDK_DIR) && TFW_BOOT_HARNESS=$(abspath $(BOOT_INT_DIR)/boot_harness) \
	   TFW_BOOT_ELF=$(abspath $(BOOT_TARGET).elf) TFW_APP_ELF=$(abspath $(TARGET).elf) \
	   $(GO) test -count=1$(call go-tags,-tags,simavr) -run '^TestOTAStageThroughTfwBoot$$' ./cmd/tfwctl/

# The application .elf is a prerequisite and not an assumption: "a correctly signed image boots" is observed by the application answering CAPS after the jump, so the image in the emulated flash has to be code that runs.
boot-harness-run: $(BOOT_INT_DIR)/boot_harness $(BOOT_TARGET).elf $(TARGET).elf
	@./$(BOOT_INT_DIR)/boot_harness $(BOOT_TARGET).elf $(TARGET).elf $(BOOT_HARNESS_MODE)

.PHONY: integration-test-boot integration-test-boot-prev boot-harness-run boot-bridge-run

# tfw_boot_measure through the boot HAL flash on the ESP32 measure.
$(BOOT_TEST_DIR)/test_boot_measure_esp32_flash: test/unit/boot/test_boot_measure_esp32_flash.c $(ESP_SHA_HOST) \
        test/mock/boot_hal_mock.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Itest/mock/esp32 -DCONFIG_IDF_TARGET_ESP32=0 \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
