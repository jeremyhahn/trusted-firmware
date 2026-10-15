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
ESP_OUT        = $(ESP_ROOT)/$(BOARD)-$(ESP_TARGET)-$(CRYPTO)
# Generated on first build, never committed, shared by every ESP32 image this
# build directory holds. The QEMU test burns its digest into emulated eFuses.
ESP_SIGN_KEY  := $(ESP_ROOT)/keys/secure_boot_signing_key.pem
ESP_PROJECT   := mk/arch/esp32
ESP32_BOARDS  := $(notdir $(patsubst %/profile.mk,%,$(shell grep -l '^ARCH[[:space:]]*:\?=[[:space:]]*esp32[[:space:]]*$$' boards/*/profile.mk)))

esp_empty :=
esp_space := $(esp_empty) $(esp_empty)
esp_list   = $(subst $(esp_space),;,$(strip $(1)))

## esp32-build: build an ESP32 board's ESP-IDF image (BOARD=esp32-qemu|esp32p4-poe, ESP_TARGET for QEMU)
esp32-build:
	@if [ "$(ARCH)" != esp32 ]; then \
	   echo "esp32-build: $(BOARD) is ARCH $(ARCH); the ESP32 boards are: $(ESP32_BOARDS)"; exit 1; fi
	@if [ -z "$(filter $(ESP_TARGET),$(ESP_TARGETS))" ]; then \
	   echo "esp32-build: $(BOARD) runs on $(ESP_TARGETS), not ESP_TARGET=$(ESP_TARGET)"; exit 1; fi
	@if [ "$(PROVISION)" = 1 ]; then \
	   echo "esp32-build: no provisioning image on ESP32 yet -- src/caps/provision.c is the ATECC608A's"; exit 1; fi
	@command -v idf.py >/dev/null || { \
	   echo "esp32-build: idf.py not found. Export ESP-IDF $(ESP_IDF_VERSION) first:"; \
	   echo "  . \$$IDF_PATH/export.sh    (see docs/multi-arch/adr-esp-idf-integration.md)"; exit 1; }
	@v=$$(idf.py --version 2>/dev/null); case "$$v" in *"$(ESP_IDF_VERSION)"*) ;; \
	   *) echo "esp32-build: ESP-IDF is '$$v'; this tree is pinned to $(ESP_IDF_VERSION)"; exit 1;; esac
	@mkdir -p $(ESP_OUT) $(dir $(ESP_SIGN_KEY))
	@[ -f $(ESP_SIGN_KEY) ] || espsecure.py generate_signing_key --version 2 --scheme rsa3072 \
	   $(ESP_SIGN_KEY) > /dev/null
	@printf 'CONFIG_SECURE_BOOT_SIGNING_KEY="%s"\nCONFIG_PARTITION_TABLE_CUSTOM_FILENAME="%s"\n' \
	   "$(abspath $(ESP_SIGN_KEY))" "$(abspath $(BOARD_DIR)/partitions.csv)" > $(ESP_OUT)/sdkconfig.build
	@# Regenerated from the defaults every time: a stale sdkconfig would keep
	@# a setting the defaults no longer say.
	@rm -f $(ESP_OUT)/sdkconfig
	@idf.py -C $(ESP_PROJECT) -B $(abspath $(ESP_OUT))/idf -DIDF_TARGET=$(ESP_TARGET) \
	   -DSDKCONFIG=$(abspath $(ESP_OUT))/sdkconfig \
	   -DSDKCONFIG_DEFAULTS="$(abspath $(ESP_PROJECT)/sdkconfig.defaults);$(abspath $(BOARD_DIR)/sdkconfig.defaults);$(abspath $(ESP_OUT))/sdkconfig.build" \
	   -DTFW_ROOT=$(CURDIR) -DTFW_BOARD_DIR=$(abspath $(BOARD_DIR)) \
	   -DTFW_SRCS="$(call esp_list,$(abspath $(FW_SRCS) $(ARCH_EXTRA_SRCS)))" \
	   -DTFW_DEFS="$(call esp_list,$(PROV_DEFS) $(ALIAS_DEFS) $(AUTH_DEFS))" \
	   build > $(ESP_OUT)/build.log 2>&1 || { tail -n 40 $(ESP_OUT)/build.log; \
	   echo "esp32-build: FAILED, full log in $(ESP_OUT)/build.log"; exit 1; }
	@echo "built $(ESP_OUT)/idf/trusted-firmware.bin ($(BOARD) on $(ESP_TARGET), $(CRYPTO) tier)"

## esp32-qemu-test: Espressif's QEMU: secure boot v2, CDI and DeviceID across reboots, a tampered image refused, the wire over Ethernet
# Both machines of boards/esp32-qemu. The esp32c3 run carries the identity;
# the esp32 run has no HMAC or DS and checks secure boot and the wire. Keys and
# eFuse images are generated into the build directory by the test itself.
esp32-qemu-test:
	@v=$$(qemu-system-riscv32 --version 2>/dev/null | head -n 1); case "$$v" in *"$(ESP_QEMU_VERSION)"*) ;; \
	   *) echo "esp32-qemu-test: QEMU is '$$v'; this tree is pinned to $(ESP_QEMU_VERSION)"; exit 1;; esac
	@$(MAKE) --no-print-directory esp32-build BOARD=esp32-qemu ESP_TARGET=esp32c3
	@$(MAKE) --no-print-directory esp32-build BOARD=esp32-qemu ESP_TARGET=esp32
	@mkdir -p $(ESP_ROOT)
	@cd test/integration/esp32 && $(GO) build -o $(abspath $(ESP_ROOT))/wirecheck ./wirecheck
	@python test/integration/esp32/qemu_test.py $(ESP_ROOT)/esp32-qemu-esp32c3-hmac-ds esp32c3 $(ESP_ROOT)/wirecheck
	@python test/integration/esp32/qemu_test.py $(ESP_ROOT)/esp32-qemu-esp32-none esp32 $(ESP_ROOT)/wirecheck
	@echo "esp32-qemu-test: green"

.PHONY: esp32-build esp32-qemu-test
