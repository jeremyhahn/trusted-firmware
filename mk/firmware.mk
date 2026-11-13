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

BOARD_DEFAULT := atmega328p
BOARD      ?= $(BOARD_DEFAULT)
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
# profile's SOC (src/hal/cortex-m/mps2/). Its files join the same wildcard
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
#
# A board whose profile turns a feature on only for a named row (mps2-an505's
# I2C=1) lists it in BOARD_FEATURES_ON from profile.mk, beside the -D that
# board_profile.h tests; its #else still says 0, which the grep would read.
feature_off  = $(if $(filter $(1),$(BOARD_FEATURES_ON)),,$(call profile_says,TFW_FEATURE_$(1),0))

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
# The EEPROM HAL is every board's that has one, the feature on or off (the
# MPS2 boards' writes OTA_CONFIRM's request through it), and hal_src picks a
# SoC's copy over the core's. The MCXN94x's writes its own flash sector
# through tfw-boot's flash helper, linked here as STAGE_SRCS links
# boot/boot_image.c.
EEPROM_HAL_SRCS := $(wildcard $(call hal_src,hal_eeprom.c))
ifneq ($(filter src/hal/cortex-m/mcxn94x/hal_eeprom.c,$(EEPROM_HAL_SRCS)),)
EEPROM_HAL_SRCS += $(if $(call feature_off,EEPROM),,boot/hal/cortex-m/mcxn94x/mcxn_flash.c)
endif
# The pulse timer is the HAL's hal_pulse.c (AVR, Cortex-M), compiled only where
# the feature is on; the ESP32 names its own of two (ARCH_PULSE_SRCS in
# mk/arch/esp32.mk), so a file the arch lists is left to the arch.
PULSE_SRCS   := $(if $(call feature_off,PULSE),,src/caps/pulse.c \
                  $(filter-out $(ARCH_HAL_OPTIONAL),$(wildcard $(call hal_src,hal_pulse.c))))

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
# An architecture whose bus driver sits over a register file of its own names it
# in ARCH_BUS_SRCS (the MPS2 SBCon, mk/arch/cortex-m.mk), and lists it in
# ARCH_HAL_OPTIONAL so it goes with hal_bus.c.
BUS_SRCS := $(if $(BUS_OFF),src/caps/bus_none.c,src/caps/bus.c $(call hal_src,hal_bus.c) $(ARCH_BUS_SRCS))
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
HAL_OPTIONAL := $(foreach d,$(HAL_DIR) $(HAL_SOC_DIR),$(d)/hal_tone.c $(d)/hal_pwm.c $(d)/hal_pulse.c \
                  $(d)/hal_bus.c $(d)/hal_uart.c $(d)/hal_uart_soft.c $(d)/hsm_port.c \
                  $(d)/hsm_intake.c $(d)/hal_eeprom.c) $(ARCH_HAL_OPTIONAL)

# Provisioning commands are a JIG image, never a production one. The gate is
# file selection: PROVISION=1 compiles the translation unit and defines the
# feature, and anything else leaves a production image with no handler at all --
# not a disabled one, not one behind a check an attacker can argue with.
# provision.c is the wire; the backend under it is the secure element's unless
# the architecture names one for the tier (ARCH_PROV_SRCS_<tier>, e.g. the
# ESP32's eFuse UDS). Recursive, because CRYPTO is settled below.
PROVISION ?= 0
# A tier with no provisioning backend says so here (PROV_BACKEND := none) beside
# the $(error) that refuses its PROVISION=1; provision-backend prints it, so
# prov-absent-check plans no jig image the build would refuse.
PROV_BACKEND :=
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

# A tier a board's profile refuses (BOARD_REFUSED_TIERS) is not built, not even
# on request: the ATmega328P is supported only with a secure element (owner
# ruling 2026-10-17, m522, item 79), so CRYPTO=soft there is an image nobody
# supports. Asked for anyway, it fails here, as an unknown tier does below.
ifneq ($(filter $(CRYPTO),$(BOARD_REFUSED_TIERS)),)
$(error CRYPTO=$(CRYPTO) is refused on $(BOARD) -- the board is supported only with its $(BOARD_TIERS) tier (owner ruling 2026-10-17, m522))
endif

