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
#
# Every Cortex-M board QEMU models, so every change to the core boots on both
# cores: the ARMv8-M Cortex-M33 (mps2-an505, #161) and the ARMv7-M Cortex-M7
# (mps2-an500). Each runs the same three cases.
CM_QEMU_BOARDS := mps2-an505 mps2-an500
CM_QEMU_TIER  := soft
CM_QEMU_ROW   := BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=0

# The soft root's identity on this row: the ES256 P-256 DeviceID derived from
# the UDS (mk/firmware.mk SOFT_ID), as on the ATmega2560 -- P-256 on every row
# (m522). Named here, not left to the default, so the row cannot fall back to
# the HMAC signer; CAPS reports ecdsa-p256-sha256 and wirecheck verifies ATTEST
# under the point cm_image derives from the UDS it provisions.
CM_QEMU_SOFT_ID := p256

# What CAPS must report, as literals on purpose: each board's id
# (docs/boards/board-id-registry.md) and the soft tier's wire value. A number
# read back from the build would agree with whatever the build got wrong.
CM_QEMU_BOARD_ID.mps2-an505 := 0x40
CM_QEMU_BOARD_ID.mps2-an500 := 0x42
CM_QEMU_TIER_ID  := 1

# Under the board's integration directory, beside mk/boot.mk's harness, whose
# host micro-ecc object the signer links.
CM_QEMU_DIR := $(BOOT_INT_DIR)/qemu

# The store row the wire run also boots (mk/boot.mk BOOT_SUPPORTED): tfw-boot
# takes an image the application staged into the board's memory store
# (OTA_STORE := mem), with no A/B. Every board here carries it; a board that
# did not would leave this list and run the base rows alone. Its application
# is built with the stager (OTA_STAGE=1) and its images go in a directory of
# their own, so the base run's are never overwritten.
CM_QEMU_STORE_BOARDS := mps2-an505 mps2-an500
CM_QEMU_STORE_ROW    := BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=1
CM_QEMU_STORE_DIR    := $(BOOT_INT_DIR)/qemu-store

# The A/B row (mk/boot.mk BOOT_SUPPORTED's :ab rows): the store row with two
# slots, the board's profile at BOOT_AB=1. QEMU loads the application and the
# platform block at their factory copies and tfw-boot copies each home, so a
# system_reset keeps what the last boot left in the slots and the block
# (docs/cortex-m/README.md, two slots). Its images go in a directory of their
# own, as the store row's do.
CM_QEMU_AB_BOARDS := mps2-an505 mps2-an500
CM_QEMU_AB_ROW    := BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=1 BOOT_AB=1
CM_QEMU_AB_DIR    := $(BOOT_INT_DIR)/qemu-ab

# The SPI row (profile.mk's soft:SPI=1): the base row's image built with SPI=1,
# so SPI_XFER reaches the shield 0 PL022, which QEMU models with nothing on its
# bus (sdk/go/cmd/tfwctl/spi_qemu_test.go). FW_DIR names SPI=1 (-spi), so no
# object compiled without the SPI gate is linked into it; the row still builds
# under a BUILD_DIR of its own, so its tfw-boot and its run's files are its own.
CM_QEMU_SPI_BOARDS := mps2-an505 mps2-an500
CM_QEMU_SPI_BUILD  := $(BUILD_DIR)/qemu-spi

# The I2C row, the same way: the base row's image built with I2C=1, so
# I2C_XFER reaches the shield 0 SBCon, which QEMU models with nothing on its
# bus (sdk/go/cmd/tfwctl/i2c_qemu_test.go). Each board's soft:I2C=1 is this
# image, so size-check measures what the row runs; mps2-an505's other I2C row,
# atecc608a:I2C=1, is the element tier's, which QEMU cannot boot. Its own
# BUILD_DIR for the same reason.
CM_QEMU_I2C_BOARDS := mps2-an505 mps2-an500
CM_QEMU_I2C_BUILD  := $(BUILD_DIR)/qemu-i2c

