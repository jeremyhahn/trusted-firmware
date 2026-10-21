# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The ESP32 port's targets. Included by the Makefile for every invocation
# (mk/arch/*-targets.mk), unlike mk/arch/esp32.mk, which only an ESP32 board
# reads -- so `make esp32-qemu-test` works without naming a board.
#
# NEITHER IS IN `make ci`. The CI image carries no ESP-IDF, and a gate that
# cannot run there would be a failure, not a skip; these run where ESP-IDF
# $(ESP_IDF_VERSION) and Espressif's QEMU are installed and exported. The
# install, outside the clone, is in docs/multi-arch/adr-esp-idf-integration.md.

ESP_ROOT      := $(BUILD_DIR)/esp32
ESP_OUT        = $(ESP_ROOT)/$(BOARD)-$(ESP_TARGET)-$(CRYPTO)$(if $(filter 1,$(PROVISION)),-prov)
# Generated on first build, never committed, shared by every ESP32 image this
# build directory holds. The QEMU test burns its digest into emulated eFuses.
ESP_SIGN_KEY  := $(ESP_ROOT)/keys/secure_boot_signing_key.pem
# Layer 0's ES256 image key (docs/multi-arch/esp32-p4-design.md section 2.2),
# the same kind and the same rule: generated, never committed. Its public half
# is compiled into the bootloader and its pin baked into tfw_plat.
ESP_IMAGE_KEY := $(ESP_ROOT)/keys/image_key_p256.pem
ESP_L0_SEAL   := boot/hal/esp32/l0_seal.py
ESP_L0_GEN     = $(ESP_OUT)/tfw_l0
# Trailer v1's image_version, the anti-rollback number the gate holds against
# the eFuse floor. 0 is refused.
ESP_IMAGE_VERSION ?= 1
# Non-empty where boards/<board>/profile.mk lists this target for ESP-IDF's
# anti-rollback. Its eFuse floor counts in set bits: 16 on the C3, so the
# version tops out there.
ESP_ANTI_ROLLBACK   = $(filter $(ESP_TARGET),$(ESP_ANTI_ROLLBACK_TARGETS))
ESP_SEC_VER_MAX    := 16
ESP_PROJECT   := mk/arch/esp32
ESP32_BOARDS  := $(notdir $(patsubst %/profile.mk,%,$(shell grep -l '^ARCH[[:space:]]*:\?=[[:space:]]*esp32[[:space:]]*$$' boards/*/profile.mk)))

esp_empty :=
esp_space := $(esp_empty) $(esp_empty)
esp_list   = $(subst $(esp_space),;,$(strip $(1)))
# ESP_SDKCONFIG_EXTRA is a space-separated list of overlays, each its own
# SDKCONFIG_DEFAULTS entry in the order given, so a later file wins.
ESP_SDKCONFIG_MISSING = $(strip $(foreach f,$(ESP_SDKCONFIG_EXTRA),$(if $(wildcard $(f)),,$(f))))

## esp32-build: build an ESP32 board's ESP-IDF image (BOARD=esp32-qemu|esp32p4-poe, ESP_TARGET for QEMU, ESP_SDKCONFIG_EXTRA="<file> ..." for a deployment's overlays in order, e.g. HIL-0 and a static address, ESP_IMAGE_VERSION, SERIAL for tfw_plat)
# Writes $(ESP_OUT)/trusted-firmware.bin, the app sealed with trailer v1 and
# the SUIT envelope the board's suit-emit builds (mk/suit.mk), and re-signed --
# the image to flash; idf/trusted-firmware.bin has no trailer and
# layer 0 refuses it -- and $(ESP_OUT)/tfw_plat.bin, the platform block with the
# image key's pin, for the tfw_plat partition. SERIAL defaults to
# platform-bake's neutral test value, no device's.
esp32-build: $(TFWPROV_BIN) $(SUIT_EMIT_BIN)
	@$(if $(ESP_SDKCONFIG_MISSING),$(error esp32-build: ESP_SDKCONFIG_EXTRA names no such file: $(ESP_SDKCONFIG_MISSING)))
	@if [ "$(ARCH)" != esp32 ]; then \
	   echo "esp32-build: $(BOARD) is ARCH $(ARCH); the ESP32 boards are: $(ESP32_BOARDS)"; exit 1; fi
	@if [ -z "$(filter $(ESP_TARGET),$(ESP_TARGETS))" ]; then \
	   echo "esp32-build: $(BOARD) runs on $(ESP_TARGETS), not ESP_TARGET=$(ESP_TARGET)"; exit 1; fi
	@case "$(ESP_L0_VERIFY)" in p256-rom|p256-soft) ;; \
	   *) echo "esp32-build: $(BOARD) sets ESP_L0_VERIFY '$(ESP_L0_VERIFY)', not p256-rom or p256-soft"; exit 1;; esac
	@case "$(ESP_PULSE)" in poll|rmt) ;; \
	   *) echo "esp32-build: $(BOARD) sets ESP_PULSE '$(ESP_PULSE)', not poll or rmt"; exit 1;; esac
	@if [ "$(ESP_PULSE)" = rmt ] && [ "$(ESP_TARGET)" = esp32 ]; then \
	   echo "esp32-build: ESP_PULSE=rmt needs RX ping-pong, which the esp32 has not; it polls"; exit 1; fi
	@if [ -n "$(ESP_ANTI_ROLLBACK)" ] && [ "$(ESP_IMAGE_VERSION)" -gt $(ESP_SEC_VER_MAX) ]; then \
	   echo "esp32-build: ESP_IMAGE_VERSION $(ESP_IMAGE_VERSION) is past the $(ESP_TARGET)'s $(ESP_SEC_VER_MAX)-bit secure-version eFuse"; exit 1; fi
	@if [ "$(PROVISION)" = 1 ] && [ -z "$(ARCH_PROV_SRCS_$(CRYPTO))" ]; then \
	   echo "esp32-build: no provisioning image for the $(CRYPTO) tier -- only $(ARCH_CRYPTO_TIERS) burns a UDS"; exit 1; fi
	@command -v idf.py >/dev/null || { \
	   echo "esp32-build: idf.py not found. Export ESP-IDF $(ESP_IDF_VERSION) first:"; \
	   echo "  . \$$IDF_PATH/export.sh    (see docs/multi-arch/adr-esp-idf-integration.md)"; exit 1; }
	@v=$$(idf.py --version 2>/dev/null); case "$$v" in *"$(ESP_IDF_VERSION)"*) ;; \
	   *) echo "esp32-build: ESP-IDF is '$$v'; this tree is pinned to $(ESP_IDF_VERSION)"; exit 1;; esac
	@mkdir -p $(ESP_OUT) $(dir $(ESP_SIGN_KEY))
	@[ -f $(ESP_SIGN_KEY) ] || espsecure.py generate_signing_key --version 2 --scheme rsa3072 \
	   $(ESP_SIGN_KEY) > /dev/null
	@[ -f $(ESP_IMAGE_KEY) ] || espsecure.py generate_signing_key --version 2 --scheme ecdsa256 \
	   $(ESP_IMAGE_KEY) > /dev/null
	@# The bootloader component reads these beside the sdkconfig: the board's
	@# directory and layer 0's verify, and the key slot with the image key in it.
	@mkdir -p $(ESP_L0_GEN)
	@printf 'set(TFW_BOARD_DIR "%s")\nset(TFW_L0_VERIFY "%s")\n' "$(abspath $(BOARD_DIR))" \
	   "$(ESP_L0_VERIFY)" > $(ESP_L0_GEN)/tfw_l0.cmake
	@python $(ESP_L0_SEAL) keyslot $(ESP_IMAGE_KEY) $(ESP_L0_GEN)/p256_key_slot.c > $(ESP_L0_GEN)/pin.hex
	@$(TFWPROV_BIN) --bake-eeprom --serial $(or $(SERIAL),$(PLATBAKE_CHECK_SERIAL)) \
	   --pubkey-hash $$(cat $(ESP_L0_GEN)/pin.hex) --out $(ESP_L0_GEN)/tfw_plat.eep > /dev/null
	@$(PLATBAKE_HEX2BIN) -I ihex -O binary $(ESP_L0_GEN)/tfw_plat.eep $(ESP_OUT)/tfw_plat.bin
	@printf 'CONFIG_SECURE_BOOT_SIGNING_KEY="%s"\nCONFIG_PARTITION_TABLE_CUSTOM_FILENAME="%s"\n' \
	   "$(abspath $(ESP_SIGN_KEY))" "$(abspath $(BOARD_DIR)/partitions.csv)" > $(ESP_OUT)/sdkconfig.build
	@# Where the board turns anti-rollback on, the app's secure version is the
	@# trailer's image_version, and layer 0 refuses an image whose two differ.
	@$(if $(ESP_ANTI_ROLLBACK),printf 'CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y\nCONFIG_BOOTLOADER_APP_SECURE_VERSION=%s\n' \
	   "$(ESP_IMAGE_VERSION)" >> $(ESP_OUT)/sdkconfig.build,true)
	@# Regenerated from the defaults every time: a stale sdkconfig would keep
	@# a setting the defaults no longer say.
	@rm -f $(ESP_OUT)/sdkconfig
	@idf.py -C $(ESP_PROJECT) -B $(abspath $(ESP_OUT))/idf -DIDF_TARGET=$(ESP_TARGET) \
	   -DSDKCONFIG=$(abspath $(ESP_OUT))/sdkconfig \
	   -DSDKCONFIG_DEFAULTS="$(abspath $(ESP_PROJECT)/sdkconfig.defaults);$(abspath $(BOARD_DIR)/sdkconfig.defaults);$(if $(ESP_SDKCONFIG_EXTRA),$(call esp_list,$(abspath $(ESP_SDKCONFIG_EXTRA)));)$(abspath $(ESP_OUT))/sdkconfig.build" \
	   -DTFW_ROOT=$(CURDIR) -DTFW_BOARD_DIR=$(abspath $(BOARD_DIR)) \
	   -DTFW_SRCS="$(call esp_list,$(abspath $(FW_SRCS) $(ARCH_EXTRA_SRCS)))" \
	   -DTFW_DEFS="$(call esp_list,$(PROV_DEFS) $(ALIAS_DEFS) $(AUTH_DEFS) $(STAGE_DEFS))" \
	   build > $(ESP_OUT)/build.log 2>&1 || { tail -n 40 $(ESP_OUT)/build.log; \
	   echo "esp32-build: FAILED, full log in $(ESP_OUT)/build.log"; exit 1; }
	@python $(ESP_L0_SEAL) seal $(ESP_OUT)/idf/trusted-firmware.bin $(ESP_OUT)/trusted-firmware.bin \
	   --board-profile $(BOARD_DIR)/board_profile.h --version $(ESP_IMAGE_VERSION) \
	   --image-key $(ESP_IMAGE_KEY) --sign-key $(ESP_SIGN_KEY) --suit-emit $(SUIT_EMIT_BIN)
	@echo "built $(ESP_OUT)/trusted-firmware.bin ($(BOARD) on $(ESP_TARGET), $(CRYPTO) tier) and $(ESP_OUT)/tfw_plat.bin"

## esp32-qemu-test: Espressif's QEMU: secure boot v2 and layer 0's ES256 gate, CDI and DeviceID across reboots, tampered and mis-sealed images and manifests refused, the wire over Ethernet
# Both machines of boards/esp32-qemu. The esp32c3 run (hmac-p256 tier) carries
# the identity: the CDI, and the P-256 DeviceID checked against the literal
# point of a fixed UDS, its ATTEST verified as ES256; the esp32 run has no HMAC
# peripheral (none tier) and checks secure boot, the wire and the ATTEST
# refusal. The esp32c3 run also boots the PROVISION=1 image on a part with no
# UDS: PROV_GENKEY burns one the harness cannot read back, and the next normal
# boot's DeviceID is the point it returned. Keys and eFuse images are generated
# into the build directory by the test itself.
esp32-qemu-test:
	@v=$$(qemu-system-riscv32 --version 2>/dev/null | head -n 1); case "$$v" in *"$(ESP_QEMU_VERSION)"*) ;; \
	   *) echo "esp32-qemu-test: QEMU is '$$v'; this tree is pinned to $(ESP_QEMU_VERSION)"; exit 1;; esac
	@$(MAKE) --no-print-directory esp32-build BOARD=esp32-qemu ESP_TARGET=esp32c3
	@$(MAKE) --no-print-directory esp32-build BOARD=esp32-qemu ESP_TARGET=esp32c3 PROVISION=1
	@$(MAKE) --no-print-directory esp32-build BOARD=esp32-qemu ESP_TARGET=esp32
	@mkdir -p $(ESP_ROOT)
	@cd test/integration/esp32 && $(GO) build$(call go-tags,-tags) -o $(abspath $(ESP_ROOT))/wirecheck ./wirecheck
# The two runs write only under their own build directory and read the keys
# and tools the builds above made, so they run side by side. Each is waited
# on by its pid, and either failing fails the target.
	@python test/integration/esp32/qemu_test.py $(ESP_ROOT)/esp32-qemu-esp32c3-hmac-p256 esp32c3 $(ESP_ROOT)/wirecheck \
	   $(BUILD_DIR)/tools/suit-emit-esp32-qemu $(ESP_ROOT)/esp32-qemu-esp32c3-hmac-p256-prov & c3=$$!; \
	 python test/integration/esp32/qemu_test.py $(ESP_ROOT)/esp32-qemu-esp32-none esp32 $(ESP_ROOT)/wirecheck \
	   $(BUILD_DIR)/tools/suit-emit-esp32-qemu & esp=$$!; \
	 rc=0; wait $$c3 || rc=1; wait $$esp || rc=1; exit $$rc
	@echo "esp32-qemu-test: green"

## esp32-overlay-selftest: ESP_SDKCONFIG_EXTRA's files reach SDKCONFIG_DEFAULTS in order, and a missing one stops the build
# A dry run (make -n) of esp32-build, so it needs no ESP-IDF and runs in CI's
# security stage. The overlays are throwaway files, never this tree's.
esp32-overlay-selftest:
	@t=$$(mktemp -d) && trap 'rm -rf "$$t"' EXIT && touch $$t/a $$t/b && \
	defs='/boards/esp32p4-poe/sdkconfig.defaults;' && \
	build='$(abspath $(ESP_ROOT))/esp32p4-poe-esp32p4-none/sdkconfig.build"' && \
	for c in ":" "$$t/a:$$t/a;" "$$t/a $$t/b:$$t/a;$$t/b;" "$$t/b $$t/a:$$t/b;$$t/a;"; do \
	  out=$$($(MAKE) --no-print-directory -n esp32-build BOARD=esp32p4-poe CRYPTO=none \
	    ESP_SDKCONFIG_EXTRA="$${c%%:*}" 2>&1) || { echo "selftest: esp32-build refused '$${c%%:*}'"; exit 1; }; \
	  case $$out in *"$$defs$${c#*:}$$build"*) ;; \
	    *) echo "selftest: '$${c%%:*}' did not give SDKCONFIG_DEFAULTS ...$$defs$${c#*:}...sdkconfig.build"; exit 1 ;; esac; \
	done; \
	if out=$$($(MAKE) --no-print-directory -n esp32-build BOARD=esp32p4-poe CRYPTO=none \
	    ESP_SDKCONFIG_EXTRA="$$t/a $$t/missing" 2>&1); then \
	  echo "selftest: esp32-build took a missing overlay"; exit 1; fi; \
	case $$out in *"no such file: $$t/missing"*) ;; \
	  *) echo "selftest: esp32-build did not name the missing overlay"; exit 1 ;; esac
	@echo "esp32-overlay-selftest: green"

.PHONY: esp32-build esp32-qemu-test esp32-overlay-selftest
