# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# tfw-boot: a SECOND, SEPARATE image.
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
#   SHA-256               the measurement must be the hash the application
#                         reports and a verifier recomputes. The boot links
#                         src/crypto/sha256_small.c, the same API and digest
#                         as the application's sha256.c in a third of the
#                         bytes; test/unit/test_sha256.c runs on both (#130)
#   src/proto/frame.c     one codec, so OTA frames and application frames are
#                         framed the same way by construction
#   $(HAL_DIR)/hal_early.c  one "clear MCUSR and disarm the watchdog", because
#                         the copy that is wrong bootloops the part
#
# Like mk/firmware.mk this file names no architecture: the HAL is $(HAL_DIR),
# the bootloader's own is $(BOOT_HAL_DIR), and the toolchain, the placement flags
# and the programmer are mk/arch/$(ARCH).mk's.

# tfw-boot's own hardware layer -- flash, EEPROM, UART, handoff placement, jump
# and reset -- one file per architecture, named for it.
BOOT_HAL_DIR := boot/hal/$(ARCH)

# The SoC's half of the bootloader's HAL, on an architecture that splits its
# HAL by SoC as mk/firmware.mk's HAL_SOC_DIR does: every .c under
# boot/hal/<arch>/<soc>/, the SoC file both images share for what
# $(HAL_DIR)/hal_early.c asks of the part (the watchdog and the reset cause, on
# Cortex-M), the SoC's register-free tables where it has them (nrf54l's
# soc_tables.c: the baud table, the PSEL encoding, the revision and the RRAM's
# bounds), and its reset-cause decode where it has a register for one (nrf54l's
# soc_reset.c), and its LPUART divisor search where it has one (mcxn94x's
# lpuart_baud.c, the arithmetic both images' UARTs share). Empty on a board
# that names no SOC.
BOOT_SOC_SRCS := $(if $(SOC),$(wildcard $(BOOT_HAL_DIR)/$(SOC)/*.c $(HAL_DIR)/$(SOC)/hal_soc.c \
                                        $(HAL_DIR)/$(SOC)/soc_tables.c $(HAL_DIR)/$(SOC)/soc_reset.c \
                                        $(HAL_DIR)/$(SOC)/lpuart_baud.c))


# The tier, chosen at LINK time exactly as CRYPTO is for the application.
#
# The defaults name the board's supported row (item 79, owner ruling
# 2026-10-17): a 4 KB section has only the element row, which verifies on the
# element; the 2560 defaults to its soft row, which verifies P-256 in software
# and sheds OTA (CA m467). An explicit BOOT_*= still wins over every default.
BOOT_TIER ?= $(if $(filter 4096,$(BOOT_SECTION)),atecc608a,soft)
ifeq ($(BOOT_TIER),soft)
BOOT_TIER_SRCS := boot/tier/cdi_soft.c
else ifeq ($(BOOT_TIER),atecc608a)
# The element driver the APPLICATION links, not a second copy of it. hal_bus.c
# and hal_sys.c come along for tfw_hal_i2c_xfer and tfw_hal_delay_us; everything
# else in them is unreferenced and --gc-sections drops it.
BOOT_TIER_SRCS := boot/tier/cdi_atecc.c src/crypto/atecc608a/atecc.c \
                  $(HAL_DIR)/hal_bus.c $(HAL_DIR)/hal_sys.c src/core/board_lookup.c \
                  boards/$(BOARD)/board.c
else
$(error unknown BOOT_TIER "$(BOOT_TIER)" -- expected soft or atecc608a)
endif

# The signature backend. ONE is linked.
#
# The default is the row's P-256 verifier (item 79, owner ruling 2026-10-17):
# the element's on the element tier, p256-soft on the soft tier. The measuring
# targets, bootloader-budget and boot-core-size, keep ed25519-donna: they exist
# for the rows that do not fit, and boot/boot_verify.h quotes what donna
# measures on this part. They reproduce the numbers rather than asking anyone
# to believe them.
BOOT_MEASURING_GOAL := $(filter bootloader-budget boot-core-size,$(MAKECMDGOALS))
BOOT_SIG ?= $(if $(BOOT_MEASURING_GOAL),ed25519-donna,$(if $(filter atecc608a,$(BOOT_TIER)),atecc608a,p256-soft))
BOOT_SIG_OPT_CFLAGS :=
ifeq ($(BOOT_SIG),ed25519-donna)
BOOT_SIG_SRCS := boot/verify/verify_ed25519_donna.c boot/verify/donna_shim.c \
                 src/third_party/ed25519-donna/ed25519.c
else ifeq ($(BOOT_SIG),atecc608a)
# The element verifies. P-256 ECDSA happens in silicon, so the boot section
# frames two commands and holds no curve arithmetic at all -- which is the only
# way an asymmetric signature fits a 4 KB section on this family.
BOOT_SIG_SRCS := boot/verify/verify_atecc608a.c
else ifeq ($(BOOT_SIG),p256-soft)
# P-256 ECDSA in software (#130, CA m453): the element's signature over the
# same trailer v1, so one signed image serves both tiers. Every row verifies;
# a row that does not fit sheds OTA, never this. micro-ecc v1.2, vendored, is
# configured by UECC_DEFS below and no vendored byte is edited.
BOOT_SIG_SRCS := boot/verify/verify_p256_soft.c boot/verify/p256_key_slot.c \
                 src/third_party/micro-ecc/uECC.c
BOOT_SIG_SU_SRC    := src/third_party/micro-ecc/uECC.c
BOOT_SIG_SU_CFLAGS  = $(UECC_BOOT_CFLAGS)
BOOT_SIG_SU_FN     := uECC_verify
# Two code-size options for this backend on AVR, every tfw-boot object and
# micro-ecc (#173): avr-gcc 14.2 splits each 32-bit value into bytes and hoists
# loop invariants into registers it then saves, and both cost the 2560's soft
# row more than the 8 KB section holds. The element rows build without them,
# so their images are unchanged.
BOOT_SIG_OPT_CFLAGS := $(if $(filter avr,$(ARCH)),-fno-split-wide-types -fno-move-loop-invariants)
else
$(error unknown BOOT_SIG "$(BOOT_SIG)" -- expected ed25519-donna, atecc608a or p256-soft)
endif
# What bootloader-budget reports the peak stack frame of: donna's verify unless
# the arm names its own.
BOOT_SIG_SU_SRC    ?= src/third_party/ed25519-donna/ed25519.c
BOOT_SIG_SU_CFLAGS ?= $(DONNA_CFLAGS)
BOOT_SIG_SU_FN     ?= ed25519_sign_open

# Where the measurement is computed.
#
# The hash is the single largest thing in the boot section -- 4 052 bytes of
# software SHA-256 against a 4 096-byte section on most of this family. The
# element has a SHA engine, so on a 4 KB part the hash can move into it and the
# boot section holds no crypto at all.
#
# The digest is identical either way: SHA-256 is defined over the byte sequence,
# not over how the bytes were delivered. What differs is time -- the element
# hashes 64 bytes per command, so a 28 KB image is about 450 round trips.
BOOT_MEASURE ?= $(BOOT_TIER)
ifeq ($(BOOT_MEASURE),soft)
# The size-first SHA-256 (#130): the same digest, sha256.h's API, a third of
# the bytes. The application keeps sha256.c for its speed.
BOOT_MEASURE_SRCS := boot/measure/measure_soft.c src/crypto/sha256_small.c
else ifeq ($(BOOT_MEASURE),atecc608a)
BOOT_MEASURE_SRCS := boot/measure/measure_atecc608a.c
else
$(error unknown BOOT_MEASURE "$(BOOT_MEASURE)" -- expected soft or atecc608a)
endif

# OTA in the boot section, or not.
#
# The update path is the single largest thing in the core after the hash, and it
# is the only part of a bootloader that is not strictly necessary there: an
# application can stage an image and reboot into a bootloader that only
# measures, verifies and commits. On a part whose boot section is 8 KB the
# question does not arise; on a 4 KB part it is the difference between having
# secure boot and not.
# On by default only where a row has it: the element verifier on an 8 KB
# section. A 4 KB row and the soft row shed OTA, never verification.
BOOT_OTA ?= $(if $(and $(filter atecc608a,$(BOOT_SIG)),$(filter-out 4096,$(BOOT_SECTION))),1,0)
ifeq ($(BOOT_OTA),1)
BOOT_OTA_SRCS := boot/recovery/recovery_ota.c boot/boot_ota.c boot/boot_session.c src/proto/frame.c
BOOT_OTA_DEFS :=
else
BOOT_OTA_SRCS := $(BOOT_HAL_DIR)/recovery_halt.c
BOOT_OTA_DEFS :=
endif

# The application-staged image in the external OTA store (issue #75, ADR 6.3),
# or not. The path an application without a session in the boot section
# updates by: boot_stage.c reads the stager's mark, checks the staged image
# again and copies it in. Read only -- the store's bus is
# $(BOOT_HAL_DIR)/boot_ota_store_<arch>.c, three opcodes, and not the
# application's driver. Off, a stub that never finds an image is linked in its
# place, and LTO folds its one call away.
#
# Its own axis and off by default, like BOOT_OTA and for the same reason: it is
# a size trade, and the 4 KB parts cannot afford it. The board profile's
# OTA_STORE says only that the part is there to be read; whether this image
# reads it is the configuration's to say, in a BOOT_SUPPORTED row.
BOOT_STORE ?= 0
ifeq ($(BOOT_STORE),1)
ifeq ($(OTA_STORE),)
$(error BOOT_STORE=1 but $(BOARD_DIR)/profile.mk names no OTA_STORE -- this board has no external store)
endif
BOOT_STORE_SRCS := boot/boot_stage.c boot/boot_cose.c $(BOOT_HAL_DIR)/boot_ota_store_$(ARCH).c
else
BOOT_STORE_SRCS := boot/boot_stage_none.c
endif

# The 2560's swap-move, on a board with two slots (BOOT_AB in profile.mk) and
# only where an image lands in the secondary: the store's copy, or the serial
# session's (TFW_OTA_STAGE_DEST). A row with neither has nothing to swap in,
# and links the stub, as does every other board.
#
# On a board with one slot and PREV in its store (the 32U4 and the MPS2 boards,
# #52), the store row links boot_prev.c in the same place: the fallback copies
# PREV back instead of swapping. The 328P's profile sets BOOT_PREV ?= 0 (owner
# queue 176, option (a)): its store row keeps the signature check and the
# copy but links the stub, so a trial that does not confirm and a pending image
# the gate refuses have no boot-time rollback; they end in recovery_halt.c and
# the part is recovered by reflash (docs/SPEC.md, the 328P row).
BOOT_PREV ?= 1
#
# boot_swap.c verifies OTA_CONFIRM's signature (#231) with the Sig_structure
# boot_cose.c builds, so it brings boot_cose.c along on the serial row too; the
# store row already links it, and BOOT_SRCS's sort drops the second name.
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

# Sorted, for the same reason mk/firmware.mk sorts: a wildcard's order depends on the
# filesystem, and link order affects the image, so an unsorted list makes the
# reproducibility check pass on one machine and fail on another.
# The CONFIGURATION is part of the output path, exactly as the crypto tier is
# for the application. tfw-boot with the element's hash and tfw-boot with the
# software hash are different images with different measurements; sharing one
# directory meant the second build silently reused the first one's objects and
# reported its size, which is a measurement that cannot be wrong and is.
# UECC_OPT and BOOT_OPT_EXTRA (below) change the objects too, and name the path
# only where they are set: micro-ecc's level where p256-soft links it, and a
# checksum of BOOT_OPT_EXTRA, whose options can hold any character a path cannot.
BOOT_CFG     := $(BOOT_TIER)-$(BOOT_MEASURE)-$(BOOT_SIG)-ota$(BOOT_OTA)-store$(BOOT_STORE)$(AB_SUFFIX)$(if \
                $(and $(filter 1,$(BOOT_STORE)),$(if $(filter 1,$(BOOT_AB)),,1),$(if $(filter 1,$(BOOT_PREV)),,1)),-noprev)$(if \
                $(and $(filter p256-soft,$(BOOT_SIG)),$(filter-out 0,$(UECC_OPT))),-uecc$(UECC_OPT))$(if \
                $(strip $(BOOT_OPT_EXTRA)),-x$(firstword $(shell printf '%s' '$(subst ','\'',$(strip $(BOOT_OPT_EXTRA)))' | cksum)))$(AB_SUFFIX)
BOOT_DIR_OUT := $(BUILD_DIR)/$(BOARD)/boot/$(BOOT_CFG)
BOOT_TARGET  := $(BOOT_DIR_OUT)/tfw-boot

BOOT_SRCS := $(sort \
  boot/boot_main.c boot/boot_image.c boot/boot_handoff.c boot/boot_store.c \
  boot/boot_request.c boot/boot_entry.c \
  boot/boot_platform.c boot/boot_policy.c boot/boot_trial.c \
  $(BOOT_HAL_DIR)/boot_hal_$(ARCH).c $(BOOT_SOC_SRCS) $(ARCH_BOOT_SRCS) \
  src/proto/crc16.c src/proto/crc16_update.c \
  $(HAL_DIR)/hal_early.c src/core/secure_zero.c \
  $(BOOT_MEASURE_SRCS) $(BOOT_OTA_SRCS) $(BOOT_STORE_SRCS) $(BOOT_TIER_SRCS) $(BOOT_SIG_SRCS))

# The architecture's startup, assembly, kept out of BOOT_SRCS because every
# loop over those compiles C. mk/arch/avr.mk says why the boot has its own.
BOOT_START_OBJ := $(patsubst %.S,$(BOOT_DIR_OUT)/%.o,$(ARCH_BOOT_START_SRC))
BOOT_OBJS := $(patsubst %.c,$(BOOT_DIR_OUT)/%.o,$(BOOT_SRCS)) $(BOOT_START_OBJ)

# The same reproducibility flags as the application, and for a stronger reason:
# the bootloader is what a verifier must be able to identify, and a bootloader
# nobody can rebuild byte-for-byte is a root of trust nobody can audit.
BOOT_CFLAGS := -std=c11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) \
               -Wall -Wextra -Werror -Wconversion -Wshadow \
               -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
               -ffile-prefix-map=$(CURDIR)=. \
               -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Isrc/crypto/atecc608a \
               -I$(HAL_DIR) -Isrc/third_party/ed25519-donna -Isrc/third_party/micro-ecc \
               -Iboot -Iboot/verify -I$(BOOT_HAL_DIR) -I$(BOARD_DIR) \
               -DTFW_BOARD_HEADER='"board_profile.h"' $(BOOT_OTA_DEFS) $(BOARD_DEFS) \
               $(ARCH_BOOT_OPT_CFLAGS) $(BOOT_SIG_OPT_CFLAGS) $(BOOT_OPT_EXTRA)

# The vendored tree, built WITHOUT -Werror and without our warning set.
#
# The same arrangement mk/host.mk uses for Unity, and the same reasoning: those
# warnings are not ours to fix, and -Werror over a vendored tree turns an
# upstream bump into a build break with no bug behind it. -Iboot/verify/shim
# supplies the empty <sys/param.h> avr-libc does not have, and -include supplies
# the one declaration upstream expects the integrator to provide -- both without
# editing a vendored byte. See test/third_party/MANIFEST.md.
DONNA_CFLAGS := -std=gnu11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) \
                -ffile-prefix-map=$(CURDIR)=. \
                -DED25519_REFHASH -DED25519_CUSTOMRANDOM -DED25519_NO_INLINE_ASM \
                -Iboot/verify/shim -Isrc/third_party/ed25519-donna \
                -include boot/verify/donna_shim.h $(BOOT_OPT_EXTRA)

# micro-ecc (BOOT_SIG=p256-soft), vendored and built the same way, configured
# only by its own macros: secp256r1 only, no compressed points, verify is all
# tfw-boot calls (--gc-sections drops the rest). The word size and platform are
# the architecture's (ARCH_UECC_CFLAGS): uECC_WORD_SIZE=1 is the AVR's word, and
# the host test compiles the same word path. UECC_OPT picks
# uECC_OPTIMIZATION_LEVEL: 0 is portable C, 1 adds the curve's fast reduction,
# 2 the small AVR assembly; 3 needs the unrolled file, which is not vendored.
# The default is whichever bootloader-budget measures smallest. It takes the
# boot's code-size flags (ARCH_BOOT_OPT_CFLAGS, mk/arch/avr.mk) as tfw-boot's
# own objects do: LTO records them per object, so without them its functions
# were the only ones in the boot built with call-by-call register saves and
# loop exit values recomputed (#130).
#
# BOOT_OPT_EXTRA is appended to the compile flags of every tfw-boot object,
# the vendored ones included, and to the link, to measure compiler options
# against the boot section without a commit per try: for instance
#   make bootloader BOOT_OPT_EXTRA='-mcall-prologues -fno-jump-tables'
# Empty by default, so no row's image changes. A row that needs an option
# takes it into its own flags, never through this knob.
UECC_OPT  ?= 0
BOOT_OPT_EXTRA ?=
UECC_DEFS := -DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 \
             -DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0 \
             -DuECC_SUPPORTS_secp256r1=1 -DuECC_SUPPORT_COMPRESSED_POINT=0 \
             -DuECC_OPTIMIZATION_LEVEL=$(UECC_OPT)
UECC_BOOT_CFLAGS := -std=gnu11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) \
                    -ffile-prefix-map=$(CURDIR)=. $(UECC_DEFS) $(ARCH_UECC_CFLAGS) \
                    -Isrc/third_party/micro-ecc $(ARCH_BOOT_OPT_CFLAGS) $(BOOT_SIG_OPT_CFLAGS) $(BOOT_OPT_EXTRA)

# Where .text starts and where the handoff block and the stack go are the
# architecture's to say -- ARCH_BOOT_PLACEMENT_LDFLAGS, which also explains why
# a board with no boot section still gets through every unrelated make
# invocation. The `.handoff` section is NOT the generic `.noinit`: see there.
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
#
# When the image does not fit, ld says "region `text' overflowed by N bytes".
# N IS the overage past the boot section: the text region starts at 0 and is
# the whole flash, and .text is placed at BOOT_BASE = FLASH_TOTAL - BOOT_SECTION
# (mk/arch/avr.mk), so the region's end is the boot section's end. The image is
# BOOT_SECTION + N bytes; it fits once N bytes go. The recovery below runs
# boot-core-size and bootloader-budget, which answer the two questions the
# failure actually raises: how big is the core, and how big is the backend.
$(BOOT_TARGET).elf: $(BOOT_OBJS)
	@$(ARCH_CC) $(BOOT_LDFLAGS) -o $@ $(BOOT_OBJS) 2>$(BOOT_DIR_OUT)/link.err || { \
	  echo "tfw-boot does not link for $(BOARD): it does not fit in the boot section."; \
	  echo; \
	  sed 's/^/  ld: /' $(BOOT_DIR_OUT)/link.err | grep -E 'overflow|will not fit|overlaps|undefined reference' || true; \
	  echo; \
	  $(MAKE) --no-print-directory boot-core-size || true; \
	  echo; \
	  echo "See docs/secure-boot/engineering.md. The budget is"; \
	  echo "$(BOOT_BUDGET) bytes because BOOTSZ $(BOOT_SECTION) is this part's largest"; \
	  echo "setting -- there is nothing to raise it to."; \
	  exit 1; }

# -R .eeprom only. mk/handoff.ld (in BOOT_LDFLAGS, above) makes `.handoff`
# genuinely NOLOAD, the same section type `.bss` already is, so it carries no
# bytes in the ELF and objcopy already leaves it out on its own.
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

# A row's optional seventh field, ab, is BOOT_AB=1; a row without it leaves
# BOOT_AB to the board's profile.mk (1 on the 2560, 0 on the MPS2 boards).
bootloader-check:
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

# The built boot's load end -- __data_load_end, the last flash byte .text and
# .data's initial values occupy -- is at most the part's flash. ld refuses an
# oversized boot only while the link knows the part's flash size (mk/arch/
# avr.mk's __TEXT_REGION_LENGTH__); this asks the ELF, so a link that lost it
# again fails here rather than passing an image that runs past the top.
#
# The top is the part's flash where flash starts at address zero, as on AVR. An
# architecture whose boot region sits elsewhere in the address space names the
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

# -DED25519_FORCE_32BIT so the host exercises the SAME 32-bit field arithmetic
# the AVR build compiles. Without it an x86-64 host takes donna's uint128 path
# and the tests would be testing a program the device never runs.
#
# -DF_CPU: boot/hal/avr/boot_tick.h, which test_boot_geometry pins, reads the
# clock from the compiler as it does on the target. 16 MHz is what every AVR
# board ships at, and it is supplied here so that no test source has to name it.
BOOT_TEST_CFLAGS := $(HOST_CFLAGS) -Iboot -Iboot/verify -Itest/mock \
                    -Isrc/third_party/ed25519-donna -DF_CPU=16000000UL
BOOT_DONNA_HOST  := -DED25519_REFHASH -DED25519_CUSTOMRANDOM -DED25519_NO_INLINE_ASM \
                    -DED25519_FORCE_32BIT -include boot/verify/donna_shim.h

# boot_policy.c is NOT here: it is the one unit that calls the verify seam, so
# every suite that linked it would have to provide a signature backend. It is
# added explicitly by the one suite that tests it.
BOOT_LOGIC_SRCS := boot/boot_image.c boot/measure/measure_soft.c boot/boot_handoff.c boot/boot_ota.c \
                   boot/boot_platform.c boot/boot_session.c boot/boot_trial.c \
                   boot/boot_request.c boot/boot_entry.c \
                   src/crypto/sha256.c src/proto/crc16.c src/proto/crc16_update.c \
                   test/mock/boot_hal_mock.c

$(BOOT_TEST_DIR):
	@mkdir -p $(BOOT_TEST_DIR)

# The vendored unit, compiled once without our warning set -- the same
# arrangement mk/host.mk uses for Unity and for the same reason.
$(BOOT_TEST_DIR)/donna_shim.o: boot/verify/donna_shim.c | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

$(BOOT_TEST_DIR)/donna.o: src/third_party/ed25519-donna/ed25519.c | $(BOOT_TEST_DIR)
	@$(CC) -std=gnu11 -g -O1 $(BOOT_DONNA_HOST) \
	   -Isrc/third_party/ed25519-donna $(EXTRA_CFLAGS) -c -o $@ $<

# The element-backed backends, against the software element. A separate link
# because the model IS the I2C HAL here -- the same arrangement test_atecc uses,
# and for the same reason: the whole point is that the driver talks to something
# that behaves like the part.
$(BOOT_TEST_DIR)/test_boot_element: test/unit/boot/test_boot_element.c \
        boot/measure/measure_atecc608a.c boot/verify/verify_atecc608a.c \
        boot/boot_handoff.c src/crypto/atecc608a/atecc.c src/crypto/sha256.c \
        src/core/secure_zero.c src/proto/crc16.c src/proto/crc16_update.c \
        $(ELEMENT_MODEL) test/mock/boot_hal_mock.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The element-backed measure against seeded mutants (#163): the reader's walk,
# a refusal midway and the element asleep after every path.
$(BOOT_TEST_DIR)/test_boot_element_measure_mutate: test/unit/boot/test_boot_element_measure_mutate.c \
        boot/measure/measure_atecc608a.c src/crypto/atecc608a/atecc.c src/core/secure_zero.c src/crypto/sha256.c \
        src/proto/crc16.c src/proto/crc16_update.c \
        $(ELEMENT_MODEL) test/mock/boot_hal_mock.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# test_measure_atecc608a_mutate drives the element measurement on its own (#163).
$(BOOT_TEST_DIR)/test_measure_atecc608a_mutate: test/unit/boot/test_measure_atecc608a_mutate.c \
        boot/measure/measure_atecc608a.c src/crypto/atecc608a/atecc.c src/core/secure_zero.c src/crypto/sha256.c \
        $(ELEMENT_MODEL) test/mock/boot_hal_mock.c boot/boot_handoff.c \
        src/proto/crc16.c src/proto/crc16_update.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) $(MODEL_INC) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# verify_atecc608a.c under mutation (#163): the same element, the software
# hash, so the Nonce and the Verify are the only commands the part sees.
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

# The same backend under seeded faults and RFC 8032 TEST 1024 (#163): linked as above.
$(BOOT_TEST_DIR)/test_boot_ed25519_mutate: test/unit/boot/test_boot_ed25519_mutate.c \
        boot/verify/verify_ed25519_donna.c $(BOOT_TEST_DIR)/donna.o $(BOOT_TEST_DIR)/donna_shim.o \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same backend with its key slot patched after compilation, as the jig
# patches it (#163): RFC 8032 TEST 1's public key, so the read-out path runs.
# The same arrangement as cdi_soft_uds.o below; objcopy fails if the section
# is not there.
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

# The same policy under mutation (#163), with a recording stand-in for verify.
$(BOOT_TEST_DIR)/test_boot_policy_mutate: test/unit/boot/test_boot_policy_mutate.c \
        boot/boot_policy.c $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The application's SHA-256 suite, against the boot's size-first object: the
# same vectors, so sha256.c and sha256_small.c cannot disagree (#130).
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_sha256_small
$(BOOT_TEST_DIR)/test_sha256_small: test/unit/test_sha256.c src/crypto/sha256_small.c src/core/secure_zero.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same object against seeded mutants: the padding edges hashed every way,
# the length field's high word, the HMAC key lengths either side of a block.
$(BOOT_TEST_DIR)/test_sha256_small_mutate: test/unit/boot/test_sha256_small_mutate.c \
        src/crypto/sha256_small.c src/core/secure_zero.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The faults that suite let through, planted in sha256_small.c (#163): the
# HMAC key's 64-byte bound and the length's high word. sha256_small.c alone.
$(BOOT_TEST_DIR)/test_boot_sha256_small_mutate: test/unit/boot/test_boot_sha256_small_mutate.c \
        src/crypto/sha256_small.c src/core/secure_zero.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# micro-ecc for BOOT_SIG=p256-soft (#130), with the boot's UECC_DEFS. UECC_WORD
# is the word path: 1, the AVR's, by default; `make boot-test UECC_WORD=` builds
# the host's native word, so a verdict that differs between them is a word-path
# bug (the uecc.o rule is rebuilt by hand for that: rm it first).
UECC_WORD ?= 1
UECC_HOST_DEFS := $(UECC_DEFS) $(if $(UECC_WORD),-DuECC_WORD_SIZE=$(UECC_WORD))
$(BOOT_TEST_DIR)/uecc.o: src/third_party/micro-ecc/uECC.c | $(BOOT_TEST_DIR)
	@$(CC) -std=gnu11 -g -O1 $(UECC_HOST_DEFS) $(EXTRA_CFLAGS) -c -o $@ $<

# Every Wycheproof P-256 P1363 vector against micro-ecc, and the shipping
# backend's contract, and a trailer v1 signed as the element tier signs (#130).
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

# Both against seeded mutants (#163): a bit flipped in the signature, the message
# or the key refuses under a key made fresh in the test; the shipped slot is
# erased in every byte.
$(BOOT_TEST_DIR)/test_boot_p256_soft_mutate: test/unit/boot/test_boot_p256_soft_mutate.c \
        boot/verify/verify_p256_soft.c boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c \
        test/mock/boot_hal_mock.c $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(UECC_HOST_DEFS) \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

$(BOOT_TEST_DIR)/test_boot_p256_slot_mutate: test/unit/boot/test_boot_p256_slot_mutate.c \
        boot/verify/verify_p256_soft.c boot/verify/p256_key_slot.c \
        boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c \
        test/mock/boot_hal_mock.c $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(UECC_HOST_DEFS) \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The slot's own mutants (#163): the suite compiles p256_key_slot.c into its
# unit, so the slot's size is in view; the unit is data, with no line to cover.
$(BOOT_TEST_DIR)/test_boot_p256_key_slot_mutate: test/unit/boot/test_boot_p256_key_slot_mutate.c \
        boot/verify/p256_key_slot.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $(filter-out boot/verify/p256_key_slot.c,$^) \
	   $(EXTRA_LDFLAGS)

# The ESP32 layer 0's ES256 gate (#48): trailer v1 and the SUIT manifest after
# an ESP-IDF image, against the mock HAL, with p256-soft and the shared policy
# primitives the bootloader links (boot/hal/esp32/bootloader_components/main/
# CMakeLists.txt). The manifest gate is src/core/suit.c over l0_crypto.c, as
# the bootloader links it; the test builds its envelopes with suit-emit's
# encoder. Against the esp32-qemu profile, whose class id the manifest names,
# and without the host's attestation switches, which that profile sets itself.
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

# The P4's backend, the ROM's ECDSA (boot/hal/esp32/verify_p256_rom.c), against
# a host ROM over micro-ecc that takes the key and signature little-endian as
# ESP-IDF documents (test/mock/esp_rom_ecdsa_mock.c): every Wycheproof vector,
# then the same gate suite as above, so the P4's link is the one tested.
ESP_ROM_HOST := boot/hal/esp32/verify_p256_rom.c test/mock/esp_rom_ecdsa_mock.c
$(BOOT_TEST_DIR)/test_boot_p256_rom: test/unit/boot/test_boot_p256_rom.c $(ESP_ROM_HOST) \
        boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c test/mock/boot_hal_mock.c \
        $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(UECC_HOST_DEFS) \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same backend, seeded: linked exactly as above.
$(BOOT_TEST_DIR)/test_boot_p256_rom_mutate: test/unit/boot/test_boot_p256_rom_mutate.c $(ESP_ROM_HOST) \
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

# The ESP32 boot HAL's aligned read windows (#48): the arithmetic
# boot_hal_esp32.c reads every piece through, because bootloader_flash_read
# refuses an unaligned source, length or destination.
$(BOOT_TEST_DIR)/test_boot_flash_span: test/unit/boot/test_boot_flash_span.c \
        boot/hal/esp32/flash_span.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/esp32 $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# tfw-boot's STM32H5 flash driver (#14), boot/hal/cortex-m/stm32h5/h5_flash.c,
# on a flash interface and a flash of the test's own: the test is the hardware,
# acting at the wait the part's boot_flash.c polls, so boot_flash.c is not here.
$(BOOT_TEST_DIR)/test_boot_stm32h5_flash: test/unit/boot/test_boot_stm32h5_flash.c \
        boot/hal/cortex-m/stm32h5/h5_flash.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Isrc/hal/cortex-m $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
# The RP2350's sector read-modify-write (row rp2350): the arithmetic every
# tfw-boot flash write goes through, over a RAM flash the test supplies in
# place of rp_flash_rom.c's boot ROM calls.
$(BOOT_TEST_DIR)/test_boot_rp2350_flash: test/unit/boot/test_boot_rp2350_flash.c \
        boot/hal/cortex-m/rp2350/rp_flash.c src/core/secure_zero.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/cortex-m/rp2350 $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
# The MCXN947's flash writes (row mcxn947), boot/hal/cortex-m/mcxn94x/
# mcxn_flash.c, over a model of the boot ROM's flash driver in place of the
# ROM's: the test is the driver, so boot_flash.c, which finds the ROM's, is
# not here. The suites sit beside the SoC's sources, not under
# test/unit/boot/, because they name the SoC's registers.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_mcxn_flash $(BOOT_TEST_DIR)/test_mcxn_uart
$(BOOT_TEST_DIR)/test_mcxn_flash: boot/hal/cortex-m/mcxn94x/test/test_mcxn_flash.c \
        boot/hal/cortex-m/mcxn94x/mcxn_flash.c src/core/secure_zero.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/cortex-m/mcxn94x $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
# Its polled LPUART, boot/hal/cortex-m/mcxn94x/mcxn_uart.c, over a register
# model of the LP_FLEXCOMM unit; boot_uart.c, which hands it LP_FLEXCOMM4 and
# sets the clock and the pins, is not here.
$(BOOT_TEST_DIR)/test_mcxn_uart: boot/hal/cortex-m/mcxn94x/test/test_mcxn_uart.c \
        boot/hal/cortex-m/mcxn94x/mcxn_uart.c src/hal/cortex-m/mcxn94x/lpuart_baud.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/cortex-m/mcxn94x -Isrc/hal/cortex-m $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The Pico 2 W's flash map by literal, under its own profile: the regions
# tfw-boot writes, and the 4 MiB flash they must fit (pico-sdk's pico2_w.h).
BOOT_TEST_RP2350_CFLAGS = $(subst -Iboards/$(HOST_BOARD),-Iboards/rp2350-pico2w,\
                            $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))
$(BOOT_TEST_DIR)/test_boot_rp2350_map: test/unit/boot/test_boot_rp2350_map.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_RP2350_CFLAGS) -Iboot/hal/cortex-m/rp2350 $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# tfw-boot's RP2350 UART over a register model. The test and its cm_regs.h live
# in boot/hal/cortex-m/rp2350/test/: they name the Cortex-M register layer, which
# lint-arch-leak allows only under the Cortex-M HAL directories, and the SoC's
# *.c wildcard does not descend into a subdirectory, so no image links them.
# That cm_regs.h comes ahead of the real one, so every TFW_CM_REG lands in the
# test's table.
RP2350_BOOT_TEST_DIR := boot/hal/cortex-m/rp2350/test
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_rp2350_uart
$(BOOT_TEST_DIR)/test_boot_rp2350_uart: $(RP2350_BOOT_TEST_DIR)/test_boot_rp2350_uart.c \
        boot/hal/cortex-m/rp2350/boot_uart.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_RP2350_CFLAGS) -I$(RP2350_BOOT_TEST_DIR) -Isrc/hal/cortex-m $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
# Its sector erase and program through the boot ROM, the same way: rp_flash_rom.c
# whole, over the test's ROM table model in place of rp_rom_lookup.c and its
# interrupt mask in place of cm_regs.h's.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_rp2350_rom
$(BOOT_TEST_DIR)/test_boot_rp2350_rom: $(RP2350_BOOT_TEST_DIR)/test_boot_rp2350_rom.c \
        boot/hal/cortex-m/rp2350/rp_flash_rom.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_RP2350_CFLAGS) -I$(RP2350_BOOT_TEST_DIR) -Iboot/hal/cortex-m/rp2350 $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The ESP32 boot HAL itself (#48, #163), boot/hal/esp32/boot_hal_esp32.c, over
# stand-ins for the ESP-IDF bootloader calls it makes, which live beside its
# test (test/unit/boot/boot_hal_esp32/). The real flash_span.c and the layer 0
# gate are linked beside it, as the bootloader links them; the gate reads
# through this HAL, so there is no mock HAL in the link. Once with the C3's
# reset names and once with the P4's.
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

# The same windows at the clamp's edges and over seeded random inputs.
$(BOOT_TEST_DIR)/test_boot_flash_span_mutate: test/unit/boot/test_boot_flash_span_mutate.c \
        boot/hal/esp32/flash_span.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/esp32 $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The ESP32 layer 0's hash (#48), boot/hal/esp32/measure_esp32.c, over the
# bootloader SHA as the C3 and the P4 build it and, with CONFIG_IDF_TARGET_ESP32,
# over the ROM SHA as the classic ESP32 does; test/mock/esp_sha_mock.c holds
# each call to its silicon's contract. Then the gate suite above once more
# with that hash in place of measure_soft.c's: the link QEMU's hmac-p256 row
# boots, which refused a SUIT span that was not whole words with a zero digest.
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

# The same hash, seeded, on both paths: linked exactly as the two above.
$(BOOT_TEST_DIR)/test_boot_measure_esp32_mutate: test/unit/boot/test_boot_measure_esp32_mutate.c $(ESP_SHA_HOST) \
        test/mock/boot_hal_mock.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Itest/mock/esp32 -DCONFIG_IDF_TARGET_ESP32=0 \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_measure_esp32_mutate_classic
$(BOOT_TEST_DIR)/test_boot_measure_esp32_mutate_classic: test/unit/boot/test_boot_measure_esp32_mutate.c $(ESP_SHA_HOST) \
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

# l0_crypto.c alone (#163): over measure_soft.c's hash, then over
# measure_esp32.c's, the hash the C3 and the P4 link.
$(BOOT_TEST_DIR)/test_boot_l0_crypto_mutate: test/unit/boot/test_boot_l0_crypto_mutate.c \
        boot/hal/esp32/l0_crypto.c boot/measure/measure_soft.c src/crypto/sha256_small.c src/core/secure_zero.c \
        test/mock/boot_hal_mock.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_l0_crypto_mutate_sha
$(BOOT_TEST_DIR)/test_boot_l0_crypto_mutate_sha: test/unit/boot/test_boot_l0_crypto_mutate.c \
        boot/hal/esp32/l0_crypto.c $(ESP_SHA_HOST) test/mock/boot_hal_mock.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Itest/mock/esp32 -DCONFIG_IDF_TARGET_ESP32=0 \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# boot_ota_store_avr.c alone (#163): register-level over test/mock/avr_regs's
# <avr/io.h>, under the host board's pins, then under the 2560's.
$(BOOT_TEST_DIR)/test_boot_ota_store_avr_mutate: test/unit/boot/test_boot_ota_store_avr_mutate.c \
        boot/hal/avr/boot_ota_store_avr.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Itest/mock/avr_regs $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_ota_store_avr_mutate_2560
$(BOOT_TEST_DIR)/test_boot_ota_store_avr_mutate_2560: test/unit/boot/test_boot_ota_store_avr_mutate.c \
        boot/hal/avr/boot_ota_store_avr.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) -Itest/mock/avr_regs $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# recovery_halt.c alone (#163): cli and power-down over test/mock/avr_regs.
$(BOOT_TEST_DIR)/test_boot_recovery_halt_avr_mutate: test/unit/boot/test_boot_recovery_halt_avr_mutate.c \
        boot/hal/avr/recovery_halt.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Itest/mock/avr_regs $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# boot_hal_avr.c alone (#163): the part's FLASHEND, SPM_PAGESIZE and, on the
# 2560, EIND come from -D, as avr-libc brings them from the part's header. The
# EEPROM address is a 16-bit pointer on the part, narrower than the host's.
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

# The two DICE layer 0 tiers (BOOT_TIER_SRCS above). test_boot_cdi_soft links
# cdi_soft.c as built, its UDS slot the erased placeholder. test_boot_cdi_soft_uds
# links the same source with the slot patched after compilation, as the
# provisioning jig patches the image: -fdata-sections puts the slot in a section
# of its own and objcopy replaces its 32 bytes, so the source under test is the
# one that ships. objcopy fails if the section is not there. test_boot_cdi_atecc
# links cdi_atecc.c and the real driver against the software element.
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

# test_boot_cdi_soft_uds's link with uds_slot made global, so the mutation
# test can flip the patched slot's bits in place (#163).
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

# The element tier's mutation tests (#163): the test file is the driver's wake,
# exec and sleep and the I2C init, so the driver and the model stay out.
$(BOOT_TEST_DIR)/test_boot_cdi_atecc_mutate: test/unit/boot/test_boot_cdi_atecc_mutate.c \
        boot/tier/cdi_atecc.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -Isrc/crypto/atecc608a \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# The ESP32 layer 0's own unit, boot_l0.c, driven through call_start_cpu0
# (#163). ESP-IDF is the stand-ins in the test's directory, searched first; the
# test file is the gate and the handoff, so l0_gate.c and boot_hal_mock.c stay
# out of the link.
$(BOOT_TEST_DIR)/test_boot_l0_esp32_mutate: test/unit/boot/test_boot_l0_esp32_mutate.c \
        boot/hal/esp32/boot_l0.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) -Itest/unit/boot/boot_l0_esp32 $(L0_TEST_CFLAGS) -Iboot/hal/esp32 $(EXTRA_CFLAGS) \
	   -o $@ $^ $(EXTRA_LDFLAGS)

# HW Req 6.4: no copy of the CDI left once a tier has handed it over. These
# link test/mock/cdi_residue.c IN PLACE of src/core/secure_zero.c, so every
# clear is recorded, and the boot's own hash, so the stack check sees the
# frames the boot leaves.
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

# The hand-over itself: boot_main.c as built, its main() renamed so Unity's is
# the program's, over the soft tier with the patched slot and the boot's hash
# in place of the application's. tfw_boot_evaluate is the test's. The tick is
# the AVR boot HAL's, as on the target. -z now: the host's lazy binding saves
# the vector registers, CDI and all, under the first call of each PLT entry,
# deeper than the next test's hand-over reaches. The target has no dynamic
# linker, so that copy is not one tfw_boot_hand_over could clear.
$(BOOT_TEST_DIR)/boot_main_ho.o: boot/boot_main.c | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Iboot/hal/avr $(EXTRA_CFLAGS) -c -o $@ $<
	@objcopy --redefine-sym main=tfw_boot_main_not_linked $@

# Both hand-over tests wrap the derive so they read the stack the moment it
# returns: the calls the hand-over makes after it, at the same depth, overwrite
# its frame, and a scan after the jump finds the UDS's copy gone whether or not
# the derive cleared it.
DERIVE_WRAP := -Wl,--wrap=tfw_boot_cdi_derive

$(BOOT_TEST_DIR)/test_boot_hand_over_clear: test/unit/boot/test_boot_hand_over_clear.c \
        $(BOOT_TEST_DIR)/boot_main_ho.o $(BOOT_TEST_DIR)/cdi_soft_uds.o \
        src/crypto/sha256_small.c test/mock/cdi_residue.c \
        boot/recovery/recovery_ota.c boot/boot_store.c boot/boot_swap_none.c \
        boot/boot_stage_none.c src/proto/frame.c \
        $(filter-out src/crypto/sha256.c,$(BOOT_LOGIC_SRCS)) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ -Wl,-z,now $(DERIVE_WRAP) $(EXTRA_LDFLAGS)

# The same hand-over on an unprovisioned device: cdi_soft.c as it ships, its
# UDS slot erased.
$(BOOT_TEST_DIR)/test_boot_hand_over_erased: test/unit/boot/test_boot_hand_over_erased.c \
        $(BOOT_TEST_DIR)/boot_main_ho.o boot/tier/cdi_soft.c \
        src/crypto/sha256_small.c test/mock/cdi_residue.c \
        boot/recovery/recovery_ota.c boot/boot_store.c boot/boot_swap_none.c \
        boot/boot_stage_none.c src/proto/frame.c \
        $(filter-out src/crypto/sha256.c,$(BOOT_LOGIC_SRCS)) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ -Wl,-z,now $(DERIVE_WRAP) $(EXTRA_LDFLAGS)

# The session loop with tfw_boot_evaluate and tfw_boot_hand_over supplied by the
# test, so boot_policy.c and the signature backend stay out of this link.
$(BOOT_TEST_DIR)/test_boot_recovery_session: test/unit/boot/test_boot_recovery_session.c \
        boot/recovery/recovery_ota.c boot/boot_store.c boot/boot_swap_none.c src/proto/frame.c \
        $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The faults the session suite let through, planted in recovery_ota.c (#163):
# linked exactly as above.
$(BOOT_TEST_DIR)/test_boot_recovery_ota_mutate: test/unit/boot/test_boot_recovery_ota_mutate.c \
        boot/recovery/recovery_ota.c boot/boot_store.c boot/boot_swap_none.c src/proto/frame.c \
        $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The settle against the floor's authority: the real boot_store.c over the mock
# EEPROM, with the element's counter supplied by the test.
$(BOOT_TEST_DIR)/test_boot_settle: test/unit/boot/test_boot_settle.c \
        boot/boot_store.c $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same link under a block and a request rewritten at random (#77).
$(BOOT_TEST_DIR)/test_boot_state_mutate: test/unit/boot/test_boot_state_mutate.c \
        boot/boot_store.c $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# boot_store.c's settle under a refused block write, a part-way counter and
# seeded floors (#163): the same link again.
$(BOOT_TEST_DIR)/test_boot_store_mutate: test/unit/boot/test_boot_store_mutate.c \
        boot/boot_store.c $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The external store's read, check and copy (issue #75), against the software
# NOR part on the mock's store bus. The platform block's write is the test's,
# so it can see the order of states the copy stores; boot_store.c stays out.
BOOT_STAGE_TEST_SRCS := test/unit/boot/test_boot_stage.c boot/boot_stage.c boot/boot_cose.c \
                        test/model/spi_nor_model.c test/mock/boot_verify_mock.c $(BOOT_LOGIC_SRCS)

$(BOOT_TEST_DIR)/test_boot_stage: $(BOOT_STAGE_TEST_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# Again under the 2560's profile, where STAGE (64 KB) is smaller than the
# application region and the trailer that ends the staged stream is copied to
# the region's end, far past it. The host board's extra feature defines are
# the application's, and are left out rather than redefined over the 2560's.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_stage_2560
BOOT_TEST_2560_CFLAGS = $(subst -Iboards/$(HOST_BOARD),-Iboards/atmega2560,\
                          $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))

$(BOOT_TEST_DIR)/test_boot_stage_2560: $(BOOT_STAGE_TEST_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The geometry suite under the 2560's profile, the pattern rule's link: its
# boot section, and the write end the A/B secondary sets, are that profile's.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_geometry_2560
$(BOOT_TEST_DIR)/test_boot_geometry_2560: test/unit/boot/test_boot_geometry.c $(BOOT_LOGIC_SRCS) \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The unprovisioned hand-over under the 2560's profile, the soft row's: the
# link above, with boot_main.c compiled against that profile too.
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
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ -Wl,-z,now $(DERIVE_WRAP) $(EXTRA_LDFLAGS)

# And under the 32U4's, the third tfw-boot row in BOOT_SUPPORTED.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_geometry_32u4
BOOT_TEST_32U4_CFLAGS = $(subst -Iboards/$(HOST_BOARD),-Iboards/atmega32u4,\
                          $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS)))

$(BOOT_TEST_DIR)/test_boot_geometry_32u4: test/unit/boot/test_boot_geometry.c $(BOOT_LOGIC_SRCS) \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_32U4_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The MPS2 A/B slot geometry, boot/hal/cortex-m/boot_slots.h, under each MPS2
# profile with TFW_CM_AB as its A/B rows build. The Cortex-M HAL directory comes
# first, so its boot_slots.h is the one found and not test/mock's. It has no
# host build: the wildcard's test_boot_slots_mps2 would take the 2560's profile.
BOOT_TEST_MPS2_BINS := $(BOOT_TEST_DIR)/test_boot_slots_mps2-an505 $(BOOT_TEST_DIR)/test_boot_slots_mps2-an500
BOOT_TEST_BINS := $(filter-out $(BOOT_TEST_DIR)/test_boot_slots_mps2,$(BOOT_TEST_BINS)) $(BOOT_TEST_MPS2_BINS)
BOOT_TEST_MPS2_CFLAGS = -Iboot/hal/cortex-m $(subst -Iboards/$(HOST_BOARD),-Iboards/$(1),\
                          $(filter-out $(HOST_EXTRA_DEFS),$(BOOT_TEST_CFLAGS))) -DTFW_CM_AB=1

# A static pattern, so the binaries' .run targets are not taken for a board.
$(BOOT_TEST_MPS2_BINS): $(BOOT_TEST_DIR)/test_boot_slots_mps2-%: test/unit/boot/test_boot_slots_mps2.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(call BOOT_TEST_MPS2_CFLAGS,mps2-$*) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same take over a store rewritten at random (#75): test_boot_stage's link.
$(BOOT_TEST_DIR)/test_boot_stage_mutate: test/unit/boot/test_boot_stage_mutate.c boot/boot_stage.c boot/boot_cose.c \
        test/model/spi_nor_model.c test/mock/boot_verify_mock.c $(BOOT_LOGIC_SRCS) \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The MPS2 boards' memory image store, the application's driver and tfw-boot's
# together over one host array: the model header is forced into every unit,
# so both address the array as they address the board's RAM.
$(BOOT_TEST_DIR)/test_store_mem: test/unit/boot/test_store_mem.c src/store/mem.c \
        boot/hal/cortex-m/boot_ota_store_cortex-m.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -Itest/model -Isrc/core \
	   -include test/model/store_mem_model.h -o $@ $^ $(EXTRA_LDFLAGS)

# The OTA_ACTIVATE Sig_structure against literal bytes (#163): boot_cose.c
# alone, the algorithm byte the test's.
$(BOOT_TEST_DIR)/test_boot_cose_mutate: test/unit/boot/test_boot_cose_mutate.c boot/boot_cose.c \
        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The swap-move under the 2560's geometry (boot/hal/avr/boot_slots.h; the MPS2
# boards' is pinned by test_boot_slots_mps2 and runs under QEMU in CI).
# With the serial session, so a session's image is seen landing in the
# secondary and swapped in; tfw_boot_evaluate and the floor are the test's.
# And with the store's copy over the software NOR part, so the store row's
# image is seen reaching the same swap and the same trial (#77).
$(BOOT_TEST_DIR)/test_boot_swap: test/unit/boot/test_boot_swap.c boot/boot_swap.c \
                                 boot/recovery/recovery_ota.c src/proto/frame.c \
                                 boot/boot_stage.c boot/boot_cose.c test/model/spi_nor_model.c test/mock/boot_verify_mock.c \
                                 $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same link under a journal and a confirm request rewritten at random (#77).
$(BOOT_TEST_DIR)/test_boot_swap_mutate: test/unit/boot/test_boot_swap_mutate.c boot/boot_swap.c \
                                        boot/recovery/recovery_ota.c src/proto/frame.c \
                                        boot/boot_stage.c boot/boot_cose.c test/model/spi_nor_model.c test/mock/boot_verify_mock.c \
                                        $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

$(BOOT_TEST_DIR)/test_boot_recovery_ota_swap_mutate: test/unit/boot/test_boot_recovery_ota_swap_mutate.c boot/boot_swap.c \
                                        boot/recovery/recovery_ota.c src/proto/frame.c \
                                        boot/boot_stage.c boot/boot_cose.c test/model/spi_nor_model.c test/mock/boot_verify_mock.c \
                                        $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The 328P's fallback (#52): PREV copied back by the store's copy loop, under
# the host board's profile, which is the 328P's.
$(BOOT_TEST_DIR)/test_boot_prev: test/unit/boot/test_boot_prev.c boot/boot_prev.c \
                                 boot/boot_stage.c boot/boot_cose.c test/model/spi_nor_model.c test/mock/boot_verify_mock.c \
                                 $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same fallback under a hostile store, seeded: linked exactly as above.
$(BOOT_TEST_DIR)/test_boot_prev_mutate: test/unit/boot/test_boot_prev_mutate.c boot/boot_prev.c \
                                        boot/boot_stage.c boot/boot_cose.c test/model/spi_nor_model.c test/mock/boot_verify_mock.c \
                                        $(BOOT_LOGIC_SRCS) $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The simavr harness's store select and UART, from each store board's profile:
# the host board's (the 328P's), then the 2560's and the 32U4's. Each build is
# told the answer from its part's data sheet: D8's port and bit, the USART the
# application speaks on and its UCSRnA. The harness itself needs simavr and
# runs only on CI's image.
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

# The rows with no staging and no swap: the two stand-ins alone.
$(BOOT_TEST_DIR)/test_boot_none_rows: test/unit/boot/test_boot_none_rows.c \
                                      boot/boot_stage_none.c boot/boot_swap_none.c \
                                      $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The same two stand-ins under a seeded hostile block (#163): linked as above.
$(BOOT_TEST_DIR)/test_boot_none_mutate: test/unit/boot/test_boot_none_mutate.c \
                                        boot/boot_stage_none.c boot/boot_swap_none.c \
                                        $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# boot_main.c's main() as tfw_boot_main_ut, against the test's own stand-ins
# for every unit it calls (#163): the sequence, not the callees. Named
# test_boot_main_seq: test_boot_main is the in-place row's suite (#77) below.
BOOT_TEST_BINS += $(BOOT_TEST_DIR)/test_boot_main_seq
$(BOOT_TEST_DIR)/test_boot_main_seq.o: boot/boot_main.c | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -Iboot/hal/avr -Dmain=tfw_boot_main_ut -c -o $@ $<

$(BOOT_TEST_DIR)/test_boot_main_seq: test/unit/boot/boot_main/test_boot_main.c \
                                     $(BOOT_TEST_DIR)/test_boot_main_seq.o \
                                     $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) $(EXTRA_CFLAGS) -Iboot/hal/avr -o $@ $^ $(EXTRA_LDFLAGS)

# tfw-boot's own main() on the host board's in-place row (#77), renamed here so
# the suite's main() can call it: the order of the trial's writes, the
# watchdog and the jump is tested in the file the device links, and nothing is
# added to tfw-boot for it. Recovery and the CDI tier are the test's.
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
# Each binary its own target, as host-test's are (mk/host.mk), so -j runs them
# side by side.
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
# A GATE, exactly as size-check is for the application, and a prerequisite of
# `bootloader` rather than something to run when someone remembers.
#
# The budget is BOOT_BUDGET from the board profile, which is the boot section's
# size. It is NOT raised to meet an image: on the 328P there is nothing to raise
# it to, because 2048 words is the largest BOOTSZ the part has.
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
# NOT A BOOTLOADER AND NOT FLASHABLE. tfw_boot_verify, tfw_boot_pubkey and
# tfw_boot_cose_alg (which the store row's boot_cose.c reads) are resolved to
# address zero by --defsym, so this image would jump into the reset vector
# instead of verifying anything. It exists to answer one question
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
# Each copy is `git archive HEAD` (scripts/repro_build.py): no build/, no
# untracked file, and SOURCE_DATE_EPOCH is the commit time. Every BOOT_* that
# names the configuration is passed, so the copies build this row's image.
bootloader-reproducible:
	@python3 scripts/repro_build.py --make "$(MAKE)" --image $(BOOT_TARGET).hex \
	   --dir /tmp/agboot-a --dir /tmp/agboot-b -- \
	   BOARD=$(BOARD) BOOT_TIER=$(BOOT_TIER) BOOT_MEASURE=$(BOOT_MEASURE) \
	   BOOT_SIG=$(BOOT_SIG) BOOT_OTA=$(BOOT_OTA) BOOT_STORE=$(BOOT_STORE) \
	   $(BOOT_TARGET).hex

## bootloader-reproducible-check: bootloader-reproducible on the 2560 soft base row
# The row scripts/test_reproducible.py builds (docs/secure-boot/engineering.md).
bootloader-reproducible-check:
	@$(MAKE) --no-print-directory bootloader-reproducible BOARD=atmega2560 \
	   BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=0

## boot-flash: write tfw-boot to a connected board (AVR: ISP; Cortex-M: the debug probe, after board-record)
# On AVR, NOT the application's programmer. That one talks TO a bootloader, and the boot
# section is the one place a bootloader cannot write. This needs an ISP -- see
# ARCH_BOOT_FLASH in mk/arch/$(ARCH).mk.
#
# Only a BOOT_SUPPORTED row is built by `bootloader` or flashed by `boot-flash`,
# and the refusal comes at parse time, before anything is built. The defaults
# name the board's supported row (above); a row named by hand that is not one
# is refused here rather than stopping at the link.
# The targets that measure a configuration rather than ship it --
# bootloader-budget, boot-core-size, bootloader-candidate -- are not refused:
# they exist for the rows that do not fit.
BOOT_ROW  := $(BOARD):$(BOOT_TIER):$(BOOT_MEASURE):$(BOOT_SIG):$(BOOT_OTA):$(BOOT_STORE)$(if $(AB_SUFFIX),:ab)
BOOT_FITS := $(filter $(BOARD):%,$(BOOT_SUPPORTED))
# A literal comma, which $(if) would otherwise read as its argument separator.
BOOT_COMMA := ,
BOOT_GATED_GOAL := $(firstword $(filter bootloader boot-flash boot-bench boot-flash-uds,$(MAKECMDGOALS)))
ifneq ($(BOOT_GATED_GOAL),)
# One update path per image (#52, question 6, decided 2026-10-26): tfw-boot
# opens a session over the UART or installs what the application staged in the
# external store, never both. Named before the row check, so the refusal says
# which two choices there are rather than only that the row is not one.
ifeq ($(BOOT_OTA)$(BOOT_STORE),11)
$(error $(BOOT_GATED_GOAL): BOOT_OTA=1 BOOT_STORE=1 is two update paths, and an image takes one. Choose BOOT_OTA=1 BOOT_STORE=0, OTA in tfw-boot, for an application built OTA_STAGE=0; or BOOT_OTA=0 BOOT_STORE=1, the external store, for an application built OTA_STAGE=1. See docs/ota/architecture.md$(BOOT_COMMA) "The 2560 with OTA and the store")
endif
ifeq ($(filter $(BOOT_ROW),$(BOOT_SUPPORTED)),)
$(error $(BOOT_GATED_GOAL): $(BOOT_ROW) (BOARD:BOOT_TIER:BOOT_MEASURE:BOOT_SIG:BOOT_OTA:BOOT_STORE) is not a supported configuration$(if $(BOOT_AB_OPTIONAL), (a trailing :ab is BOOT_AB=1)). $(if $(BOOT_FITS),For $(BOARD) the supported rows$(BOOT_COMMA) which fit$(BOOT_COMMA) are: $(BOOT_FITS),No tfw-boot configuration fits $(BOARD)). See BOOT_SUPPORTED in mk/boot.mk and docs/secure-boot/cli.md)
endif
endif

## bootloader-candidate: measure a row that is not in BOOT_SUPPORTED yet
# BOOT_CANDIDATE=<board>:<tier>:<measure>:<sig>:<ota>:<store>, the six fields of
# a BOOT_SUPPORTED row. Builds tfw-boot for it, as boot-size-check does, and
# prints its size against the board's boot section; an image that does not fit
# stops at the link with ld's own "overflowed by N bytes" and boot-core-size.
# It adds no row: a row joins BOOT_SUPPORTED by hand, once this has measured it
# on CI's image and bootloader-check passes it. Exit 0 is "fits", 1 "does not".
#
# The row it exists for (#52, docs/ota/architecture.md): the 32U4's restore from
# PREV in the store,
#   make bootloader-candidate BOOT_CANDIDATE=atmega32u4:atecc608a:atecc608a:atecc608a:0:1
# The 328P's store row, without the restore, overflowed its boot section by
# 1498 bytes (avr-gcc 14.2.0), so it is not in BOOT_SUPPORTED (#52).
#
# The candidate is checked before anything is built: six fields, a board with
# a profile, and not a row already (bootloader-check builds those).
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

# On AVR the recipe reads EESAVE before the chip erase and refuses an erase that would
# take the EEPROM; see ARCH_BOOT_FLASH in mk/arch/$(ARCH).mk. ISP_RERUN is what
# that refusal tells the operator to run again, BOOT_EEPROM_RESTORE=1 added.
boot-flash: ISP_RERUN = BOOT_TIER=$(BOOT_TIER) BOOT_MEASURE=$(BOOT_MEASURE) BOOT_SIG=$(BOOT_SIG) \
                        BOOT_OTA=$(BOOT_OTA) BOOT_STORE=$(BOOT_STORE)
# BOOT_FLASH_HEX is the image it writes: the build's, unless mk/boot-bench.mk's
# boot-flash-uds names its temporary copy with the UDS in it.
BOOT_FLASH_HEX ?= $(BOOT_TARGET).hex
boot-flash: $(BOOT_FLASH_HEX)
	@$(ARCH_BOOT_FLASH)

## lock-check: read the lock bits back over ISP; refuse unless they lock the UDS
# The provisioning flow's last step (SPEC device-provisioning R6): after the
# operator's avrdude lock write, the lock byte is read and every bit the board's
# BOOT_LOCK_PROGRAMMED names must read programmed. Writes nothing. A board whose
# profile names no bit is refused here, never passed. See ARCH_LOCK_CHECK in
# mk/arch/$(ARCH).mk.
ifneq ($(filter lock-check,$(MAKECMDGOALS)),)
ifeq ($(BOOT_LOCK_PROGRAMMED),)
$(error lock-check: REFUSED. boards/$(BOARD)/profile.mk names no BOOT_LOCK_PROGRAMMED, so nothing says which lock bits protect the UDS on this part; the profile names the data sheet they come from)
endif
endif

lock-check:
	@$(ARCH_LOCK_CHECK)

.PHONY: boot-test bootloader boot-size-check bootloader-budget boot-core-size bootloader-reproducible bootloader-reproducible-check boot-flash lock-check

# ------------------------------------------------------- simavr integration
#
# The real tfw-boot image, the real application image and a modelled ATECC608A on
# the emulated TWI bus. Part of `make ci`.
#
# The image_flags the harness's signer stamps into the trailer
# (include/tfw/boot.h): 0x00 a release image, 0x01 a development one. A build
# setting, like the board id, which the signer takes from the board profile.
IMAGE_FLAGS ?= 0x00

# Its own variable rather than mk/integration.mk's INT_DIR: that file is
# included AFTER this one, so INT_DIR is still empty here and the rule target
# would expand to "/boot_harness" -- a path at the filesystem root, and a "no
# rule to make target" that names something nobody wrote.
#
# The BOARD is in the path. The harness compiles the board profile in, so a
# 2560 harness and a 328P harness are different programs, and a shared path
# means the second build reuses the first one's binary and reports its result --
# the same defect mk/firmware.mk's output path exists to prevent.
# IMAGE_FLAGS (above) is compiled into the harness and UECC_OPT into its uECC.o,
# so each names the path where it is not its default (#161), as on tfw-boot's.
BOOT_INT_DIR := $(BUILD_DIR)/integration/$(BOARD)$(if $(filter-out 0,$(UECC_OPT)),-uecc$(UECC_OPT))$(if \
                $(filter-out 0x00,$(IMAGE_FLAGS)),-flags$(IMAGE_FLAGS))

# WHICH ROW OF BOOT_SUPPORTED THIS RUNS, and it is not the default board.
#
# The 328P's boot section has no room for the framed OTA path, so its recovery
# is recovery_halt.c: a refusing device stops, mute, and "tampered" and
# "downgrade" become the same observation. Two of the harness's three assertions
# would then be one. The Mega's 8 KB section carries OTA, so a refusal arrives
# as an EVENT frame with the reason in it -- and the Mega is also the part with
# the three-byte PC and the far-flash read, which is where the jump and the
# measurement have the most room to be wrong. See the header of boot_harness.c.
BOOT_INT_BOARD := atmega2560
BOOT_INT_ROW   := BOOT_TIER=atecc608a BOOT_MEASURE=atecc608a BOOT_SIG=atecc608a BOOT_OTA=1 \
                  BOOT_STORE=0

# The harness's half that names the store's PREV (#52): only a board whose
# profile has one links it, the board that tfw-boot's store row gives
# boot_prev.c above: an OTA_STORE, one slot (no BOOT_AB) and BOOT_PREV=1. A
# two-slot board, which falls back to its secondary, and the 328P, which has no
# boot-time rollback, link the stub, and the harness's prev row fails there.
BOOT_INT_HAS_PREV := $(if $(OTA_STORE),$(if $(filter 1,$(BOOT_AB)),,$(if $(filter 1,$(BOOT_PREV)),1)))
BOOT_INT_PREV_SRC := test/integration/simavr/boot_harness_prev_none.c
ifeq ($(BOOT_INT_HAS_PREV),1)
BOOT_INT_PREV_SRC := test/integration/simavr/boot_harness_prev.c
endif

# The same warning set as the firmware. The harness is a model of a part other
# people will read as documentation for it, so it is held to the standard the
# thing it models is held to.
# The two-slot cases (the confirm request, the swap record) by object list.
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

# The soft row's signer (#130): the one vendored micro-ecc, built for the host
# without our warning set, as every vendored tree is.
$(BOOT_INT_DIR)/uECC.o: src/third_party/micro-ecc/uECC.c
	@mkdir -p $(BOOT_INT_DIR)
	@$(HOST_CC) -std=gnu11 -O1 $(UECC_DEFS) -Isrc/third_party/micro-ecc -c -o $@ $<

## integration-test-boot: run tfw-boot under simavr
# Recursive, because this suite runs one specific row of BOOT_SUPPORTED on one
# specific board and a variable set on the command line would otherwise follow
# whatever BOARD the caller happened to have.
#
# Then the BOOT_STORE=1 row (#75, #77): the image staged in the external store,
# a modelled 25-series part on the emulated SPI bus, read, checked, copied into
# the secondary, swapped in, tried, confirmed or swapped back. It is the only
# row with the store (OTA plus the store does not fit, docs/ota/architecture.md).
#
# Last, its network leg (#75, #55): the application built with ETH=1, whose
# stager is ETH's default on a board with an OTA_STORE (mk/firmware.mk), the
# W5100 model (test/model/w5x00_model.c) on the emulated SPI bus behind D10
# beside the NOR part behind D8, and the harness the TCP peer that streams
# OTA_BEGIN/OTA_DATA/OTA_END to it before the reset into tfw-boot's trial.
# ETH=1 is the 2560's default (#55 item 48), so the serial legs say ETH=0.
#
# And the soft base row (#130), the one soft row that fits (CA m467):
# BOOT_SIG=p256-soft at UECC_OPT's default, no element on the bus. The harness
# provisions the key and UDS slots as the jig does, signs with P-256, and checks
# that a genuine image runs and that a tampered one, or one signed by another
# key, never reaches the application. Verification is on this row as on every
# other (CA m453).
BOOT_INT_SOFT_ROW := BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 \
                     BOOT_STORE=0
BOOT_INT_STORE_ROW := BOOT_TIER=atecc608a BOOT_MEASURE=atecc608a BOOT_SIG=atecc608a BOOT_OTA=0 \
                      BOOT_STORE=1

# The first row's application is CRYPTO=soft by name: test_signed_image_boots
# (test/integration/simavr/boot_harness.c) proves a soft application refuses
# the element-derived CDI tfw-boot hands off. The 2560's default is CRYPTO=auto
# (issue #130), which finds the element on this bus and rightly takes the
# element's path, so that case has to ask for the soft image. The bridge row's
# application is CRYPTO=soft for a like reason: TestOTAStageThroughTfwBoot is
# the stager through tfw-boot, and the harness bakes no operator public key, so
# an element-tier image's OTA_ACTIVATE gate (src/core/ota_activate.c) could not
# be opened by anyone on this bench, as it must not be. The network row's
# application is CRYPTO=soft for the same reason: its leg ends in OTA_ACTIVATE.
# The element tier's gate is the net-gate row's: the same network row built
# CRYPTO=auto with the element answering, where OTA_ACTIVATE without a session
# answers TFW_ERR_AUTH_REQUIRED, writes no MARK, and the reset boots the
# primary as it was.
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

## integration-test-boot-prev: BOARD's BOOT_STORE=1 restore from PREV under simavr (#52)
# The harness's prev row: an unconfirmed trial and a refused pending image each
# copy PREV back, GOOD FAILED, and a corrupt PREV halts. Not in
# integration-test-boot, so not in CI: the row is not in BOOT_SUPPORTED until
# its size is measured, and boot-harness-run builds it without that gate.
#
# For the board the caller names, a one-slot board with a store and a PREV (the
# 32U4, not the 328P). Nothing here names one: the profile supplies the store's select
# (TFW_OTA_STORE_CS_IO), the UART (TFW_UART_N) and the simavr core (MCU), and a
# board without PREV is refused before anything is built.
ifneq ($(filter integration-test-boot-prev,$(MAKECMDGOALS)),)
ifneq ($(BOOT_INT_HAS_PREV),1)
$(error integration-test-boot-prev: $(BOARD) has no PREV in a store (boards/$(BOARD)/profile.mk: $(if $(OTA_STORE),$(if $(filter 1,$(BOOT_AB)),BOOT_AB=1$(BOOT_COMMA) it falls back to its secondary slot,BOOT_PREV=0$(BOOT_COMMA) it has no boot-time rollback),no OTA_STORE)). Name a one-slot board with a store: BOARD=<board>)
endif
endif
integration-test-boot-prev:
	@$(MAKE) --no-print-directory boot-harness-run \
	   BOARD=$(BOARD) $(BOOT_INT_STORE_ROW) ETH=0 BOOT_HARNESS_MODE=prev

# The same store row from the host (#75): the application built with the stager
# (OTA_STAGE=1, mk/firmware.mk), the harness in its bridge mode, and the Go SDK
# staging into it over the emulated UART. The test starts the harness itself.
boot-bridge-run: $(BOOT_INT_DIR)/boot_harness $(BOOT_TARGET).elf $(TARGET).elf
	@cd $(SDK_DIR) && TFW_BOOT_HARNESS=$(abspath $(BOOT_INT_DIR)/boot_harness) \
	   TFW_BOOT_ELF=$(abspath $(BOOT_TARGET).elf) TFW_APP_ELF=$(abspath $(TARGET).elf) \
	   $(GO) test -count=1$(call go-tags,-tags,simavr) -run '^TestOTAStageThroughTfwBoot$$' ./cmd/tfwctl/

# The application .elf is a prerequisite and not an assumption: "a correctly
# signed image boots" is observed by the application answering CAPS after the
# jump, so the image in the emulated flash has to be code that runs.
boot-harness-run: $(BOOT_INT_DIR)/boot_harness $(BOOT_TARGET).elf $(TARGET).elf
	@./$(BOOT_INT_DIR)/boot_harness $(BOOT_TARGET).elf $(TARGET).elf $(BOOT_HARNESS_MODE)

.PHONY: integration-test-boot integration-test-boot-prev boot-harness-run boot-bridge-run

# tfw_boot_measure through the boot HAL flash on the ESP32 measure (#163).
$(BOOT_TEST_DIR)/test_boot_measure_esp32_flash: test/unit/boot/test_boot_measure_esp32_flash.c $(ESP_SHA_HOST) \
        test/mock/boot_hal_mock.c $(HOST_DIR)/unity.o | $(BOOT_TEST_DIR)
	@$(HOST_CC) $(BOOT_TEST_CFLAGS) -Itest/mock/esp32 -DCONFIG_IDF_TARGET_ESP32=0 \
	   $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)