# The PULSE row: the base row's image built with PULSE=1, so PULSE_IN and
# PULSE_CAPTURE run the SysTick timer (src/hal/cortex-m/hal_pulse.c) on a pin
# QEMU does not drive (sdk/go/cmd/tfwctl/pulse_qemu_test.go). The board's
# soft:PULSE=1 row is this image. Its own BUILD_DIR, as the I2C row's.
CM_QEMU_PULSE_BOARDS := mps2-an505
CM_QEMU_PULSE_BUILD  := $(BUILD_DIR)/qemu-pulse

# The 1-Wire row (profile.mk's soft:ONEWIRE=1), the same way: the base row's
# image built with ONEWIRE=1, so the 1-Wire verbs time their slots on GPIO
# block 0 bit 0, pin 3 (sdk/go/cmd/tfwctl/onewire_qemu_test.go). Its own
# BUILD_DIR for the same reason.
CM_QEMU_ONEWIRE_BOARDS := mps2-an505 mps2-an500
CM_QEMU_ONEWIRE_BUILD  := $(BUILD_DIR)/qemu-onewire

## integration-test-cortex-m: QEMU mps2-an505 (M33) and mps2-an500 (M7): tfw-boot verifies and boots the firmware across a reset; a tampered image and another key's stay silent; on mps2-an505 the tamper-response loop decides as its sets require
#
# One board after the other, not in parallel: a red board names itself, and
# the two QEMUs never compete for the host. integration-test-cortex-m-<board>
# runs one.
integration-test-cortex-m:
	@$(foreach b,$(CM_QEMU_BOARDS),$(MAKE) --no-print-directory integration-test-cortex-m-$(b) &&) true
	@echo "integration-test-cortex-m: green"

$(addprefix integration-test-cortex-m-,$(CM_QEMU_BOARDS)): integration-test-cortex-m-%:
	@$(MAKE) --no-print-directory firmware BOARD=$* CRYPTO=$(CM_QEMU_TIER) SOFT_ID=$(CM_QEMU_SOFT_ID)
	@$(MAKE) --no-print-directory bootloader BOARD=$* $(CM_QEMU_ROW)
	@$(MAKE) --no-print-directory cortex-m-qemu-run BOARD=$* CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) $(CM_QEMU_ROW)
	@echo "$@: green"

## integration-test-cortex-m-wire: the tfwctl wire suite (hw-test) on QEMU mps2-an505 and mps2-an500, through a pty and a QMP reset
#
# The same signed image as integration-test-cortex-m, booted once per board
# with its UART on a pty; the suite reaches it through internal/peer as it
# would a board on a serial port, and resets it with QMP system_reset
# (HW_RESET=qmp:<socket>) where a board's DTR line would.
# test/integration/cortex-m/qemu_wire_test.sh runs it.
integration-test-cortex-m-wire:
	@$(foreach b,$(CM_QEMU_BOARDS),$(MAKE) --no-print-directory integration-test-cortex-m-wire-$(b) &&) true
	@echo "integration-test-cortex-m-wire: green"

$(addprefix integration-test-cortex-m-wire-,$(CM_QEMU_BOARDS)): integration-test-cortex-m-wire-%:
	@$(MAKE) --no-print-directory firmware BOARD=$* CRYPTO=$(CM_QEMU_TIER) SOFT_ID=$(CM_QEMU_SOFT_ID)
	@$(MAKE) --no-print-directory bootloader BOARD=$* $(CM_QEMU_ROW)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$* CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) $(CM_QEMU_ROW)
	@$(if $(filter $*,$(CM_QEMU_STORE_BOARDS)),$(MAKE) --no-print-directory cortex-m-qemu-store-wire BOARD=$*,true)
	@$(if $(filter $*,$(CM_QEMU_AB_BOARDS)),$(MAKE) --no-print-directory cortex-m-qemu-ab-wire BOARD=$*,true)
	@$(if $(filter $*,$(CM_QEMU_SPI_BOARDS)),$(MAKE) --no-print-directory cortex-m-qemu-spi-wire BOARD=$*,true)
	@$(if $(filter $*,$(CM_QEMU_I2C_BOARDS)),$(MAKE) --no-print-directory cortex-m-qemu-i2c-wire BOARD=$*,true)
	@$(if $(filter $*,$(CM_QEMU_PULSE_BOARDS)),$(MAKE) --no-print-directory cortex-m-qemu-pulse-wire BOARD=$*,true)
	@$(if $(filter $*,$(CM_QEMU_ONEWIRE_BOARDS)),$(MAKE) --no-print-directory cortex-m-qemu-onewire-wire BOARD=$*,true)
	@echo "$@: green"