# MEASURE_SOFT_CSR=1 (the soft tier, below) measures the one soft AVR row left,
# the ATmega2560's (issue #53 T3). The 328P's soft rows went with owner ruling
# 2026-10-17 (m522), so asked for anywhere else, or on another tier, it fails
# here rather than build an image nobody supports or quietly measure nothing.
MEASURE_SOFT_CSR       ?= 0
MEASURE_SOFT_CSR_BOARD := atmega2560
ifeq ($(MEASURE_SOFT_CSR),1)
ifneq ($(BOARD),$(MEASURE_SOFT_CSR_BOARD))
$(error MEASURE_SOFT_CSR=1 is refused on $(BOARD) -- it measures the $(MEASURE_SOFT_CSR_BOARD) soft row only (owner ruling 2026-10-17, m522))
endif
ifneq ($(CRYPTO),soft)
$(error MEASURE_SOFT_CSR=1 measures CRYPTO=soft, not CRYPTO=$(CRYPTO))
endif
endif

# The soft root's identity (#48, #130), linked by the soft and auto tiers:
# `p256` is the ES256 DeviceID, derived from the UDS (src/crypto/deviceid_p256.c)
# and signed with the vendored micro-ecc; `hmac` is the HMAC signer the soft
# root had before, kept for a board whose profile says the P-256 code does not
# fit (the ATmega328P, and the ATmega32U4 until its fit is measured). Chosen by
# file selection, as CRYPTO is. The board's default is BOARD_SOFT_ID in its
# profile.mk, so FW_DIR can tell a command-line SOFT_ID from the board's own.
SOFT_ID_DEFAULT   := $(or $(BOARD_SOFT_ID),p256)
SOFT_ID           ?= $(SOFT_ID_DEFAULT)
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
# the ATmega2560's budget (MEASURE_SOFT_CSR_BOARD above). With SOFT_ID=p256 the
# figure includes the P-256 DeviceID's signer; with SOFT_ID=hmac the provider has
# no public half and the image can only refuse the command. No profile, row or CI
# target sets it.
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
else ifeq ($(CRYPTO),optiga)
# THE OPTIGA TRUST M TIER (row optiga-trust-m,
# docs/multi-arch/adr-optiga-trust-m-onboarding.md): the I2C transport, the
# read-only command layer and the provider over them, with the shared software
# SHA-256. No board's BOARD_TIERS names it -- which boards it fits is the ADR's
# open question 5 -- so only a measurement row in a profile's BOARD_ROWS builds
# it: the 2560's two, and mps2-an505's over the SBCon (I2C=1). One provider is linked, so nothing is renamed: CRYPTO_RENAME is auto's.
CRYPTO_SRCS := src/crypto/sha256.c src/crypto/optiga/optiga_i2c.c src/crypto/optiga/optiga_cmd.c \
               src/crypto/optiga/optiga_crypto.c
