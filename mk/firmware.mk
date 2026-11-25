# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The device build, for whatever architecture the board says it is.

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

# Where this architecture's register-level code lives.
HAL_DIR := src/hal/$(ARCH)

# An architecture that spans more than one family of parts keeps its core in HAL_DIR and each SoC's peripherals in a directory under it, named by the profile's SOC (src/hal/cortex-m/mps2/).
HAL_SOC_DIR := $(if $(SOC),$(HAL_DIR)/$(SOC))
hal_src = $(or $(if $(HAL_SOC_DIR),$(wildcard $(HAL_SOC_DIR)/$(1))),$(HAL_DIR)/$(1))

# The crypto tier is part of the output path, not just the object list.
profile_says = $(shell grep -Eq '^[[:space:]]*#[[:space:]]*define[[:space:]]+$(1)[[:space:]]+$(2)[[:space:]]*(/\*.*)?$$' \
                       $(BOARD_DIR)/board_profile.h && echo 1)
#
# A board whose profile turns a feature on only for a named row (mps2-an505's I2C=1) lists it in BOARD_FEATURES_ON from profile.mk, beside the -D that board_profile.h tests; its #else still says 0, which the grep would read.
feature_off  = $(if $(filter $(1),$(BOARD_FEATURES_ON)),,$(call profile_says,TFW_FEATURE_$(1),0))

TONE_SRCS := $(if $(call feature_off,TONE),,src/caps/tone.c $(call hal_src,hal_tone.c))

# PWM.
PWM_SRCS := $(if $(call feature_off,PWM),,src/caps/pwm.c $(call hal_src,hal_pwm.c))

# EEPROM and pulse measurement, each its own command family over its own HAL entry points.
STORAGE_SRCS := $(if $(call feature_off,EEPROM),,src/caps/storage.c)

# The EEPROM HAL, one file on every board: hal_src picks the SoC's copy where the family is on and the SoC has one, else the architecture's.
SOC_EEPROM_SRCS_stm32h5 := src/hal/cortex-m/stm32h5/ee_flash.c src/hal/cortex-m/stm32h5/ee_flash_port.c \
                           boot/hal/cortex-m/stm32h5/h5_flash.c
SOC_EEPROM_SRCS_mcxn94x := boot/hal/cortex-m/mcxn94x/mcxn_flash.c
SOC_EEPROM_SRCS_rp2350  := boot/hal/cortex-m/rp2350/rp_flash.c boot/hal/cortex-m/rp2350/rp_flash_rom.c \
                           boot/hal/cortex-m/rp2350/rp_rom_lookup.c
EEPROM_HAL_SRCS := $(if $(call feature_off,EEPROM),$(wildcard $(HAL_DIR)/hal_eeprom.c),\
                     $(wildcard $(call hal_src,hal_eeprom.c)) \
                     $(if $(wildcard $(HAL_SOC_DIR)/hal_eeprom.c),$(SOC_EEPROM_SRCS_$(SOC))))
# The pulse timer is the HAL's hal_pulse.c (AVR, Cortex-M), compiled only where the feature is on; the ESP32 names its own of two (ARCH_PULSE_SRCS in mk/arch/esp32.mk), so a file the arch lists is left to the arch.
PULSE_SRCS   := $(if $(call feature_off,PULSE),,src/caps/pulse.c \
                  $(filter-out $(ARCH_HAL_OPTIONAL),$(wildcard $(call hal_src,hal_pulse.c))))

# The three buses, gated as one unit because they are one translation unit.
BUS_OFF  := $(and $(call feature_off,I2C),$(call feature_off,SPI),$(call feature_off,ONEWIRE))
# main.c calls tfw_bus_init unconditionally (no #if there, by the same rule as everywhere else): bus.c provides it when any bus is on, and bus_none.c, a no-op, provides the same symbol when BUS_OFF, so exactly one is always linked.
BUS_SRCS := $(if $(BUS_OFF),src/caps/bus_none.c,src/caps/bus.c $(call hal_src,hal_bus.c) $(ARCH_BUS_SRCS))
# bus.c asks tfw_bus_i2c_held which addresses a linked driver holds (src/caps/bus.h).
BUS_HELD_SRCS = $(if $(BUS_OFF),,$(if $(filter src/crypto/optiga/optiga_i2c.c,$(CRYPTO_SRCS)),,src/caps/bus_held_none.c))