# The store row's run, after the base row's, whose application it stages.
# This make's TARGET is that base image (no OTA_STAGE here), so it names the
# image the cycle installs; the sub-makes build and boot the stager.
cortex-m-qemu-store-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) OTA_STAGE=1
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_STORE_ROW)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) OTA_STAGE=1 $(CM_QEMU_STORE_ROW) \
	   CM_QEMU_DIR=$(CM_QEMU_STORE_DIR) CM_QEMU_STAGED=$(TARGET)

# The A/B row's run, after the store row's. Its application is the A/B build
# with the stager, and that same image, tagged, is what its tests stage
# (cm_image.c), so CM_QEMU_AB names no other build.
cortex-m-qemu-ab-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) OTA_STAGE=1 BOOT_AB=1
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_AB_ROW)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) OTA_STAGE=1 $(CM_QEMU_AB_ROW) \
	   CM_QEMU_DIR=$(CM_QEMU_AB_DIR) CM_QEMU_AB=1

# The SPI row's run, after the A/B row's: the base row's tfw-boot and the
# SPI=1 application, every path under CM_QEMU_SPI_BUILD.
cortex-m-qemu-spi-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) SPI=1 BUILD_DIR=$(CM_QEMU_SPI_BUILD)
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_SPI_BUILD)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) SPI=1 $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_SPI_BUILD) CM_QEMU_SPI=1

# The I2C row's run, after the SPI row's: the base row's tfw-boot and the I2C=1
# application, every path under CM_QEMU_I2C_BUILD.
cortex-m-qemu-i2c-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) I2C=1 BUILD_DIR=$(CM_QEMU_I2C_BUILD)
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_I2C_BUILD)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) I2C=1 $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_I2C_BUILD) CM_QEMU_I2C=1

# The PULSE row's run, after the I2C row's: the base row's tfw-boot and the
# PULSE=1 application, every path under CM_QEMU_PULSE_BUILD.
cortex-m-qemu-pulse-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) PULSE=1 BUILD_DIR=$(CM_QEMU_PULSE_BUILD)
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_PULSE_BUILD)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) PULSE=1 $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_PULSE_BUILD) CM_QEMU_PULSE=1

# The 1-Wire row's run, after the I2C row's: the base row's tfw-boot and the
# ONEWIRE=1 application, every path under CM_QEMU_ONEWIRE_BUILD.
cortex-m-qemu-onewire-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) ONEWIRE=1 BUILD_DIR=$(CM_QEMU_ONEWIRE_BUILD)
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_ONEWIRE_BUILD)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) ONEWIRE=1 $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_ONEWIRE_BUILD) CM_QEMU_ONEWIRE=1

# One OTA test against a boot qemu_wire_test.sh started for it, as it starts
# hw-test: PORT and HW_RESET are that boot's pty and QMP socket, STAGED the
# directory cm_image wrote the staged images and their authorizations into.
# CYCLE_TEST names the test: the store row's TestQEMUOTACycle
# (sdk/go/cmd/tfwctl/ota_cycle_qemu_test.go), or one of the A/B row's
# TestQEMUAB* (ota_ab_qemu_test.go), or the SPI row's TestQEMUSPIXfer
# (spi_qemu_test.go), or the I2C row's TestQEMUI2CXfer (i2c_qemu_test.go), or
# the base row's TestQEMUResetCause (reset_qemu_test.go) or TestQEMUFaultPanic
# (fault_qemu_test.go), which reads the fault record at TFW_QEMU_FAULTFRAME.
CYCLE_TEST ?= TestQEMUOTACycle
cortex-m-qemu-cycle:
	@cd $(SDK_DIR) && TFW_HW_PORT=$(PORT) TFW_HW_BOARD=$(BOARD) TFW_HW_RESET='$(HW_RESET)' \
	   TFW_QEMU_STAGED=$(abspath $(STAGED)) TFW_QEMU_FAULTFRAME=$(CM_FAULTFRAME_AT) \
	   $(GO) test -count=1$(call go-tags,-tags,qemu) -p 1 -run '^$(CYCLE_TEST)$$' ./cmd/tfwctl