# No alias certificate: its builder names the element's public key, which this
# provider cannot read (pubkey is TFW_ERR_UNSUPPORTED; the key is in the factory
# certificate, and X.509 is go-xkms's). The auth session is the element tier's,
# with the same profile knob: it needs only random, SHA-256 and verify_operator,
# which is VerifySign here, and the operator key from the platform block.
ALIAS_SRCS  :=
ALIAS_DEFS  :=
ifeq ($(call feature_off,AUTH_SESSION),1)
AUTH_SRCS   := src/core/auth_session_none.c
AUTH_DEFS   :=
else
AUTH_SRCS   := src/core/auth_session.c
AUTH_DEFS   := -DTFW_FEATURE_AUTH_SESSION=1
endif
# No provisioning image: every PROV_* backend writes the element, and the ADR's
# onboarding rule 6 forbids any write to it until the owner rules (question 4).
# OTA_INFO's floor is the platform block's (ota_floor_eeprom.c, as on soft):
# the element's monotonic counters are read, never incremented, on this tier.
# The element answers on I2C and nowhere else (the Solution Reference Manual's
# host interface), so a build whose board has the bus off is refused here rather
# than at the link, where the missing tfw_hal_i2c_* names would not say why.
# mps2-an505 turns the bus on for its row with I2C=1.
ifeq ($(call feature_off,I2C),1)
$(error CRYPTO=optiga needs the I2C bus -- $(BOARD) has it off (a board that turns it on per row, as mps2-an505 does, takes I2C=1))
endif
PROV_BACKEND := none
ifeq ($(PROVISION),1)
$(error PROVISION=1 has no backend on CRYPTO=optiga -- provisioning writes the element, which the OPTIGA ADR forbids until the owner rules)
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
$(error unknown CRYPTO tier "$(CRYPTO)" -- expected none, soft, atecc608a, auto or optiga$(if $(ARCH_CRYPTO_TIERS), or $(ARCH_CRYPTO_TIERS) on $(ARCH)))
endif