# The byte link.
UART_SRCS := $(if $(call profile_says,TFW_UART_SOFT,1), \
                  $(call hal_src,hal_uart_soft.c),$(call hal_src,hal_uart.c))

# What the transport reads frames from: the UART alone, or the UART and a TCP connection through a WIZnet shield, issue, docs/ethernet-transport/.
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

# Where an Ethernet image's address comes from: DHCP=1 links the RFC 2131 client (src/net/dhcp.c) for a network block whose flags ask for a lease; otherwise dhcp_none.c takes the block's own addresses and refuses one that asks.
DHCP ?= 0
ifeq ($(DHCP),1)
ifneq ($(ETH),1)
$(error DHCP=1 needs ETH=1 -- the client runs on the Ethernet controller)
endif
LINK_SRCS += src/net/dhcp.c
else ifeq ($(ETH),1)
LINK_SRCS += src/net/dhcp_none.c
endif

# The SoC's own Ethernet MAC under lwIP NO_SYS: ETH_MAC=1 links the SoC's hal_eth_netif.c and lwIP's core, IPv4 and Ethernet sources (third_party/lwip, pinned in its VERSION), and the main loop polls the interface; the console prints "eth: dhcp bound" on a lease.
ETH_MAC ?= 0
LWIP_DIR := third_party/lwip/src

## lwip-check: the vendored lwIP is byte-identical to upstream at its VERSION's commit (third_party/lwip/SHA256SUMS)
.PHONY: lwip-check lwip-upstream-check
lwip-check: lwip-upstream-check
	@python3 -B scripts/vendor_sums.py third_party/lwip

## lwip-upstream-check: every vendored lwIP file (src/, COPYING, README) is byte-identical to the upstream release archive (SHA-256 checked) and to upstream git at LWIP_UPSTREAM_COMMIT
LWIP_ARCHIVE_URL     ?= https://github.com/lwip-tcpip/lwip/archive/refs/tags/STABLE-2_2_1_RELEASE.tar.gz
LWIP_ARCHIVE_SHA256  := ce0b7461c0ad9602c376f0bf07c5eb7253b48c7bf66f011c6bf3e2a96731c539
LWIP_UPSTREAM_URL    ?= https://github.com/lwip-tcpip/lwip
LWIP_UPSTREAM_COMMIT := 77dcd25a72509eb83f72b033d219b1d40cd8eb95
lwip-upstream-check:
	@python3 -I -B scripts/lwip_upstream_check.py --archive third_party/lwip $(LWIP_ARCHIVE_URL) $(LWIP_ARCHIVE_SHA256)
	@python3 -B scripts/lwip_upstream_check.py third_party/lwip $(LWIP_UPSTREAM_URL) $(LWIP_UPSTREAM_COMMIT)