# The runs themselves, recursive so BOARD is the Cortex-M board's and every
# path below (TARGET, BOOT_TARGET, the toolchain) is that board's. On an image
# that links the tamper-response engine (mk/firmware.mk HSM_SRCS: HSM := 1 on
# the soft tier, mps2-an505), the run also writes the engine's policy sets and
# qemu_test.sh runs its hsm-* cases.
cm-qemu-hsm = $(if $(filter 1soft,$(HSM)$(CRYPTO)),hsm)
cortex-m-qemu-run: cortex-m-qemu-image $(if $(cm-qemu-hsm),$(CM_QEMU_DIR)/hsm_sets)
	@$(if $(cm-qemu-hsm),$(CM_QEMU_DIR)/hsm_sets $(CM_QEMU_DIR),true)
	@cd test/integration/esp32 && $(GO) build$(call go-tags,-tags) -o $(abspath $(CM_QEMU_DIR))/wirecheck ./wirecheck
	@test/integration/cortex-m/qemu_test.sh $(CM_QEMU_DIR) $(QEMU_MACHINE) $(CM_QEMU_BOARD_ID.$(BOARD)) $(CM_QEMU_TIER_ID) $(cm-qemu-hsm)

# hw-test is a sub-make the script starts, with the board's BOARD, once it
# knows the pty; MAKE is passed so it is this make. A failed row's dump
# (qmp_regs.py) reads QEMU_RAM, the board's RAM base and budget; QEMU_UART,
# its UART0's registers (profile.mk); and the firmware's ring and framer by
# symbol from QEMU_ELF, the image the guest runs.
cortex-m-qemu-wire-run: cortex-m-qemu-image
	@command -v python3 >/dev/null || { \
	   echo "$@: python3 is not on PATH: qmp_pty.py reads the pty's path over QMP"; exit 1; }
	@MAKE='$(MAKE)' QEMU_RAM='$(RAM_BASE) $(RAM_BUDGET)' QEMU_UART='$(QEMU_UART)' \
	   QEMU_ELF='$(abspath $(TARGET).elf)' test/integration/cortex-m/qemu_wire_test.sh $(CM_QEMU_DIR) $(QEMU_MACHINE) $(BOARD) \
	   $(if $(filter 1,$(CM_QEMU_AB)),ab,$(if $(CM_QEMU_STAGED),store,$(if $(filter 1,$(CM_QEMU_SPI)),spi,$(if $(filter 1,$(CM_QEMU_I2C)),i2c,$(if $(filter 1,$(CM_QEMU_ONEWIRE)),onewire,$(if $(filter 1,$(CM_QEMU_PULSE)),pulse))))))