# The SUIT manifest reader and activation gate (issue #78), by FILE SELECTION:
# on the element tier, whose stored image key verifies the manifest's ES256,
# and only on a board whose profile.mk opts in with OTA_MANIFEST := 1 -- the
# ATmega2560 today. The 328P and 32U4 sit under the 16 384-byte policy line,
# where the ADR's decision 6 leaves the trailer alone, and they do not opt in.
# The 4 KB parts keep it off, as ruled (owner queue 176, option (a)): refused
# here, not left to link the stub quietly.
ifneq ($(filter $(BOARD),atmega328p atmega32u4),)
ifeq ($(OTA_MANIFEST),1)
$(error OTA_MANIFEST=1 is refused on $(BOARD) -- the manifest check stays off the ATmega328P and ATmega32U4 (owner queue 176, option (a)); the image is left to tfw-boot's signature. See docs/ota/README.md)
endif
endif
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
STAGE_SRCS := src/core/ota_stage.c src/store/$(OTA_STORE).c src/crypto/sha256.c
# A store on a bus needs the bus; the memory store (OTA_STORE := mem, the MPS2
# boards) has none, and its architecture no hal_bus.c to link.
ifneq ($(OTA_STORE),mem)
STAGE_SRCS += $(HAL_DIR)/hal_bus.c
endif
# OTA_BEGIN backs the running image up to PREV where the fallback is the store
# and tfw-boot restores from it (the 32U4 and the MPS2 boards, #52); the 2560
# falls back to its other slot, as an MPS2 board built with BOOT_AB=1 does. The
# 328P no longer backs up to PREV: its tfw-boot has no restore, and its profile
# sets OTA_NO_PREV (#317). The MPS2 boards have
# no EEPROM: their platform block is tfw-boot's, in the code bank, and their
# hal_eeprom.c writes only past it (OTA_CONFIRM's request).
ifneq ($(filter 1,$(BOOT_AB) $(OTA_NO_PREV)),)
STAGE_SRCS += src/core/ota_prev_none.c
else
STAGE_SRCS += src/core/ota_prev.c $(EEPROM_HAL_SRCS)
endif
# OTA_ACTIVATE (#77) is every stager's: OTA_END does not write the MARK, so a
# single-slot board without it could never install what it staged.
STAGE_SRCS += src/core/ota_activate.c src/core/ota_auth.c
# Its manifest check (ruling 121) where the board links the SUIT reader; the
# stub where it does not, which leaves the image to tfw-boot's signature.
STAGE_SRCS += $(if $(MANIFEST_SRCS),src/core/ota_manifest.c boot/boot_image.c,src/core/ota_manifest_none.c)
STAGE_DEFS := -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_ACTIVATE=1
else
STAGE_SRCS := src/core/ota_stage_none.c
STAGE_DEFS :=
endif
# The output path says so only where the choice is not ETH's default, so every
# image that existed before keeps its path; an ARCH_OTA_SRCS image always stages.
STAGE_SUFFIX := $(if $(ARCH_OTA_SRCS),,$(if $(filter 1,$(OTA_STAGE)),$(if $(filter 1,$(ETH)),,-stage),$(if $(and $(filter 1,$(ETH)),$(OTA_STORE)),-nostage,)))
# The soft root's identity and micro-ecc's optimisation level (mk/boot.mk
# UECC_OPT) change a soft or auto image's objects but not its tier. Make does not
# track a flag, so two rows in one directory would link each other's objects:
# the path names each where the tier links it and it is not the default.
ID_SUFFIX := $(if $(filter $(SOFT_ID_SRCS),$(CRYPTO_SRCS)),$(if $(filter-out $(SOFT_ID_DEFAULT),$(SOFT_ID)),-$(SOFT_ID)))$(if $(and $(filter src/third_party/micro-ecc/uECC.c,$(CRYPTO_SRCS)),$(filter-out 0,$(UECC_OPT))),-uecc$(UECC_OPT))

# Likewise -ab, only on a board where two slots are a choice (BOOT_AB_OPTIONAL
# in profile.mk, the MPS2 boards); the 2560 always has two and keeps its paths.
# mk/boot.mk names tfw-boot's directory with it too.
AB_SUFFIX := $(if $(BOOT_AB_OPTIONAL),$(if $(filter 1,$(BOOT_AB)),-ab))
# And a bus a board turns on only for a named row (BOARD_FEATURES_ON in
# profile.mk: the MPS2 boards' soft:I2C=1 and soft:SPI=1), as -i2c and -spi.
ROW_FEATURE_SUFFIX := $(if $(strip $(BOARD_FEATURES_ON)),$(foreach f,$(shell printf '%s' \
                      '$(strip $(BOARD_FEATURES_ON))' | tr A-Z a-z),-$(f)))

# Every make variable that changes the image's objects names the directory, and
# only where it differs from its default, so every default image keeps its path.
FW_DIR     := $(BUILD_DIR)/$(BOARD)/$(CRYPTO)$(if $(filter 1,$(PROVISION)),-prov,)$(if $(filter 1,$(ETH)),-eth-$(ETH_CHIP),)$(if $(filter 1,$(DHCP)),-dhcp,)$(STAGE_SUFFIX)$(AB_SUFFIX)$(ROW_FEATURE_SUFFIX)$(APP_SUFFIX)$(if $(filter 1,$(MEASURE_SOFT_CSR)),-csr,)$(ID_SUFFIX)
TARGET     := $(FW_DIR)/trusted-firmware

# The row, for the bench images that compile with FW_CFLAGS in a directory of
# their own (mk/bench.mk, mk/platform-bake.mk, mk/wiznet-probe.mk): FW_CFLAGS
# carries every row flag's defines, so a row flag reaches their objects too.
# FW_ROW_SUFFIX names the row only where a row flag is given rather than taken
# from the makefiles, so a bench image built with no flags keeps its path.
FW_ROW        := $(notdir $(FW_DIR))
FW_ROW_FLAGS  := CRYPTO PROVISION ETH ETH_CHIP DHCP OTA_STAGE MEASURE_SOFT_CSR SOFT_ID UECC_OPT APP_DIR SPI
FW_ROW_SUFFIX := $(if $(filter command environment override,$(foreach v,$(FW_ROW_FLAGS),$(firstword $(origin $(v))))),-$(FW_ROW))

# The toolchain binaries and the flags that name a machine, an optimisation
# policy and a link placement are the ARCHITECTURE'S (mk/arch/$(ARCH).mk). What
# is the project's, and the same on every architecture, is the warning set and
# the reproducibility flags below.
#
# The include path is named once, because board-sweep preprocesses the
# stager's sources with it: a file that finds its header only under a host
# build's wider path (src/store/mem.c and -Isrc/core) fails there, not in QEMU.
FW_INCLUDES := -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Iboot -I$(HAL_DIR) -I$(BOARD_DIR)

# The reproducibility flags are NOT optional. FWHash is signed evidence, so a
# verifier needs a golden measurement computable from source -- which requires
# that the same source produce the same bytes on any machine. -ffile-prefix-map
# strips the build path, -frandom-seed makes the compiler's internal symbol
# naming deterministic, and SOURCE_DATE_EPOCH pins anything time-derived.
FW_CFLAGS := -std=c11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) \
             -Wall -Wextra -Werror -Wconversion -Wshadow \
             -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
             -ffile-prefix-map=$(CURDIR)=. \
             $(FW_INCLUDES) \
             -DTFW_BOARD_HEADER='"board_profile.h"' $(PROV_DEFS) $(ALIAS_DEFS) $(AUTH_DEFS) \
             $(LINK_DEFS) $(STAGE_DEFS) $(APP_DEFS) $(BOARD_DEFS)

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
# write TRIAL: the A/B part (BOOT_AB in profile.mk, the 2560, and an MPS2
# board built with BOOT_AB=1). The 328P and
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
# One update path per image on the A/B part (#52, question 6, decided
# 2026-10-26), by FILE SELECTION. An image with the stager is the store's: its
# tfw-boot is the BOOT_STORE=1 row, which opens no session, so RESET(1) answers
# TFW_ERR_UNSUPPORTED (update_request_store.c). An image without it is OTA in
# tfw-boot's, the BOOT_OTA=1 row: RESET(1) opens the session, and OTA_BEGIN,
# OTA_DATA, OTA_END, OTA_STATUS, OTA_ACTIVATE, OTA_AUTH, OTA_INFO and
# OTA_CONFIRM have no entry and answer TFW_ERR_UNKNOWN_CMD. mk/boot.mk refuses
# a tfw-boot with both.
ifeq ($(BOOT_AB)$(OTA_STAGE),11)
ifneq ($(filter src/core/update_request.c,$(IDENTITY_SRCS)),)
IDENTITY_SRCS := $(filter-out src/core/update_request.c,$(IDENTITY_SRCS)) \
                 src/core/update_request_store.c
