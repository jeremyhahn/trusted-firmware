# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The Cortex-M port's QEMU target (#161). Included by the Makefile for every
# invocation (mk/arch/*-targets.mk), unlike mk/arch/cortex-m.mk, which only a
# Cortex-M board reads -- so `make integration-test-cortex-m` works without
# naming a board.
#
# NOT IN `make ci`, and not because it cannot run there: the devcontainer
# carries the Arm toolchain and QEMU (docker/packages.txt). It is the CI image
# job's own step, after the two images it boots are built
# (.github/workflows/ci.yml), so a red Cortex-M boot names itself.

# The one row it runs: the board's only tier, with tfw-boot verifying a P-256
# signature in software and halting on a refusal (no OTA, no store). Never
# BOOT_SIG=none: every tfw-boot row verifies (m453).
CM_QEMU_BOARD := mps2-an505
CM_QEMU_TIER  := soft
CM_QEMU_ROW   := BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=0

# The soft root's identity on this row: the ES256 P-256 DeviceID derived from
# the UDS (mk/firmware.mk SOFT_ID), as on the ATmega2560 -- P-256 on every row
# (m522). Named here, not left to the default, so the row cannot fall back to
# the HMAC signer; CAPS reports ecdsa-p256-sha256 and wirecheck verifies ATTEST
# under the point cm_image derives from the UDS it provisions.
CM_QEMU_SOFT_ID := p256

# What CAPS must report, as literals on purpose: the board's id
# (boards/mps2-an505/board_profile.h, provisional) and the soft tier's wire
# value. A number read back from the build would agree with whatever the build
# got wrong.
CM_QEMU_BOARD_ID := 0x40
CM_QEMU_TIER_ID  := 1

# Under the board's integration directory, beside mk/boot.mk's harness, whose
# host micro-ecc object the signer links.
CM_QEMU_DIR := $(BOOT_INT_DIR)/qemu

## integration-test-cortex-m: QEMU mps2-an505: tfw-boot verifies and boots the firmware across a reset; a tampered image and another key's stay silent
integration-test-cortex-m:
	@$(MAKE) --no-print-directory firmware BOARD=$(CM_QEMU_BOARD) CRYPTO=$(CM_QEMU_TIER) SOFT_ID=$(CM_QEMU_SOFT_ID)
	@$(MAKE) --no-print-directory bootloader BOARD=$(CM_QEMU_BOARD) $(CM_QEMU_ROW)
	@$(MAKE) --no-print-directory cortex-m-qemu-run BOARD=$(CM_QEMU_BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) $(CM_QEMU_ROW)
	@echo "integration-test-cortex-m: green"

# The run itself, recursive so BOARD is the Cortex-M board's and every path
# below (TARGET, BOOT_TARGET, the toolchain) is that board's.
#
# QEMU loads raw blobs, not the ELF files: the ELF would place the images
# where the link put them, but the signer has to edit both (the key and UDS
# slots in tfw-boot, the trailer after the application), and a raw image is
# what a part's flash holds. The nm listing is how cm_image finds the slots.
cortex-m-qemu-run: $(CM_QEMU_DIR)/cm_image
	@if [ "$(ARCH)" != cortex-m ]; then \
	   echo "$@: $(BOARD) is ARCH $(ARCH); run make integration-test-cortex-m"; exit 1; fi
	@command -v qemu-system-arm >/dev/null || { \
	   echo "$@: qemu-system-arm is not on PATH: install the Debian package qemu-system-arm"; \
	   echo "  (docker/packages.txt; the CI image has it)"; exit 1; }
	@$(ARCH_OBJCOPY) -O binary $(BOOT_TARGET).elf $(CM_QEMU_DIR)/tfw-boot.bin
	@$(ARCH_NM) $(BOOT_TARGET).elf > $(CM_QEMU_DIR)/tfw-boot.nm
	@$(ARCH_OBJCOPY) -O binary $(TARGET).elf $(CM_QEMU_DIR)/trusted-firmware.bin
	@$(CM_QEMU_DIR)/cm_image $(CM_QEMU_DIR)/tfw-boot.bin $(CM_QEMU_DIR)/tfw-boot.nm \
	   $(CM_QEMU_DIR)/trusted-firmware.bin $(CM_QEMU_DIR)
	@cd test/integration/esp32 && $(GO) build$(call go-tags,-tags) -o $(abspath $(CM_QEMU_DIR))/wirecheck ./wirecheck
	@test/integration/cortex-m/qemu_test.sh $(CM_QEMU_DIR) $(QEMU_MACHINE) $(CM_QEMU_BOARD_ID) $(CM_QEMU_TIER_ID)

# The signer and jig, for the host, with the board's profile compiled in: the
# addresses it signs for and writes into addrs.env are the board's. It links
# the firmware's src/crypto/deviceid_p256.c to derive the DeviceID point it
# writes there, so the point wirecheck checks is the one the image's code gives.
$(CM_QEMU_DIR)/cm_image: test/integration/cortex-m/cm_image.c boot/boot_image.c boot/boot_platform.c \
        src/crypto/sha256.c src/crypto/deviceid_p256.c src/core/secure_zero.c \
        src/proto/crc16.c src/proto/crc16_update.c $(BOOT_INT_DIR)/uECC.o
	@mkdir -p $(CM_QEMU_DIR)
	@$(HOST_CC) -std=c11 -g -O1 -Wall -Wextra -Werror -Wconversion -Wshadow \
	   -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Iboot -Isrc/third_party/micro-ecc $(UECC_DEFS) \
	   -I$(HAL_DIR) -I$(BOARD_DIR) -DTFW_BOARD_HEADER='"board_profile.h"' \
	   -DTFW_IMAGE_FLAGS='$(IMAGE_FLAGS)u' -o $@ $^
	@echo "built $@"

.PHONY: integration-test-cortex-m cortex-m-qemu-run