# The signed image both runs boot. On the store row (CM_QEMU_STAGED, the
# application's path without its extension), cm_image also signs the image
# the OTA cycle stages and its activate and confirm authorizations. On the A/B
# row (CM_QEMU_AB=1) the image staged is the application itself, and cm_image
# signs it twice, tagged: at version 2 and, for the floor, at version 1.
#
# QEMU loads raw blobs, not the ELF files: the ELF would place the images
# where the link put them, but the signer has to edit both (the key and UDS
# slots in tfw-boot, the trailer after the application), and a raw image is
# what a part's flash holds. The nm listing is how cm_image finds the slots.
cortex-m-qemu-image: $(CM_QEMU_DIR)/cm_image
	@if [ "$(ARCH)" != cortex-m ]; then \
	   echo "$@: $(BOARD) is ARCH $(ARCH); run make integration-test-cortex-m"; exit 1; fi
	@if [ -z "$(CM_QEMU_BOARD_ID.$(BOARD))" ]; then \
	   echo "$@: $(BOARD) has no CM_QEMU_BOARD_ID.$(BOARD) literal in mk/arch/cortex-m-targets.mk"; exit 1; fi
	@command -v qemu-system-arm >/dev/null || { \
	   echo "$@: qemu-system-arm is not on PATH: install the Debian package qemu-system-arm"; \
	   echo "  (docker/packages.txt; the CI image has it)"; exit 1; }
	@$(ARCH_OBJCOPY) -O binary $(BOOT_TARGET).elf $(CM_QEMU_DIR)/tfw-boot.bin
	@$(ARCH_NM) $(BOOT_TARGET).elf > $(CM_QEMU_DIR)/tfw-boot.nm
	@$(ARCH_OBJCOPY) -O binary $(TARGET).elf $(CM_QEMU_DIR)/trusted-firmware.bin
	@$(if $(cm-qemu-staged),$(ARCH_OBJCOPY) -O binary $(cm-qemu-staged).elf $(CM_QEMU_DIR)/staged-app.bin,true)
	@$(CM_QEMU_DIR)/cm_image $(CM_QEMU_DIR)/tfw-boot.bin $(CM_QEMU_DIR)/tfw-boot.nm \
	   $(CM_QEMU_DIR)/trusted-firmware.bin $(CM_QEMU_DIR) \
	   $(if $(cm-qemu-staged),$(CM_QEMU_DIR)/staged-app.bin) $(if $(filter 1,$(CM_QEMU_AB)),ab)

# The application cortex-m-qemu-image signs for staging, if any.
cm-qemu-staged = $(if $(filter 1,$(CM_QEMU_AB)),$(TARGET),$(CM_QEMU_STAGED))

# The signer and jig, for the host, with the board's profile compiled in: the
# addresses it signs for and writes into addrs.env are the board's. It links
# the firmware's src/crypto/deviceid_p256.c to derive the DeviceID point it
# writes there, so the point wirecheck checks is the one the image's code gives.
$(CM_QEMU_DIR)/cm_image: test/integration/cortex-m/cm_image.c boot/boot_image.c boot/boot_platform.c boot/boot_cose.c \
        src/crypto/sha256.c src/crypto/deviceid_p256.c src/core/secure_zero.c \
        src/proto/crc16.c src/proto/crc16_update.c $(BOOT_INT_DIR)/uECC.o
	@mkdir -p $(CM_QEMU_DIR)
	@$(HOST_CC) -std=c11 -g -O1 -Wall -Wextra -Werror -Wconversion -Wshadow \
	   -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Iboot -Isrc/third_party/micro-ecc $(UECC_DEFS) \
	   -I$(HAL_DIR) -I$(BOARD_DIR) -DTFW_BOARD_HEADER='"board_profile.h"' $(BOARD_DEFS) \
	   -DTFW_IMAGE_FLAGS='$(IMAGE_FLAGS)u' -o $@ $^
	@echo "built $@"

# The tamper-response cases' policy sets (test/integration/cortex-m/hsm_sets.c),
# signed by the build's SUIT emitter with the same host micro-ecc object.
$(CM_QEMU_DIR)/hsm_sets: test/integration/cortex-m/hsm_sets.c tools/suit-emit/emit.c \
        src/crypto/sha256.c $(BOOT_INT_DIR)/uECC.o
	@mkdir -p $(CM_QEMU_DIR)
	@$(HOST_CC) -std=c11 -g -O1 -Wall -Wextra -Werror -Wconversion -Wshadow \
	   -Iinclude -Isrc -Isrc/crypto -Itools/suit-emit -Isrc/third_party/micro-ecc $(UECC_DEFS) \
	   -o $@ $^
	@echo "built $@"

.PHONY: integration-test-cortex-m cortex-m-qemu-run cortex-m-qemu-image \
        integration-test-cortex-m-wire cortex-m-qemu-wire-run cortex-m-qemu-pulse-wire cortex-m-qemu-store-wire cortex-m-qemu-ab-wire \
        cortex-m-qemu-spi-wire cortex-m-qemu-i2c-wire cortex-m-qemu-onewire-wire \
        cortex-m-qemu-cycle \
        $(addprefix integration-test-cortex-m-,$(CM_QEMU_BOARDS)) \
        $(addprefix integration-test-cortex-m-wire-,$(CM_QEMU_BOARDS))