endif
endif
# The tamper-response engine (#50, docs/hsm-self-defence/m7-design.md slice 3),
# by FILE SELECTION: a board whose profile.mk says HSM := 1 links the engine,
# its SUIT reader, the channel's commands (HSM_* in commands.def) and the loop
# main.c polls, behind the SoC's hsm_port.c (in HAL_SOC_DIR, HAL_OPTIONAL so it
# is linked here and nowhere else). Every other image links hsm_loop_none.c's
# no-op. The policy set is verified under the tenant key
# in software here: the soft provider's tfw_crypto_verify_operator refuses
# (issue #4), so on an HSM image alone it is renamed away and
# src/crypto/soft/verify_p256.c answers, with micro-ecc linked for it. The
# soft tier only: an element image has the element's verify and no board
# asks for HSM with one yet.
ifeq ($(HSM)$(CRYPTO),1soft)
HSM_SRCS := src/core/suit.c src/core/suit_integrated.c src/core/hsm_payload.c \
            src/core/hsm_engine.c src/core/hsm_cmd.c src/core/hsm_loop.c \
            src/core/hsm_record.c src/core/hsm_store_ram.c \
            src/crypto/soft/verify_p256.c src/third_party/micro-ecc/uECC.c \
            $(wildcard $(HAL_SOC_DIR)/hsm_port.c) $(wildcard $(HAL_SOC_DIR)/hsm_intake.c)