ifeq ($(ETH_MAC),1)
ifeq ($(wildcard $(HAL_SOC_DIR)/hal_eth_netif.c),)
$(error ETH_MAC=1 but $(HAL_SOC_DIR) has no hal_eth_netif.c -- this SoC has no MAC driver)
endif
ifeq ($(ETH),1)
$(error ETH_MAC=1 and ETH=1 both name the image's link -- pick one)
endif
LINK_SRCS := $(HAL_SOC_DIR)/hal_eth_wire.c
ETH_MAC_SRCS := $(HAL_SOC_DIR)/hal_eth_netif.c $(HAL_SOC_DIR)/hal_eth_wire_tcp.c \
  $(HAL_SOC_DIR)/hal_eth_sign_yield.c src/net/netcfg.c \
  $(addprefix $(LWIP_DIR)/core/,init.c def.c inet_chksum.c ip.c mem.c memp.c netif.c \
    pbuf.c tcp.c tcp_in.c tcp_out.c timeouts.c udp.c) \
  $(addprefix $(LWIP_DIR)/core/ipv4/,acd.c dhcp.c etharp.c icmp.c ip4.c ip4_addr.c ip4_frag.c) \
  $(LWIP_DIR)/netif/ethernet.c
ETH_MAC_INC  := -I$(LWIP_DIR)/include -I$(HAL_SOC_DIR) -I$(HAL_SOC_DIR)/lwip-port
ETH_MAC_DEFS := -DTFW_CM_ETH_MAC=1
else
ETH_MAC_SRCS :=
ETH_MAC_DEFS :=
endif

# hal_eeprom.c is always present, but a SoC may have its own (nrf54l: the window is RRAM, which a plain store does not program).

# The HAL modules that are chosen rather than always present.
HAL_OPTIONAL := $(foreach d,$(HAL_DIR) $(HAL_SOC_DIR),$(d)/hal_tone.c $(d)/hal_pwm.c $(d)/hal_pulse.c \
                  $(d)/hal_bus.c $(d)/hal_uart.c $(d)/hal_uart_soft.c $(d)/hsm_port.c \
                  $(d)/hsm_intake.c $(d)/hal_eeprom.c $(d)/hal_eth_netif.c \
                  $(d)/hal_eth_wire.c $(d)/hal_eth_wire_tcp.c \
                  $(d)/hal_eth_sign_yield.c) $(ARCH_HAL_OPTIONAL) \
                $(SOC_EEPROM_SRCS_$(SOC))

# Provisioning commands are a JIG image, never a production one.
PROVISION ?= 0
# A tier with no provisioning backend says so here (PROV_BACKEND := none) beside the $(error) that refuses its PROVISION=1; provision-backend prints it, so prov-absent-check plans no jig image the build would refuse.
PROV_BACKEND :=
ifeq ($(PROVISION),1)
PROV_SRCS   = src/caps/provision.c $(or $(ARCH_PROV_SRCS_$(CRYPTO)),src/caps/provision_atecc.c)
PROV_DEFS  := -DTFW_FEATURE_PROVISION=1
else
PROV_SRCS  :=
PROV_DEFS  :=
endif

# The tier default is the BOARD'S, when the board has an opinion.
CRYPTO ?= soft

# A tier a board's profile refuses (BOARD_REFUSED_TIERS) is not built, not even on request: the ATmega328P is supported only with a secure element, so CRYPTO=soft there, or CRYPTO=auto with the soft root linked beside the element, is an image nobody supports.
ifneq ($(filter $(CRYPTO),$(BOARD_REFUSED_TIERS)),)
$(error CRYPTO=$(CRYPTO) is refused on $(BOARD) -- the board is supported only with its $(BOARD_TIERS) tier (owner ruling 2026-10-17, m522))
endif

# MEASURE_SOFT_CSR=1 (the soft tier, below) measures the one soft AVR row left, the ATmega2560's.
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

# The soft root's identity, linked by the soft and auto tiers: `p256` is the ES256 DeviceID, derived from the UDS (src/crypto/deviceid_p256.c) and signed with the vendored micro-ecc; `hmac` is the HMAC signer the soft root had before, kept for a board whose profile says the P-256 code does not fit (the ATmega328P, and the ATmega32U4 until its fit is measured).
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
# MEASURE_SOFT_CSR=1 is a measurement, never a row: it links the alias builder and ATTEST_CSR into a soft image only to read what they cost on the ATmega2560's budget (MEASURE_SOFT_CSR_BOARD above).
ifeq ($(MEASURE_SOFT_CSR),1)
ALIAS_SRCS  := src/caps/alias.c src/caps/tmpl.c src/caps/uccs.c src/caps/csr.c
ALIAS_DEFS  := -DTFW_FEATURE_ALIAS_CERT=1 -DTFW_FEATURE_ATTEST_CSR=1
endif
# No operator-signature session on this tier: see TFW_FEATURE_AUTH_SESSION in config.h and auth_session_none.c.
AUTH_SRCS   := src/core/auth_session_none.c
AUTH_DEFS   :=
else ifeq ($(CRYPTO),none)
# The unattested tier (src/crypto/none/provider.c).
CRYPTO_SRCS := src/crypto/none/provider.c
ALIAS_SRCS  :=
ALIAS_DEFS  :=
AUTH_SRCS   := src/core/auth_session_none.c
AUTH_DEFS   :=
else ifneq ($(filter $(CRYPTO),atecc608a auto),)
CRYPTO_SRCS := src/crypto/sha256.c src/crypto/atecc608a/atecc.c src/crypto/atecc608a/provider.c
# The Mega's runtime tier: everything the element image links, and the soft provider beside it, each renamed per object by CRYPTO_RENAME below; src/crypto/auto/provider.c chooses one at init, the element when one answers, else the soft root.
ifeq ($(CRYPTO),auto)
CRYPTO_SRCS += src/crypto/soft/provider.c src/crypto/soft/uds_flash.c src/crypto/auto/provider.c \
               $(SOFT_ID_SRCS)
endif
# The alias certificate rides with the tier that can sign one, and only on a board with room for it.
ALIAS_OFF   := $(call feature_off,ALIAS_CERT)
ifeq ($(ALIAS_OFF),1)
ALIAS_SRCS  :=
ALIAS_DEFS  :=
else
ALIAS_SRCS  := src/caps/alias.c src/caps/tmpl.c src/caps/uccs.c
ALIAS_DEFS  := -DTFW_FEATURE_ALIAS_CERT=1
# ATTEST_CSR reads its fixed headers out of the alias template, so it is only ever selected beside it; a profile can still say no to it alone.
ifneq ($(call feature_off,ATTEST_CSR),1)
ALIAS_SRCS  += src/caps/csr.c
ALIAS_DEFS  += -DTFW_FEATURE_ATTEST_CSR=1
endif
endif
# The operator-signature auth session.
AUTH_OFF    := $(call feature_off,AUTH_SESSION)
ifeq ($(AUTH_OFF),1)
AUTH_SRCS   := src/core/auth_session_none.c
AUTH_DEFS   :=
else
AUTH_SRCS   := src/core/auth_session.c $(if $(filter auto,$(CRYPTO)),src/core/auth_session_auto.c)
AUTH_DEFS   := -DTFW_FEATURE_AUTH_SESSION=1
endif
else ifeq ($(CRYPTO),optiga)
# The OPTIGA Trust M tier (row optiga-trust-m, docs/multi-arch/adr-optiga-trust-m-onboarding.md): the I2C transport, the read-only command layer and the provider over them, with the shared software SHA-256.
CRYPTO_SRCS := src/crypto/sha256.c src/crypto/optiga/optiga_i2c.c src/crypto/optiga/optiga_cmd.c \
               src/crypto/optiga/optiga_crypto.c
# No alias certificate: its builder names the element's public key, which this provider cannot read (pubkey is TFW_ERR_UNSUPPORTED; the key is in the factory certificate, and X.509 is go-xkms's).
ALIAS_SRCS  :=
ALIAS_DEFS  :=
ifeq ($(call feature_off,AUTH_SESSION),1)
AUTH_SRCS   := src/core/auth_session_none.c
AUTH_DEFS   :=
else
AUTH_SRCS   := src/core/auth_session.c
AUTH_DEFS   := -DTFW_FEATURE_AUTH_SESSION=1
endif
# No provisioning image: every PROV_* backend writes the element, which the ADR's onboarding rule 6 forbids until question 4 is decided.
ifeq ($(call feature_off,I2C),1)
$(error CRYPTO=optiga needs the I2C bus -- $(BOARD) has it off (a board that turns it on per row, as mps2-an505 does, takes I2C=1))
endif
PROV_BACKEND := none
ifeq ($(PROVISION),1)
$(error PROVISION=1 has no backend on CRYPTO=optiga -- provisioning writes the element, which the OPTIGA ADR forbids until the owner rules)
endif
else ifneq ($(filter $(CRYPTO),$(ARCH_CRYPTO_TIERS)),)
# A tier the architecture provides, because its provider drives that architecture's own key hardware, the ESP32's HMAC peripheral, say.
CRYPTO_SRCS := $(ARCH_CRYPTO_SRCS_$(CRYPTO))
ALIAS_SRCS  :=
ALIAS_DEFS  :=
ifneq ($(filter AUTH_SESSION,$(BOARD_FEATURES_ON)),)
AUTH_SRCS   := src/core/auth_session.c
AUTH_DEFS   := -DTFW_FEATURE_AUTH_SESSION=1
else
AUTH_SRCS   := src/core/auth_session_none.c
AUTH_DEFS   :=
endif
else
$(error unknown CRYPTO tier "$(CRYPTO)" -- expected none, soft, atecc608a, auto or optiga$(if $(ARCH_CRYPTO_TIERS), or $(ARCH_CRYPTO_TIERS) on $(ARCH)))
endif

# The SUIT manifest reader and activation gate, by file selection: on the element tier, whose stored image key verifies the manifest's ES256, and only on a board whose profile.mk opts in with OTA_MANIFEST := 1, the ATmega2560 today.
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

# The application-side OTA stager, by file selection.
OTA_STAGE ?= $(if $(ARCH_OTA_SRCS),1,$(if $(OTA_STORE),$(ETH),0))
ifneq ($(and $(filter 1,$(OTA_STAGE)),$(ARCH_OTA_SRCS)),)
STAGE_SRCS := $(ARCH_OTA_SRCS) src/core/ota_auth.c boot/boot_image.c
# That file also answers tfw_update_slots, the update path an application's ota_slots is checked against (src/core/update_request.h); the architecture links update_request_arch.c (ARCH_UPDATE_REQUEST := arch), which leaves it out.
STAGE_DEFS := -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_INFO=1 -DTFW_FEATURE_OTA_ACTIVATE=1 \
              -DTFW_FEATURE_OTA_CTL=1 -DTFW_ARCH_OTA=1
else ifeq ($(OTA_STAGE),1)
ifeq ($(OTA_STORE),)
$(error OTA_STAGE=1 but $(BOARD_DIR)/profile.mk names no OTA_STORE -- this board has no external store)
endif
ifeq ($(wildcard src/store/$(OTA_STORE).c),)
$(error unknown OTA_STORE "$(OTA_STORE)" -- expected one of: $(basename $(notdir $(wildcard src/store/*.c))))
endif
STAGE_SRCS := src/core/ota_stage.c src/store/$(OTA_STORE).c src/crypto/sha256.c
# A store on a bus needs the bus; the memory store (OTA_STORE := mem, the MPS2 boards) has none, and its architecture no hal_bus.c to link.
ifneq ($(OTA_STORE),mem)
STAGE_SRCS += $(HAL_DIR)/hal_bus.c
endif
# OTA_BEGIN backs the running image up to PREV where the fallback is the store and tfw-boot restores from it; the 2560 falls back to its other slot, as an MPS2 board built with BOOT_AB=1 does.
ifneq ($(filter 1,$(BOOT_AB) $(OTA_NO_PREV)),)
STAGE_SRCS += src/core/ota_prev_none.c
else
STAGE_SRCS += src/core/ota_prev.c $(EEPROM_HAL_SRCS)
endif
# OTA_ACTIVATE is every stager's: OTA_END does not write the MARK, so a single-slot board without it could never install what it staged.
STAGE_SRCS += src/core/ota_activate.c src/core/ota_auth.c
# Its manifest check where the board links the SUIT reader; the stub where it does not, which leaves the image to tfw-boot's signature.
STAGE_SRCS += $(if $(MANIFEST_SRCS),src/core/ota_manifest.c boot/boot_image.c,src/core/ota_manifest_none.c)
STAGE_DEFS := -DTFW_FEATURE_OTA_STAGE=1 -DTFW_FEATURE_OTA_ACTIVATE=1
else
STAGE_SRCS := src/core/ota_stage_none.c
STAGE_DEFS :=
endif
# The output path says so only where the choice is not ETH's default, so every image that existed before keeps its path; an ARCH_OTA_SRCS image always stages.
STAGE_SUFFIX := $(if $(ARCH_OTA_SRCS),,$(if $(filter 1,$(OTA_STAGE)),$(if $(filter 1,$(ETH)),,-stage),$(if $(and $(filter 1,$(ETH)),$(OTA_STORE)),-nostage,)))
# The soft root's identity and micro-ecc's optimisation level (mk/boot.mk UECC_OPT) change a soft or auto image's objects but not its tier.
ID_SUFFIX := $(if $(filter $(SOFT_ID_SRCS),$(CRYPTO_SRCS)),$(if $(filter-out $(SOFT_ID_DEFAULT),$(SOFT_ID)),-$(SOFT_ID)))$(if $(and $(filter src/third_party/micro-ecc/uECC.c,$(CRYPTO_SRCS)),$(filter-out 0,$(UECC_OPT))),-uecc$(UECC_OPT))

# Likewise -ab, only on a board where two slots are a choice (BOOT_AB_OPTIONAL in profile.mk, the MPS2 boards); the 2560 always has two and keeps its paths.
AB_SUFFIX := $(if $(BOOT_AB_OPTIONAL),$(if $(filter 1,$(BOOT_AB)),-ab))
# And a bus a board turns on only for a named row (BOARD_FEATURES_ON in profile.mk: the MPS2 boards' soft:I2C=1 and soft:SPI=1), as -i2c and -spi.
ROW_FEATURE_SUFFIX := $(if $(strip $(BOARD_FEATURES_ON)),-$(subst $() ,-,$(shell printf '%s' \
                      '$(strip $(BOARD_FEATURES_ON))' | tr A-Z a-z)))

# Every make variable that changes the image's objects names the directory, and only where it differs from its default, so every default image keeps its path.
FW_DIR     := $(BUILD_DIR)/$(BOARD)/$(CRYPTO)$(if $(filter 1,$(PROVISION)),-prov,)$(if $(filter 1,$(ETH)),-eth-$(ETH_CHIP),)$(if $(filter 1,$(DHCP)),-dhcp,)$(if $(filter 1,$(ETH_MAC)),-ethmac,)$(STAGE_SUFFIX)$(AB_SUFFIX)$(ROW_FEATURE_SUFFIX)$(APP_SUFFIX)$(if $(filter 1,$(MEASURE_SOFT_CSR)),-csr,)$(ID_SUFFIX)
TARGET     := $(FW_DIR)/trusted-firmware

# The row, for the bench images that compile with FW_CFLAGS in a directory of their own (mk/bench.mk, mk/platform-bake.mk, mk/wiznet-probe.mk): FW_CFLAGS carries every row flag's defines, so a row flag reaches their objects too.
FW_ROW        := $(notdir $(FW_DIR))
FW_ROW_FLAGS  := CRYPTO PROVISION ETH ETH_CHIP DHCP ETH_MAC OTA_STAGE MEASURE_SOFT_CSR SOFT_ID UECC_OPT APP_DIR SPI
FW_ROW_SUFFIX := $(if $(filter command environment override,$(foreach v,$(FW_ROW_FLAGS),$(firstword $(origin $(v))))),-$(FW_ROW))

# The toolchain binaries and the flags that name a machine, an optimisation policy and a link placement are the architecture's (mk/arch/$(ARCH).mk).
FW_INCLUDES := -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Iboot -I$(HAL_DIR) -I$(BOARD_DIR)

# The reproducibility flags are not optional.
FW_CFLAGS := -std=c11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) \
             -Wall -Wextra -Werror -Wconversion -Wshadow \
             -Wpointer-arith -Wcast-qual -Wstrict-prototypes \
             -ffile-prefix-map=$(CURDIR)=. \
             $(FW_INCLUDES) \
             -DTFW_BOARD_HEADER='"board_profile.h"' $(PROV_DEFS) $(ALIAS_DEFS) $(AUTH_DEFS) \
             $(LINK_DEFS) $(STAGE_DEFS) $(APP_DEFS) $(BOARD_DEFS) $(ETH_MAC_DEFS)

# The CDI handoff block's placement is the architecture's: see ARCH_FW_PLACEMENT_LDFLAGS, which is empty on a board with no RAM_END.
FW_LDFLAGS := $(strip $(ARCH_LDFLAGS) $(ARCH_FW_PLACEMENT_LDFLAGS))

# The identity sources follow the same RAM_END gate, by file selection rather than a conditional in main.c, main.c is not one of the four places a target conditional is permitted.
ifneq ($(RAM_END)$(ARCH_HANDOFF),)
IDENTITY_SRCS := src/core/handoff.c src/core/platform_id.c \
                 $(if $(filter src/crypto/soft/id_p256.c,$(CRYPTO_SRCS)),src/core/handoff_seed.c,src/core/handoff_seed_none.c) \
                 src/core/update_request.c \
                 boot/boot_handoff.c boot/boot_platform.c boot/boot_request.c
else
IDENTITY_SRCS := src/core/handoff_none.c src/core/platform_id_none.c \
                 src/core/update_request_none.c
endif
# A bootloader that hands off but opens no update session (ESP-IDF's, until its OTA is wired to the wire) takes no request either, so RESET(1) is refused rather than quietly behaving as a soft reset.
ifeq ($(BOOT_AB),1)
IDENTITY_SRCS += src/core/ota_trial.c src/core/ota_trial_block.c
else ifneq ($(ARCH_OTA_SRCS),)
IDENTITY_SRCS += src/core/ota_trial.c
else
IDENTITY_SRCS += src/core/ota_trial_none.c
endif
# OTA_INFO, OTA_CONFIRM and OTA_ACTIVATE, by file selection: only where tfw-boot runs the trial (BOOT_AB) and the image stages into the store, the block ACTIVATE verifies.
ifeq ($(BOOT_AB)$(OTA_STAGE),11)
OTACTL_SRCS := src/core/ota_ctl.c boot/boot_trial.c boot/boot_image.c \
               $(if $(filter atecc608a,$(CRYPTO)),src/core/ota_floor_atecc.c,$(if $(filter auto,$(CRYPTO)),src/core/ota_floor_atecc.c src/core/ota_floor_eeprom.c src/core/ota_floor_auto.c,src/core/ota_floor_eeprom.c))
FW_CFLAGS   += -DTFW_FEATURE_OTA_CTL=1
else
OTACTL_SRCS :=
endif
# ARCH_UPDATE_REQUEST := arch is the same refusal with tfw_update_slots left to the architecture's OTA file (update_request_arch.c), by file, not by a conditional in the core file.
ifneq ($(filter none arch,$(ARCH_UPDATE_REQUEST)),)
IDENTITY_SRCS := $(filter-out src/core/update_request.c boot/boot_request.c,$(IDENTITY_SRCS)) \
                 src/core/update_request_$(ARCH_UPDATE_REQUEST).c
endif
# One update path per image on the A/B part, by file selection.
ifeq ($(BOOT_AB)$(OTA_STAGE),11)
ifneq ($(filter src/core/update_request.c,$(IDENTITY_SRCS)),)
IDENTITY_SRCS := $(filter-out src/core/update_request.c,$(IDENTITY_SRCS)) \
                 src/core/update_request_store.c
endif
endif
# The tamper-response engine, by file selection: a board whose profile.mk says HSM := 1 links the engine, its SUIT reader, the channel's commands (HSM_* in commands.def) and the loop main.c polls, behind the SoC's hsm_port.c (in HAL_SOC_DIR, HAL_OPTIONAL so it is linked here and nowhere else).
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

# Sorted, not wildcard-ordered.
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
  $(BOARD_DIR)/board.c $(UART_SRCS) $(ARCH_FW_SRCS) $(ETH_MAC_SRCS) \
  $(filter-out $(HAL_OPTIONAL),$(wildcard $(HAL_DIR)/*.c $(if $(HAL_SOC_DIR),$(HAL_SOC_DIR)/*.c))))

FW_OBJS := $(patsubst %.c,$(FW_DIR)/%.o,$(FW_SRCS))

# LwIP is third-party code under its own warnings; the glue is ours and keeps the project's.
ifeq ($(ETH_MAC),1)
$(FW_DIR)/$(HAL_SOC_DIR)/hal_eth_netif.o $(FW_DIR)/$(HAL_SOC_DIR)/hal_eth_wire_tcp.o: FW_CFLAGS += $(ETH_MAC_INC)
$(FW_DIR)/$(LWIP_DIR)/%.o: FW_CFLAGS += $(ETH_MAC_INC) -w
endif

# CRYPTO=auto links two providers that define the same eleven tfw_crypto_* names.
CRYPTO_RENAME = $(foreach f,init tier algorithms random sha256 hmac sign pubkey counter \
                  verify_operator verify_image,-Dtfw_crypto_$(f)=tfw_crypto_$(1)_$(f))
FLOOR_RENAME = -Dtfw_otactl_floor=tfw_otactl_floor_$(1)
AUTH_RENAME = -Dtfw_auth_session_init=tfw_auth_element_session_init -Dtfw_auth_gate=tfw_auth_element_gate
# AUTO_RENAMES takes an object directory, so the firmware's objects and the benchmark's (mk/bench.mk, which compiles the same sources) get one list.
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
# The soft root's P-256 objects (SOFT_ID=p256): the vendored micro-ecc is compiled as the boot compiles it (mk/boot.mk UECC_DEFS), without our warning set, an upstream's warnings are not ours to fix, and deviceid_p256.c finds uECC.h.
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
# An architecture tier whose provider wraps the soft one links it renamed, as auto does.
ifneq ($(filter $(CRYPTO),$(ARCH_CRYPTO_WRAPS_SOFT)),)
$(FW_DIR)/src/crypto/soft/provider.o: FW_CFLAGS += $(call CRYPTO_RENAME,soft)
# The soft provider refuses verify_operator, so the wrapper falls back to src/crypto/soft/verify_p256.c's micro-ecc verify, linked renamed.
$(FW_DIR)/src/crypto/soft/verify_p256.o: FW_CFLAGS += -Isrc/third_party/micro-ecc \
                                         -Dtfw_crypto_verify_operator=tfw_crypto_soft_verify_p256
endif
# The STM32H5 provider turns the HASH, RNG and PKA clocks on through the SoC's RCC, named in the SoC's soc_regs.h, which it includes by its own name.
$(FW_DIR)/src/crypto/stm32h5/provider.o: FW_CFLAGS += -I$(HAL_DIR)/stm32h5
# The RP2350's flash EEPROM includes tfw-boot's flash helper by its own name, as the boot's files do: a path through hal/reads to lint-arch-leak as ESP-IDF.
$(FW_DIR)/src/hal/cortex-m/rp2350/hal_eeprom.o: FW_CFLAGS += -Iboot/hal/cortex-m/rp2350

$(FW_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(FW_CFLAGS) -MMD -MP -frandom-seed=$(notdir $<) -c -o $@ $<

# The headers each object read, written by -MMD beside it.
-include $(FW_OBJS:.o=.d)

# An application's sources (mk/app.mk), compiled with the firmware's machine, optimisation and warning flags and its reproducibility flags, but against include/ and the board only: src/ is the firmware's own, and an application that reached into it would break on the next refactor of a file it never named.
APP_FW_OBJS := $(patsubst %.c,$(FW_DIR)/app/%.o,$(APP_SRCS))
APP_FW_CFLAGS := -std=c11 $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) $(APP_WARN_CFLAGS) \
                 -ffile-prefix-map=$(CURDIR)=. $(APP_PREFIX_MAP) \
                 -Iinclude -I$(HAL_DIR) -I$(BOARD_DIR) \
                 -DTFW_BOARD_HEADER='"board_profile.h"' $(PROV_DEFS) $(ALIAS_DEFS) $(AUTH_DEFS) \
                 $(APP_CFLAGS)

# mk/app.mk's selection stamp: the ELF depends on it, so changing APP_COMMANDS
# relinks. Empty with no application.
APP_FW_CONFIG :=

ifneq ($(APP_SRCS),)
APP_FW_CONFIG := $(FW_DIR)/app/app-config.txt
$(APP_FW_CONFIG): FORCE
	$(APP_CONFIG_STAMP)

$(APP_FW_OBJS): $(FW_DIR)/app/%.o: $(APP_ABS)/%.c
	@mkdir -p $(dir $@)
	@$(ARCH_CC) $(APP_FW_CFLAGS) -MMD -MP -frandom-seed=$(notdir $<) -c -o $@ $<

-include $(APP_FW_OBJS:.o=.d)
endif

$(TARGET).elf: $(FW_OBJS) $(APP_FW_OBJS) $(APP_FW_CONFIG)
	@$(ARCH_CC) $(FW_LDFLAGS) -o $@ $(FW_OBJS) $(APP_FW_OBJS)

# objcopy -R .eeprom only: mk/handoff.ld makes `.handoff` NOLOAD, so it carries no bytes and no derived image includes it.
$(TARGET).hex: $(TARGET).elf
	@$(ARCH_OBJCOPY) -O ihex -R .eeprom $< $@

## firmware-matrix: build EVERY board and crypto tier, as CI does
BOARD_TIERS     ?= soft atecc608a
# Rows a board builds besides its tiers at their defaults: "tier:VAR=value", more than one setting comma-separated ("soft:ETH=1,OTA_STAGE=0"), each a named configuration with the same size-check as the default rows.
BOARD_ROWS      ?=
# A board whose profile.mk sets IMAGE_TARGET is built by that target and not by `make firmware`, an ESP-IDF board is built by ESP-IDF's own toolchain, which the CI image does not carry.
OTHER_IMAGE_BOARDS := $(notdir $(patsubst %/profile.mk,%,$(shell grep -l '^IMAGE_TARGET[[:space:]]*:\?=' boards/*/profile.mk)))
FIRMWARE_BOARDS := $(filter-out $(OTHER_IMAGE_BOARDS),$(notdir $(wildcard boards/*)))

## board-tiers: print the crypto tiers BOARD supports (used by firmware-matrix)
board-tiers:
	@echo $(BOARD_TIERS)

## feature-defs: print the -D feature flags the BOARD/CRYPTO image compiles with (used by board-sweep)
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
ifeq ($(TZ),1)
firmware: tz-firmware
size-check: tz-size-check
else
firmware: $(TARGET).hex size-check
	@echo "built $(TARGET).hex"

## size-check: fail the build when an image exceeds its board's budget
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
endif

## reproducible-check: build twice from clean copies of HEAD and diff the image
reproducible-check:
	@python3 scripts/repro_build.py --make "$(MAKE)" --image $(TARGET).hex \
	   --dir /tmp/agrepro-a$(APP_SUFFIX) --dir /tmp/agrepro-b$(APP_SUFFIX) \
	   $(if $(strip $(APP_DIR)),--app-dir "$(APP_DIR)") -- \
	   BOARD=$(BOARD) CRYPTO=$(CRYPTO) $(TARGET).hex

## flash: write the firmware to a connected board
flash: $(TARGET).hex
	$(ARCH_FLASH)

.PHONY: firmware size-check reproducible-check flash
