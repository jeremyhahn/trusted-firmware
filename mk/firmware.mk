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

# An architecture that spans more than one family of parts keeps its core in
# HAL_DIR and each SoC's peripherals in a directory under it, named by the
# profile's SOC (src/hal/cortex-m/mps2-tz/). Its files join the same wildcard
# and the same optional list, so a SoC file is selected exactly as a core one
# is. hal_src names the SoC's copy of a module when it has one, else the
# core's. Empty on a board that names no SOC: nothing changes there.
HAL_SOC_DIR := $(if $(SOC),$(HAL_DIR)/$(SOC))
hal_src = $(or $(if $(HAL_SOC_DIR),$(wildcard $(HAL_SOC_DIR)/$(1))),$(HAL_DIR)/$(1))

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

TONE_SRCS := $(if $(call feature_off,TONE),,src/caps/tone.c $(call hal_src,hal_tone.c))

# PWM. The ATtiny85 spends Timer0 on the millisecond tick and Timer1 on the
# software UART's bit clock, and Timer1's control register is not a mega's in
# any case -- so there is no compare unit for tfw_hal_pwm_write to drive and the
# board sets TFW_FEATURE_PWM 0.
PWM_SRCS := $(if $(call feature_off,PWM),,src/caps/pwm.c $(call hal_src,hal_pwm.c))

# EEPROM and pulse measurement, each its own command family over its own HAL
# entry points. A part with neither (the mps2-an505, #161) defines neither
# entry point, and storage.c's bounds against TFW_EEPROM_SIZE 0 do not
# compile under -Werror, so the board drops the whole file.
STORAGE_SRCS := $(if $(call feature_off,EEPROM),,src/caps/storage.c)
PULSE_SRCS   := $(if $(call feature_off,PULSE),,src/caps/pulse.c)

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
BUS_SRCS := $(if $(BUS_OFF),src/caps/bus_none.c,src/caps/bus.c $(call hal_src,hal_bus.c))
# bus.c asks tfw_bus_i2c_held() which addresses a linked driver holds
# (src/caps/bus.h). The OPTIGA Trust M transport answers for its own when the
# crypto tier carries it; bus_held_none.c answers for every other build. Exactly
# one is linked. Recursive, because CRYPTO_SRCS is set further down.
BUS_HELD_SRCS = $(if $(BUS_OFF),,$(if $(filter src/crypto/optiga/optiga_i2c.c,$(CRYPTO_SRCS)),,src/caps/bus_held_none.c))

# The byte link. A part with no USART needs a software one, and the two
# implementations define the same four functions -- so exactly one is linked and
# the choice is the board's, not a flag's.
UART_SRCS := $(if $(call profile_says,TFW_UART_SOFT,1), \
                  $(call hal_src,hal_uart_soft.c),$(call hal_src,hal_uart.c))