HSM_DEFS := -DTFW_FEATURE_HSM=1
$(FW_DIR)/src/crypto/soft/provider.o: FW_CFLAGS += -Dtfw_crypto_verify_operator=tfw_crypto_soft_verify_refused
$(FW_DIR)/src/crypto/soft/verify_p256.o: FW_CFLAGS += -Isrc/third_party/micro-ecc
else
HSM_SRCS := src/core/hsm_loop_none.c
HSM_DEFS :=
endif
FW_CFLAGS += $(HSM_DEFS)

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
#   CRYPTO=optiga    the OPTIGA Trust M signs, counts and verifies; a
#                    measurement row only, no board's tier (ADR question 5)
FW_SRCS := $(sort \
  src/main.c \
  src/proto/crc16.c src/proto/frame.c src/proto/transport.c $(LINK_SRCS) \
  src/core/reader.c src/core/writer.c src/core/dispatch.c src/core/board_lookup.c \
  src/core/secure_zero.c $(IDENTITY_SRCS) \
  $(APP_RUNNER_SRCS) \
  src/caps/sys.c src/caps/gpio.c src/caps/sched.c \
  src/caps/analog.c $(STORAGE_SRCS) $(EEPROM_HAL_SRCS) src/caps/attest.c \
  $(PULSE_SRCS) $(PWM_SRCS) $(BUS_SRCS) $(BUS_HELD_SRCS) $(TONE_SRCS) $(PROV_SRCS) \
  $(CRYPTO_SRCS) $(ALIAS_SRCS) $(AUTH_SRCS) $(MANIFEST_SRCS) $(STAGE_SRCS) $(OTACTL_SRCS) $(HSM_SRCS) \
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
# 2560's ETH=0 rows (the shieldless bench Mega) are here (issue #55 item 48);
# the 328P's soft-tier Ethernet node was, until the owner withdrew it
# (2026-10-17, m522).
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
	@echo $(PROV_DEFS) $(ALIAS_DEFS) $(AUTH_DEFS) $(LINK_DEFS) $(STAGE_DEFS) $(APP_DEFS) $(HSM_DEFS)

## firmware-dir: print the directory `firmware` builds BOARD's image in, for the flags given (used by scripts/test_build_dirs.py)
firmware-dir:
	@echo $(FW_DIR)

## board-eth-chip: print the Ethernet controller BOARD names, or nothing (used by platform-bake-no-link-check)
board-eth-chip:
	@echo $(ETH_CHIP)

## firmware-srcs: print the object list `firmware` links for BOARD/CRYPTO/PROVISION/ETH/APP (used by platform-bake-no-link-check)
firmware-srcs:
	@echo $(FW_SRCS) $(APP_SRCS)

## firmware-includes: print the include path every firmware object compiles with (used by board-sweep)
firmware-includes:
	@echo $(FW_INCLUDES)

## board-ota-store: print the image store BOARD names, or nothing (used by board-sweep)
board-ota-store:
	@echo $(OTA_STORE)

## stage-srcs: print the stager's sources for BOARD/CRYPTO/OTA_STAGE (used by board-sweep)
stage-srcs:
	@echo $(STAGE_SRCS)

## provision-backend: print "none" when BOARD/CRYPTO has no provisioning backend, else "yes" (used by prov-absent-check)
provision-backend:
	@echo $(or $(PROV_BACKEND),yes)

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

.PHONY: board-tiers board-rows board-eth-chip board-ota-store feature-defs firmware-dir \
        firmware-srcs firmware-includes stage-srcs firmware-matrix-list provision-backend

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

## reproducible-check: build twice from clean copies of HEAD and diff the image
# Without this the platform's attestation verdict can never be better than
# "trust whatever hash the device reported", because nobody can compute the
# expected hash from source.
#
# Each copy is `git archive HEAD` (scripts/repro_build.py): no build/, no
# untracked file, so an image a previous build left cannot satisfy it, and
# SOURCE_DATE_EPOCH is the commit time.
reproducible-check:
	@python3 scripts/repro_build.py --make "$(MAKE)" --image $(TARGET).hex \
	   --dir /tmp/agrepro-a --dir /tmp/agrepro-b -- \
	   BOARD=$(BOARD) CRYPTO=$(CRYPTO) $(TARGET).hex

## flash: write the firmware to a connected board
# The programmer is the architecture's: ARCH_FLASH is a recipe from
# mk/arch/$(ARCH).mk, so this is the one `flash` for every board.
flash: $(TARGET).hex
	$(ARCH_FLASH)

.PHONY: firmware size-check reproducible-check flash