# What the transport reads frames from: the UART alone, or the UART and a TCP
# connection through a WIZnet shield -- issue #55, docs/ethernet-transport/.
# Both files define tfw/link.h, so exactly one is linked, and an image built
# without ETH=1 carries no network code at all. The controller is the board's
# word (ETH_CHIP in profile.mk), overridable on the command line for the other
# shield; a board that names none cannot be built with ETH=1. The output path
# carries the choice, as it carries the crypto tier, because an Ethernet image
# is different firmware. A board may make ETH=1 its default (ETH ?= 1 in
# profile.mk, the ATmega2560); every other board's default is serial only.
ETH ?= 0
ifeq ($(ETH),1)
ifeq ($(ETH_CHIP),)
$(error ETH=1 but $(BOARD_DIR)/profile.mk names no ETH_CHIP -- this board has no Ethernet support)
endif
ETH_CHIPS := $(basename $(notdir $(filter-out src/net/w5x00.c src/net/netcfg.c src/net/dhcp.c src/net/dhcp_none.c,$(wildcard src/net/*.c))))
ifeq ($(filter $(ETH_CHIP),$(ETH_CHIPS)),)
$(error unknown ETH_CHIP "$(ETH_CHIP)" -- expected one of: $(ETH_CHIPS))
endif
LINK_SRCS := src/proto/link_eth.c src/net/w5x00.c src/net/netcfg.c src/net/$(ETH_CHIP).c
LINK_DEFS := -DTFW_FEATURE_ETHERNET=1
else
LINK_SRCS := src/proto/link_serial.c
LINK_DEFS :=
endif

# Where an Ethernet image's address comes from: DHCP=1 links the RFC 2131
# client (src/net/dhcp.c) for a network block whose flags ask for a lease;
# otherwise dhcp_none.c takes the block's own addresses and refuses one that
# asks. Off by default until it is measured on a board; the output path carries
# the choice.
DHCP ?= 0
ifeq ($(DHCP),1)
ifneq ($(ETH),1)
$(error DHCP=1 needs ETH=1 -- the client runs on the Ethernet controller)
endif
LINK_SRCS += src/net/dhcp.c
else ifeq ($(ETH),1)
LINK_SRCS += src/net/dhcp_none.c
endif

# The HAL modules that are chosen rather than always present. Filtered out of
# the wildcard below and added back by the lists above, so a file that is gated
# off is not compiled at all -- which is the point, since several of them do not
# compile on the parts that gate them.
HAL_OPTIONAL := $(foreach d,$(HAL_DIR) $(HAL_SOC_DIR),$(d)/hal_tone.c $(d)/hal_pwm.c \
                  $(d)/hal_bus.c $(d)/hal_uart.c $(d)/hal_uart_soft.c) $(ARCH_HAL_OPTIONAL)

# Provisioning commands are a JIG image, never a production one. The gate is
# file selection: PROVISION=1 compiles the translation unit and defines the
# feature, and anything else leaves a production image with no handler at all --
# not a disabled one, not one behind a check an attacker can argue with.
# provision.c is the wire; the backend under it is the secure element's unless
# the architecture names one for the tier (ARCH_PROV_SRCS_<tier>, e.g. the
# ESP32's eFuse UDS). Recursive, because CRYPTO is settled below.
PROVISION ?= 0
ifeq ($(PROVISION),1)
PROV_SRCS   = src/caps/provision.c $(or $(ARCH_PROV_SRCS_$(CRYPTO)),src/caps/provision_atecc.c)
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

# The soft root's identity (#48, #130), linked by the soft and auto tiers:
# `p256` is the ES256 DeviceID, derived from the UDS (src/crypto/deviceid_p256.c)
# and signed with the vendored micro-ecc; `hmac` is the HMAC signer the soft
# root had before, kept for a board whose profile says the P-256 code does not
# fit (the ATmega328P, and the ATmega32U4 until its fit is measured). Chosen by
# file selection, as CRYPTO is.
SOFT_ID           ?= p256
SOFT_ID_SRCS_p256 := src/crypto/soft/id_p256.c src/crypto/deviceid_p256.c \
                     src/third_party/micro-ecc/uECC.c
SOFT_ID_SRCS_hmac := src/crypto/soft/id_hmac.c
SOFT_ID_SRCS      := $(or $(SOFT_ID_SRCS_$(SOFT_ID)),$(error unknown SOFT_ID "$(SOFT_ID)" -- expected p256 or hmac))

ifeq ($(CRYPTO),soft)
CRYPTO_SRCS := src/crypto/sha256.c src/crypto/soft/provider.c src/crypto/soft/uds_flash.c $(SOFT_ID_SRCS)
ALIAS_SRCS  :=
ALIAS_DEFS  :=
# MEASURE_SOFT_CSR=1 is a MEASUREMENT, never a row (issue #53 T3): it links the
# alias builder and ATTEST_CSR into a soft image only to read what they cost on
# a board's budget. With SOFT_ID=p256 the figure includes the P-256 DeviceID's
# signer; with SOFT_ID=hmac the provider has no public half and the image can
# only refuse the command. No profile, row or CI target sets it.
MEASURE_SOFT_CSR ?= 0
ifeq ($(MEASURE_SOFT_CSR),1)
ALIAS_SRCS  := src/caps/alias.c src/caps/tmpl.c src/caps/uccs.c src/caps/csr.c
ALIAS_DEFS  := -DTFW_FEATURE_ALIAS_CERT=1 -DTFW_FEATURE_ATTEST_CSR=1
endif
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
else ifneq ($(filter $(CRYPTO),atecc608a auto),)
CRYPTO_SRCS := src/crypto/sha256.c src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c
# THE MEGA'S RUNTIME TIER (issue #130): everything the element image links, and
# the soft provider beside it, each renamed per object by CRYPTO_RENAME below;
# src/crypto/auto/provider.c chooses one at init -- the element when one
# answers, else the soft root. docs/dice-attestation/architecture.md, "The
# Mega's runtime tier".
ifeq ($(CRYPTO),auto)
CRYPTO_SRCS += src/crypto/soft/provider.c src/crypto/soft/uds_flash.c src/crypto/auto/provider.c \
               $(SOFT_ID_SRCS)
endif
# The alias certificate rides with the tier that can sign one, and only on a
# board with room for it. It is gated by FILE SELECTION, exactly as tone is: a
# certificate names a subject public key, which the element tier's builder
# takes from the element, so a soft image does not contain the code at all
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
AUTH_SRCS   := src/core/auth_session.c $(if $(filter auto,$(CRYPTO)),src/core/auth_session_auto.c)
AUTH_DEFS   := -DTFW_FEATURE_AUTH_SESSION=1
endif
else ifneq ($(filter $(CRYPTO),$(ARCH_CRYPTO_TIERS)),)
# A tier the ARCHITECTURE provides, because its provider drives that
# architecture's own key hardware -- the ESP32's HMAC peripheral, say.
# mk/arch/$(ARCH).mk names the tier and its sources, which live in the HAL
# directory and are listed in ARCH_HAL_OPTIONAL so no other tier links them.
# No alias certificate and no auth session: both are the element tier's.
CRYPTO_SRCS := $(ARCH_CRYPTO_SRCS_$(CRYPTO))
ALIAS_SRCS  :=
ALIAS_DEFS  :=
AUTH_SRCS   := src/core/auth_session_none.c
AUTH_DEFS   :=
else
$(error unknown CRYPTO tier "$(CRYPTO)" -- expected none, soft, atecc608a or auto$(if $(ARCH_CRYPTO_TIERS), or $(ARCH_CRYPTO_TIERS) on $(ARCH)))
endif

# The SUIT manifest reader and activation gate (issue #78), by FILE SELECTION:
# on the element tier, whose stored image key verifies the manifest's ES256,
# and only on a board whose profile.mk opts in with OTA_MANIFEST := 1 -- the
# ATmega2560 today. The 328P and 32U4 sit under the 16 384-byte policy line,
# where the ADR's decision 6 leaves the trailer alone; they opt in only once a
# measured image fits the region budget with the reader in it.
ifneq ($(filter $(CRYPTO)$(OTA_MANIFEST),atecc608a1 auto1),)
MANIFEST_SRCS := src/core/suit.c
else
MANIFEST_SRCS :=
endif

# The application-side OTA stager (issue #75, docs/ota/), by FILE SELECTION. The
# board names its external store's driver in profile.mk (OTA_STORE := spi_nor),
# and an Ethernet image stages by default, the network being the transport an
# update arrives on; OTA_STAGE=1 asks for it on a serial image. A board that
# names no store cannot be built with it, and a board with no store -- or an
# image with neither flag -- links none of it, only ota_stage_none.c's no-op
# tfw_stage_init, which main.c calls on every image to park the store's select.
# hal_bus.c holds the SPI driver the store talks through; it is already linked
# unless all three buses are off, and $(sort) drops the second copy.
# An architecture whose update path is its own (ESP-IDF's esp_ota, #48) names
# the session's file in ARCH_OTA_SRCS: that file is the stager, it stages on
# every image, and it answers OTA_INFO, OTA_ACTIVATE and OTA_CONFIRM over its
# own trial. OTA_AUTH is the shared holder; the trailer checks are tfw-boot's.
OTA_STAGE ?= $(if $(ARCH_OTA_SRCS),1,$(if $(OTA_STORE),$(ETH),0))
ifneq ($(and $(filter 1,$(OTA_STAGE)),$(ARCH_OTA_SRCS)),)
STAGE_SRCS := $(ARCH_OTA_SRCS) src/core/ota_auth.c boot/boot_image.c
STAGE_DEFS := -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_INFO=1 -DTFW_FEATURE_OTA_ACTIVATE=1 \
              -DTFW_FEATURE_OTA_CTL=1
else ifeq ($(OTA_STAGE),1)
ifeq ($(OTA_STORE),)
$(error OTA_STAGE=1 but $(BOARD_DIR)/profile.mk names no OTA_STORE -- this board has no external store)
endif
ifeq ($(wildcard src/store/$(OTA_STORE).c),)
$(error unknown OTA_STORE "$(OTA_STORE)" -- expected one of: $(basename $(notdir $(wildcard src/store/*.c))))
endif
STAGE_SRCS := src/core/ota_stage.c src/store/$(OTA_STORE).c src/crypto/sha256.c $(HAL_DIR)/hal_bus.c
# OTA_BEGIN backs the running image up to PREV where the fallback is the store
# (the 328P and 32U4, #52); the 2560 falls back to its other slot.
ifeq ($(BOOT_AB),1)
STAGE_SRCS += src/core/ota_prev_none.c
else
STAGE_SRCS += src/core/ota_prev.c $(HAL_DIR)/hal_eeprom.c
endif
# OTA_ACTIVATE (#77) is every stager's: OTA_END does not write the MARK, so a
# single-slot board without it could never install what it staged.
STAGE_SRCS += src/core/ota_activate.c src/core/ota_auth.c
STAGE_DEFS := -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_ACTIVATE=1
else
STAGE_SRCS := src/core/ota_stage_none.c
STAGE_DEFS :=
endif
# The output path says so only where the choice is not ETH's default, so every
# image that existed before keeps its path; an ARCH_OTA_SRCS image always stages.
STAGE_SUFFIX := $(if $(ARCH_OTA_SRCS),,$(if $(filter 1,$(OTA_STAGE)),$(if $(filter 1,$(ETH)),,-stage),$(if $(and $(filter 1,$(ETH)),$(OTA_STORE)),-nostage,)))

FW_DIR     := $(BUILD_DIR)/$(BOARD)/$(CRYPTO)$(if $(filter 1,$(PROVISION)),-prov,)$(if $(filter 1,$(ETH)),-eth-$(ETH_CHIP),)$(if $(filter 1,$(DHCP)),-dhcp,)$(STAGE_SUFFIX)$(APP_SUFFIX)$(if $(filter 1,$(MEASURE_SOFT_CSR)),-csr,)
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
             -DTFW_BOARD_HEADER='"board_profile.h"' $(PROV_DEFS) $(ALIAS_DEFS) $(AUTH_DEFS) \
             $(LINK_DEFS) $(STAGE_DEFS) $(APP_DEFS)

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
# An architecture whose bootloader hands off by other means than a fixed SRAM
# address -- ESP-IDF's RTC-retained memory -- says so with ARCH_HANDOFF := 1.
ifneq ($(RAM_END)$(ARCH_HANDOFF),)
IDENTITY_SRCS := src/core/handoff.c src/core/platform_id.c \
                 $(if $(filter src/crypto/soft/id_p256.c,$(CRYPTO_SRCS)),src/core/handoff_seed.c,src/core/handoff_seed_none.c) \
                 src/core/update_request.c \
                 boot/boot_handoff.c boot/boot_platform.c boot/boot_request.c
else
IDENTITY_SRCS := src/core/handoff_none.c src/core/platform_id_none.c \
                 src/core/update_request_none.c
endif
# A bootloader that hands off but opens no update session (ESP-IDF's, until
# its OTA is wired to the wire) takes no request either, so RESET(1) is
# refused rather than quietly behaving as a soft reset.
# The one trial's application half (#77), only on a board whose tfw-boot can
# write TRIAL: the A/B part (BOOT_AB in profile.mk, the 2560). The 328P and
# 32U4 boots have no OTA and no restore yet, so there it would be dead code
# over their flash budget; they and every other board link the stub. An
# architecture whose update path is its own (ARCH_OTA_SRCS, ESP-IDF's) runs
# the same deadline over its own trial state, which that file answers.
ifeq ($(BOOT_AB),1)
IDENTITY_SRCS += src/core/ota_trial.c src/core/ota_trial_block.c
else ifneq ($(ARCH_OTA_SRCS),)
IDENTITY_SRCS += src/core/ota_trial.c
else
IDENTITY_SRCS += src/core/ota_trial_none.c
endif
# OTA_INFO, OTA_CONFIRM and OTA_ACTIVATE (#77, ADR decision 8), by FILE
# SELECTION: only where tfw-boot runs the trial (BOOT_AB) and the image stages
# into the store, the block ACTIVATE verifies. Every other image has no entry
# and answers TFW_ERR_UNKNOWN_CMD. boot_image.c is tfw_boot_trial_settle's floor
# helper, which --gc-sections drops with settle. OTA_INFO's floor is the
# element's counter on the element tier and the platform block's elsewhere
# (src/core/ota_floor.h), chosen here by file.
ifeq ($(BOOT_AB)$(OTA_STAGE),11)
OTACTL_SRCS := src/core/ota_ctl.c boot/boot_trial.c boot/boot_image.c \
               $(if $(filter atecc608a,$(CRYPTO)),src/core/ota_floor_atecc.c,$(if $(filter auto,$(CRYPTO)),src/core/ota_floor_atecc.c src/core/ota_floor_eeprom.c src/core/ota_floor_auto.c,src/core/ota_floor_eeprom.c))
FW_CFLAGS   += -DTFW_FEATURE_OTA_CTL=1
else
OTACTL_SRCS :=
endif
ifeq ($(ARCH_UPDATE_REQUEST),none)
IDENTITY_SRCS := $(filter-out src/core/update_request.c boot/boot_request.c,$(IDENTITY_SRCS)) \
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
#   CRYPTO=auto      both, the element when one answers at init, else soft:
#                    one branch per entry in src/crypto/auto/provider.c
FW_SRCS := $(sort \
  src/main.c \
  src/proto/crc16.c src/proto/frame.c src/proto/transport.c $(LINK_SRCS) \
  src/core/reader.c src/core/writer.c src/core/dispatch.c src/core/board_lookup.c \
  src/core/secure_zero.c $(IDENTITY_SRCS) \
  $(APP_RUNNER_SRCS) \
  src/caps/sys.c src/caps/gpio.c src/caps/sched.c \
  src/caps/analog.c $(STORAGE_SRCS) src/caps/attest.c \
  $(PULSE_SRCS) $(PWM_SRCS) $(BUS_SRCS) $(BUS_HELD_SRCS) $(TONE_SRCS) $(PROV_SRCS) \
  $(CRYPTO_SRCS) $(ALIAS_SRCS) $(AUTH_SRCS) $(MANIFEST_SRCS) $(STAGE_SRCS) $(OTACTL_SRCS) \
  $(BOARD_DIR)/board.c $(UART_SRCS) $(ARCH_FW_SRCS) \
  $(filter-out $(HAL_OPTIONAL),$(wildcard $(HAL_DIR)/*.c $(if $(HAL_SOC_DIR),$(HAL_SOC_DIR)/*.c))))

FW_OBJS := $(patsubst %.c,$(FW_DIR)/%.o,$(FW_SRCS))

# CRYPTO=auto links two providers that define the same eleven tfw_crypto_*
# names. Each is compiled with its names renamed, so neither source changes;
# src/crypto/auto/provider.c defines the public ones. mk/host.mk uses the list.
CRYPTO_RENAME = $(foreach f,init tier algorithms random sha256 hmac sign pubkey counter \
                  verify_operator verify_image,-Dtfw_crypto_$(f)=tfw_crypto_$(1)_$(f))
FLOOR_RENAME = -Dtfw_otactl_floor=tfw_otactl_floor_$(1)
AUTH_RENAME = -Dtfw_auth_session_init=tfw_auth_element_session_init -Dtfw_auth_gate=tfw_auth_element_gate
# AUTO_RENAMES takes an object directory, so the firmware's objects and the
# benchmark's (mk/bench.mk, which compiles the same sources) get one list.
define AUTO_RENAMES
$(1)/src/crypto/soft/provider.o:      FW_CFLAGS += $(call CRYPTO_RENAME,soft)
$(1)/src/crypto/atecc608a/provider.o: FW_CFLAGS += $(call CRYPTO_RENAME,element)
# The auth session the same way: auth_session.c's gate is the element's, and
# src/core/auth_session_auto.c answers the public one on the dispatch's choice.
$(1)/src/core/auth_session.o:         FW_CFLAGS += $(AUTH_RENAME)
# And OTA_INFO's rollback floor: src/core/ota_floor_auto.c reads the element's
# counter or the platform block's floor on the dispatch's choice.
$(1)/src/core/ota_floor_atecc.o:      FW_CFLAGS += $(call FLOOR_RENAME,element)
$(1)/src/core/ota_floor_eeprom.o:     FW_CFLAGS += $(call FLOOR_RENAME,soft)
endef
# The soft root's P-256 objects (SOFT_ID=p256): the vendored micro-ecc is
# compiled as the boot compiles it (mk/boot.mk UECC_DEFS), without our warning
# set -- an upstream's warnings are not ours to fix -- and deviceid_p256.c finds
# uECC.h. Takes an object directory, as AUTO_RENAMES does, for mk/bench.mk.
FW_UECC_CFLAGS = -std=gnu11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) \
                 -ffile-prefix-map=$(CURDIR)=. $(UECC_DEFS) $(ARCH_UECC_CFLAGS) \
                 -Isrc/third_party/micro-ecc
define SOFT_ID_RULES
$(1)/src/third_party/micro-ecc/uECC.o: FW_CFLAGS = $$(FW_UECC_CFLAGS)
$(1)/src/crypto/deviceid_p256.o:       FW_CFLAGS += -Isrc/third_party/micro-ecc
endef
$(eval $(call SOFT_ID_RULES,$(FW_DIR)))
ifeq ($(CRYPTO),auto)
$(eval $(call AUTO_RENAMES,$(FW_DIR)))
endif

$(FW_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(FW_CFLAGS) -MMD -MP -frandom-seed=$(notdir $<) -c -o $@ $<

# The headers each object read, written by -MMD beside it. Without these an edit
# to a header rebuilt no image: the .o depended on its .c and nothing else.
-include $(FW_OBJS:.o=.d)

# An application's sources (mk/app.mk), compiled with the firmware's machine,
# optimisation and warning flags and its reproducibility flags, but against
# include/ and the board only: src/ is the firmware's own, and an application
# that reached into it would break on the next refactor of a file it never
# named. APP_PREFIX_MAP keeps the application's path out of the image, as
# -ffile-prefix-map=$(CURDIR) keeps this one's.
APP_FW_OBJS := $(patsubst %.c,$(FW_DIR)/app/%.o,$(APP_SRCS))
APP_FW_CFLAGS := -std=c11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) $(APP_WARN_CFLAGS) \
                 -ffile-prefix-map=$(CURDIR)=. $(APP_PREFIX_MAP) \
                 -Iinclude -I$(HAL_DIR) -I$(BOARD_DIR) \
                 -DTFW_BOARD_HEADER='"board_profile.h"' $(PROV_DEFS) $(ALIAS_DEFS) $(AUTH_DEFS) \
                 $(APP_CFLAGS)

ifneq ($(APP_SRCS),)
$(APP_FW_OBJS): $(FW_DIR)/app/%.o: $(APP_ABS)/%.c
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(APP_FW_CFLAGS) -MMD -MP -frandom-seed=$(notdir $<) -c -o $@ $<

-include $(APP_FW_OBJS:.o=.d)
endif

$(TARGET).elf: $(FW_OBJS) $(APP_FW_OBJS)
	@$(ARCH_CC) $(FW_LDFLAGS) -o $@ $(FW_OBJS) $(APP_FW_OBJS)

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
# Rows a board builds besides its tiers at their defaults: "tier:VAR=value",
# more than one setting comma-separated ("soft:ETH=1,OTA_STAGE=0"), each a
# named configuration with the same size-check as the default rows. The
# 2560's ETH=0 rows (the shieldless bench Mega) and the 328P's soft-tier
# Ethernet node are here (issue #55 item 48).
BOARD_ROWS      ?=
# A board whose profile.mk sets IMAGE_TARGET is built by that target and not by
# `make firmware` -- an ESP-IDF board is built by ESP-IDF's own toolchain, which
# the CI image does not carry. It stays out of this list, so firmware-matrix,
# and provenance never build it, and board-sweep (which reads
# every directory) still compiles and tests its descriptor.
OTHER_IMAGE_BOARDS := $(notdir $(patsubst %/profile.mk,%,$(shell grep -l '^IMAGE_TARGET[[:space:]]*:\?=' boards/*/profile.mk)))
FIRMWARE_BOARDS := $(filter-out $(OTHER_IMAGE_BOARDS),$(notdir $(wildcard boards/*)))

## board-tiers: print the crypto tiers BOARD supports (used by firmware-matrix)
board-tiers:
	@echo $(BOARD_TIERS)

## feature-defs: print the -D feature flags the BOARD/CRYPTO image compiles with (used by board-sweep)
# The same variables FW_CFLAGS carries, so a test compiled with this output sees
# the gates the image was built with -- not config.h's defaults, which say 0 to
# every tier-selected feature on every board.
feature-defs:
	@echo $(PROV_DEFS) $(ALIAS_DEFS) $(AUTH_DEFS) $(LINK_DEFS) $(STAGE_DEFS) $(APP_DEFS)

## board-eth-chip: print the Ethernet controller BOARD names, or nothing (used by platform-bake-no-link-check)
board-eth-chip:
	@echo $(ETH_CHIP)

## firmware-srcs: print the object list `firmware` links for BOARD/CRYPTO/PROVISION/ETH/APP (used by platform-bake-no-link-check)
firmware-srcs:
	@echo $(FW_SRCS) $(APP_SRCS)

## board-rows: print BOARD's extra "tier:VAR=value" rows (used by firmware-matrix-list)
board-rows:
	@echo $(BOARD_ROWS)

## firmware-matrix-list: print every "board tier [VAR=value]" row firmware-matrix builds
firmware-matrix-list:
	@for b in $(FIRMWARE_BOARDS); do \
	  for t in $$($(MAKE) -s --no-print-directory board-tiers BOARD=$$b); do \
	    echo "$$b $$t"; \
	  done; \
	  for r in $$($(MAKE) -s --no-print-directory board-rows BOARD=$$b); do \
	    v=$${r#*:}; echo "$$b $${r%%:*} $${v//,/ }"; \
	  done; \
	done

.PHONY: board-tiers board-rows board-eth-chip feature-defs firmware-srcs firmware-matrix-list

firmware-matrix:
	@fail=0; n=0; \
	while read -r b t v; do \
	  $(MAKE) --no-print-directory firmware BOARD=$$b CRYPTO=$$t $$v || fail=1; \
	  n=$$((n+1)); \
	done < <($(MAKE) -s --no-print-directory firmware-matrix-list); \
	[ "$$n" -gt 0 ] || { echo "firmware-matrix built nothing"; exit 1; }; \
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
	 ram=$$($(call ARCH_RAM_USED,$<)); \
	 case "$$flash$$ram" in ''|*[!0-9]*) echo "size-check: cannot measure $<"; exit 1;; esac; \
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
