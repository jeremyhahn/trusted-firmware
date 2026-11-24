# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The Cortex-M port's QEMU target.

# The one row it runs: the board's only tier, with tfw-boot verifying a P-256 signature in software and halting on a refusal (no OTA, no store).
CM_QEMU_BOARDS := mps2-an505 mps2-an500
CM_QEMU_TIER  := soft
CM_QEMU_ROW   := BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=0

# The soft root's identity on this row: the ES256 P-256 DeviceID derived from the UDS (mk/firmware.mk SOFT_ID), as on the ATmega2560, P-256 on every row.
CM_QEMU_SOFT_ID := p256

# What CAPS must report, as literals on purpose: each board's id (docs/boards/board-id-registry.md) and the soft tier's wire value.
CM_QEMU_BOARD_ID.mps2-an505 := 0x40
CM_QEMU_BOARD_ID.mps2-an500 := 0x42
CM_QEMU_TIER_ID  := 1

# Under the board's integration directory, beside mk/boot.mk's harness, whose host micro-ecc object the signer links.
CM_QEMU_DIR := $(BOOT_INT_DIR)/qemu

# The store row the wire run also boots (mk/boot.mk BOOT_SUPPORTED): tfw-boot takes an image the application staged into the board's memory store (OTA_STORE := mem), with no A/B.
CM_QEMU_STORE_BOARDS := mps2-an505 mps2-an500
CM_QEMU_STORE_ROW    := BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=1
CM_QEMU_STORE_DIR    := $(BOOT_INT_DIR)/qemu-store

# The A/B row (mk/boot.mk BOOT_SUPPORTED's :ab rows): the store row with two slots, the board's profile at BOOT_AB=1.
CM_QEMU_AB_BOARDS := mps2-an505 mps2-an500
CM_QEMU_AB_ROW    := BOOT_TIER=soft BOOT_MEASURE=soft BOOT_SIG=p256-soft BOOT_OTA=0 BOOT_STORE=1 BOOT_AB=1
CM_QEMU_AB_DIR    := $(BOOT_INT_DIR)/qemu-ab

# The SPI row (profile.mk's soft:SPI=1): the base row's image built with SPI=1, so SPI_XFER reaches the shield 0 PL022, which QEMU models with nothing on its bus (sdk/go/cmd/tfwctl/spi_qemu_test.go).
CM_QEMU_SPI_BOARDS := mps2-an505 mps2-an500
CM_QEMU_SPI_BUILD  := $(BUILD_DIR)/qemu-spi

# The tone row (profile.mk's soft:TONE=1), the same way: the base row's image built with TONE=1, so TONE and NOTONE reach hal_tone.c's TIMER0 (sdk/go/cmd/tfwctl/tone_qemu_test.go).
CM_QEMU_TONE_BOARDS := mps2-an505 mps2-an500
CM_QEMU_TONE_BUILD  := $(BUILD_DIR)/qemu-tone

# The PWM row (profile.mk's soft:PWM=1), the same way: the base row's image built with PWM=1, so PWM_WRITE reaches the software PWM on the CMSDK dual timer (sdk/go/cmd/tfwctl/pwm_qemu_test.go).
CM_QEMU_PWM_BOARDS := mps2-an505 mps2-an500
CM_QEMU_PWM_BUILD  := $(BUILD_DIR)/qemu-pwm

# The I2C row, the same way: the base row's image built with I2C=1, so I2C_XFER reaches the shield 0 SBCon, which QEMU models with nothing on its bus (sdk/go/cmd/tfwctl/i2c_qemu_test.go).
CM_QEMU_I2C_BOARDS := mps2-an505 mps2-an500
CM_QEMU_I2C_BUILD  := $(BUILD_DIR)/qemu-i2c

# The PULSE row: the base row's image built with PULSE=1, so PULSE_IN and PULSE_CAPTURE run the SysTick timer (src/hal/cortex-m/hal_pulse.c) on a pin QEMU does not drive (sdk/go/cmd/tfwctl/pulse_qemu_test.go).
CM_QEMU_PULSE_BOARDS := mps2-an505 mps2-an500
CM_QEMU_PULSE_BUILD  := $(BUILD_DIR)/qemu-pulse

# The 1-Wire row (profile.mk's soft:ONEWIRE=1), the same way: the base row's image built with ONEWIRE=1, so the 1-Wire verbs time their slots on GPIO block 0 bit 0, pin 3 (sdk/go/cmd/tfwctl/onewire_qemu_test.go).
CM_QEMU_ONEWIRE_BOARDS := mps2-an505 mps2-an500
CM_QEMU_ONEWIRE_BUILD  := $(BUILD_DIR)/qemu-onewire

## cm-frame-bound-check: the Cortex-M build refuses a frame past TFW_CM_FRAME_MAX, and a VLA (row 482)
CM_FRAME_PROBE_BOARD ?= mps2-an500
cm-frame-bound-check:
	@$(MAKE) --no-print-directory cm-frame-bound-probe BOARD=$(CM_FRAME_PROBE_BOARD)

CM_FRAME_PROBE_DIR := $(BUILD_DIR)/cm-frame-probe
cm-frame-bound-probe:
	@d=$(CM_FRAME_PROBE_DIR); mkdir -p $$d; \
	 probe() { $(ARCH_CC) -std=c11 -Wall -Wextra -Werror $(ARCH_MACH_CFLAGS) $(ARCH_OPT_CFLAGS) \
	             -c -o $$d/probe.o test/unit/cm_frame_probe.c "$$1" > $$d/probe.log 2>&1 && \
	           $(ARCH_CC) $(ARCH_LDFLAGS) -nostdlib -Wl,-e,probe_entry -o $$d/probe.elf $$d/probe.o \
	             >> $$d/probe.log 2>&1; }; \
	 fail=0; \
	 probe -DPROBE_FRAME=256 || { echo "$@: a 256-byte frame is refused:"; cat $$d/probe.log; fail=1; }; \
	 for n in $$(( $(CM_FRAME_MAX) + 8 )) 4096; do \
	   if probe -DPROBE_FRAME=$$n; then echo "$@: a $$n-byte frame links past the $(CM_FRAME_MAX)-byte bound"; fail=1; \
	   elif ! grep -q 'frame size of' $$d/probe.log; then echo "$@: a $$n-byte frame failed, not for its size:"; cat $$d/probe.log; fail=1; fi; \
	 done; \
	 if probe -DPROBE_VLA; then echo "$@: a variable-length array links"; fail=1; \
	 elif ! grep -q 'variable length array' $$d/probe.log; then echo "$@: the VLA failed, not as one:"; cat $$d/probe.log; fail=1; fi; \
	 [ $$fail = 0 ] && echo "$@: $(BOARD) refuses frames past $(CM_FRAME_MAX) bytes and VLAs"; exit $$fail

.PHONY: cm-frame-bound-check cm-frame-bound-probe

## integration-test-cortex-m: QEMU mps2-an505 (M33) and mps2-an500 (M7): tfw-boot verifies and boots the firmware across a reset; a tampered image and another key's stay silent; with no platform block tfw-boot refuses and names TFW_ERR_NOT_PROVISIONED; on mps2-an505 the tamper-response loop decides as its sets require
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
	@$(if $(filter $*,$(CM_QEMU_TONE_BOARDS)),$(MAKE) --no-print-directory cortex-m-qemu-tone-wire BOARD=$*,true)
	@$(if $(filter $*,$(CM_QEMU_PULSE_BOARDS)),$(MAKE) --no-print-directory cortex-m-qemu-pulse-wire BOARD=$*,true)
	@$(if $(filter $*,$(CM_QEMU_ONEWIRE_BOARDS)),$(MAKE) --no-print-directory cortex-m-qemu-onewire-wire BOARD=$*,true)
	@$(if $(filter $*,$(CM_QEMU_PWM_BOARDS)),$(MAKE) --no-print-directory cortex-m-qemu-pwm-wire BOARD=$*,true)
	@echo "$@: green"

# The store row's run, after the base row's, whose application it stages.
cortex-m-qemu-store-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) OTA_STAGE=1
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_STORE_ROW)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) OTA_STAGE=1 $(CM_QEMU_STORE_ROW) \
	   CM_QEMU_DIR=$(CM_QEMU_STORE_DIR) CM_QEMU_STAGED=$(TARGET)

# The A/B row's run, after the store row's.
cortex-m-qemu-ab-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) OTA_STAGE=1 BOOT_AB=1
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_AB_ROW)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) OTA_STAGE=1 $(CM_QEMU_AB_ROW) \
	   CM_QEMU_DIR=$(CM_QEMU_AB_DIR) CM_QEMU_AB=1

# The SPI row's run, after the A/B row's: the base row's tfw-boot and the SPI=1 application, every path under CM_QEMU_SPI_BUILD.
cortex-m-qemu-spi-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) SPI=1 BUILD_DIR=$(CM_QEMU_SPI_BUILD)
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_SPI_BUILD)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) SPI=1 $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_SPI_BUILD) CM_QEMU_SPI=1

# The tone row's run, after the I2C row's: the base row's tfw-boot and the TONE=1 application, every path under CM_QEMU_TONE_BUILD.
cortex-m-qemu-tone-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) TONE=1 BUILD_DIR=$(CM_QEMU_TONE_BUILD)
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_TONE_BUILD)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) TONE=1 $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_TONE_BUILD) CM_QEMU_TONE=1

# The PWM row's run: the base row's tfw-boot and the PWM=1 application, every path under CM_QEMU_PWM_BUILD.
cortex-m-qemu-pwm-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) PWM=1 BUILD_DIR=$(CM_QEMU_PWM_BUILD)
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_PWM_BUILD)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) PWM=1 $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_PWM_BUILD) CM_QEMU_PWM=1

# The I2C row's run, after the SPI row's: the base row's tfw-boot and the I2C=1 application, every path under CM_QEMU_I2C_BUILD.
cortex-m-qemu-i2c-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) I2C=1 BUILD_DIR=$(CM_QEMU_I2C_BUILD)
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_I2C_BUILD)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) I2C=1 $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_I2C_BUILD) CM_QEMU_I2C=1

# The PULSE row's run, after the tone row's: the base row's tfw-boot and the PULSE=1 application, every path under CM_QEMU_PULSE_BUILD.
cortex-m-qemu-pulse-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) PULSE=1 BUILD_DIR=$(CM_QEMU_PULSE_BUILD)
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_PULSE_BUILD)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) PULSE=1 $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_PULSE_BUILD) CM_QEMU_PULSE=1

# The 1-Wire row's run, after the PULSE row's: the base row's tfw-boot and the ONEWIRE=1 application, every path under CM_QEMU_ONEWIRE_BUILD.
cortex-m-qemu-onewire-wire:
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) ONEWIRE=1 BUILD_DIR=$(CM_QEMU_ONEWIRE_BUILD)
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_ONEWIRE_BUILD)
	@$(MAKE) --no-print-directory cortex-m-qemu-wire-run BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) ONEWIRE=1 $(CM_QEMU_ROW) BUILD_DIR=$(CM_QEMU_ONEWIRE_BUILD) CM_QEMU_ONEWIRE=1

# One OTA test against a boot qemu_wire_test.sh started for it, as it starts hw-test: PORT and HW_RESET are that boot's pty and QMP socket, STAGED the directory cm_image wrote the staged images and their authorizations into.
CYCLE_TEST ?= TestQEMUOTACycle
cortex-m-qemu-cycle:
	@cd $(SDK_DIR) && TFW_HW_PORT=$(PORT) TFW_HW_BOARD=$(BOARD) TFW_HW_RESET='$(HW_RESET)' \
	   TFW_QEMU_STAGED=$(abspath $(STAGED)) TFW_QEMU_FAULTFRAME=$(CM_FAULTFRAME_AT) \
	   $(GO) test -count=1$(call go-tags,-tags,qemu) -p 1 -run '^$(CYCLE_TEST)$$' ./cmd/tfwctl

# The runs themselves, recursive so BOARD is the Cortex-M board's and every path below (TARGET, BOOT_TARGET, the toolchain) is that board's.
cm-qemu-hsm = $(if $(filter 1soft,$(HSM)$(CRYPTO)),hsm)
cortex-m-qemu-run: cortex-m-qemu-image $(if $(cm-qemu-hsm),$(CM_QEMU_DIR)/hsm_sets)
	@$(if $(cm-qemu-hsm),$(CM_QEMU_DIR)/hsm_sets $(CM_QEMU_DIR),true)
	@cd test/integration/esp32 && $(GO) build$(call go-tags,-tags) -o $(abspath $(CM_QEMU_DIR))/wirecheck ./wirecheck
	@test/integration/cortex-m/qemu_test.sh $(CM_QEMU_DIR) $(QEMU_MACHINE) $(CM_QEMU_BOARD_ID.$(BOARD)) $(CM_QEMU_TIER_ID) $(cm-qemu-hsm)

# hw-test is a sub-make the script starts, with the board's BOARD, once it knows the pty; MAKE is passed so it is this make.
cortex-m-qemu-wire-run: cortex-m-qemu-image
	@command -v python3 >/dev/null || { \
	   echo "$@: python3 is not on PATH: qmp_pty.py reads the pty's path over QMP"; exit 1; }
	@MAKE='$(MAKE)' QEMU_RAM='$(RAM_BASE) $(RAM_BUDGET)' QEMU_UART='$(QEMU_UART)' \
	   QEMU_ELF='$(abspath $(TARGET).elf)' test/integration/cortex-m/qemu_wire_test.sh $(CM_QEMU_DIR) $(QEMU_MACHINE) $(BOARD) \
	   $(if $(filter 1,$(CM_QEMU_AB)),ab,$(if $(CM_QEMU_STAGED),store,$(if $(filter 1,$(CM_QEMU_SPI)),spi,$(if $(filter 1,$(CM_QEMU_I2C)),i2c,$(if $(filter 1,$(CM_QEMU_TONE)),tone,$(if $(filter 1,$(CM_QEMU_PWM)),pwm,$(if $(filter 1,$(CM_QEMU_ONEWIRE)),onewire,$(if $(filter 1,$(CM_QEMU_PULSE)),pulse))))))))

# The signed image both runs boot.
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

# The signer and jig, for the host, with the board's profile compiled in: the addresses it signs for and writes into addrs.env are the board's.
$(CM_QEMU_DIR)/cm_image: test/integration/cortex-m/cm_image.c boot/boot_image.c boot/boot_platform.c boot/boot_cose.c \
        src/crypto/sha256.c src/crypto/deviceid_p256.c src/core/secure_zero.c \
        src/proto/crc16.c src/proto/crc16_update.c $(BOOT_INT_DIR)/uECC.o
	@mkdir -p $(CM_QEMU_DIR)
	@$(HOST_CC) -std=c11 -g -O1 -Wall -Wextra -Werror -Wconversion -Wshadow \
	   -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Iboot -Isrc/third_party/micro-ecc $(UECC_DEFS) \
	   -I$(HAL_DIR) -I$(BOARD_DIR) -DTFW_BOARD_HEADER='"board_profile.h"' $(BOARD_DEFS) \
	   -DTFW_IMAGE_FLAGS='$(IMAGE_FLAGS)u' -o $@ $^
	@echo "built $@"

## cortex-m-dk-image: the STM32H573I-DK's signed bench set, with its unprovisioned boot (BOARD=stm32h573i-dk)
CM_DK_BOARDS := stm32h573i-dk
CM_DK_DIR    := $(BOOT_INT_DIR)/bench
CM_DK_DEFS   := -DTFW_CM_LOAD_APP_AT=TFW_APP_REGION_BASE -DTFW_CM_LOAD_PLATFORM_AT=TFW_CM_PLATFORM_BASE \
                -DTFW_OTA_STORE_STAGE_LEN=0x020000ul

cortex-m-dk-image:
	@if [ -z "$(filter $(BOARD),$(CM_DK_BOARDS))" ]; then \
	   echo "$@: BOARD=$(BOARD) is not one of $(CM_DK_BOARDS); a QEMU board runs cortex-m-qemu-image"; exit 1; fi
	@$(MAKE) --no-print-directory firmware BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) SOFT_ID=$(CM_QEMU_SOFT_ID)
	@$(MAKE) --no-print-directory bootloader BOARD=$(BOARD) $(CM_QEMU_ROW)
	@$(MAKE) --no-print-directory cortex-m-dk-sign BOARD=$(BOARD) CRYPTO=$(CM_QEMU_TIER) \
	   SOFT_ID=$(CM_QEMU_SOFT_ID) $(CM_QEMU_ROW)

# The signing step, recursive so TARGET and BOOT_TARGET are the board's.
cortex-m-dk-sign: $(CM_DK_DIR)/cm_image
	@$(ARCH_OBJCOPY) -O binary $(BOOT_TARGET).elf $(CM_DK_DIR)/tfw-boot.bin
	@$(ARCH_NM) $(BOOT_TARGET).elf > $(CM_DK_DIR)/tfw-boot.nm
	@$(ARCH_OBJCOPY) -O binary $(TARGET).elf $(CM_DK_DIR)/trusted-firmware.bin
	@$(CM_DK_DIR)/cm_image $(CM_DK_DIR)/tfw-boot.bin $(CM_DK_DIR)/tfw-boot.nm \
	   $(CM_DK_DIR)/trusted-firmware.bin $(CM_DK_DIR)
	@$(call cm-dk-pack,boot.bin,bench.hex)
	@$(call cm-dk-pack,boot-unprovisioned.bin,bench-unprovisioned.hex)
	@echo "$(BOARD): bench set in $(CM_DK_DIR) (bench.hex, bench-unprovisioned.hex)"

# One Intel HEX of a boot, app.bin and platform.bin at addrs.env's addresses: each blob converted on its own, their end-of-file records dropped, one added.
cm-dk-pack = d=$(CM_DK_DIR); . $$d/addrs.env || exit 1; \
	{ keep=1; for p in "$(1) $$BOOT_AT" "app.bin $$APP_AT" "platform.bin $$PLATFORM_AT"; do \
	    set -- $$p; $(ARCH_OBJCOPY) -I binary -O ihex --change-addresses $$2 $$d/$$1 $$d/part.hex || exit 1; \
	    if [ $$keep = 1 ]; then grep -v '^:00000001FF' $$d/part.hex; \
	    else grep -v -e '^:00000001FF' -e '^:0400000[35]' $$d/part.hex; fi; keep=0; \
	    done; echo ':00000001FF'; } > $$d/$(2).tmp && \
	mv $$d/$(2).tmp $$d/$(2) && rm -f $$d/part.hex && $(call cm-dk-hex-check,$$d/$(2))

cm-dk-hex-check = s=$$(grep -c '^:0400000[35]' $(1)); e=$$(grep -c '^:00000001FF' $(1)); \
	if [ "$$s" != 1 ] || [ "$$e" != 1 ]; then \
	  echo "$(1): $$s start-address and $$e end-of-file records, want 1 and 1"; exit 1; fi

# The packer on three small blobs with the host objcopy, no board image: the packed HEX carries one start-address record and one end-of-file record.
cm-dk-pack-test: CM_DK_DIR = $(BUILD_DIR)/cm-dk-pack-test
cm-dk-pack-test: ARCH_OBJCOPY = $(or $(CM_DK_TEST_OBJCOPY),objcopy)
cm-dk-pack-test:
	@rm -rf $(CM_DK_DIR) && mkdir -p $(CM_DK_DIR)
	@printf 'BOOT_AT=0x08000000\nAPP_AT=0x08020000\nPLATFORM_AT=0x081F0000\n' > $(CM_DK_DIR)/addrs.env
	@for f in boot.bin app.bin platform.bin; do printf '%s-0123456789' $$f > $(CM_DK_DIR)/$$f; done
	@$(call cm-dk-pack,boot.bin,bench.hex)
	@echo "cm-dk-pack-test: one start-address and one end-of-file record"
	@rm -rf $(CM_DK_DIR)

# The signer for the DK: cm_image's sources, with CM_DK_DEFS added to the board's BOARD_DEFS, in a directory of its own.
$(CM_DK_DIR)/cm_image: test/integration/cortex-m/cm_image.c boot/boot_image.c boot/boot_platform.c boot/boot_cose.c \
        src/crypto/sha256.c src/crypto/deviceid_p256.c src/core/secure_zero.c \
        src/proto/crc16.c src/proto/crc16_update.c $(BOOT_INT_DIR)/uECC.o
	@mkdir -p $(CM_DK_DIR)
	@$(HOST_CC) -std=c11 -g -O1 -Wall -Wextra -Werror -Wconversion -Wshadow \
	   -Iinclude -Isrc -Isrc/proto -Isrc/crypto -Iboot -Isrc/third_party/micro-ecc $(UECC_DEFS) \
	   -I$(HAL_DIR) -I$(BOARD_DIR) -DTFW_BOARD_HEADER='"board_profile.h"' $(BOARD_DEFS) $(CM_DK_DEFS) \
	   -DTFW_IMAGE_FLAGS='$(IMAGE_FLAGS)u' -o $@ $^
	@echo "built $@"

# The tamper-response cases' policy sets (test/integration/cortex-m/hsm_sets.c), signed by the build's SUIT emitter with the same host micro-ecc object.
$(CM_QEMU_DIR)/hsm_sets: test/integration/cortex-m/hsm_sets.c tools/suit-emit/emit.c \
        src/crypto/sha256.c $(BOOT_INT_DIR)/uECC.o
	@mkdir -p $(CM_QEMU_DIR)
	@$(HOST_CC) -std=c11 -g -O1 -Wall -Wextra -Werror -Wconversion -Wshadow \
	   -Iinclude -Isrc -Isrc/crypto -Itools/suit-emit -Isrc/third_party/micro-ecc $(UECC_DEFS) \
	   -o $@ $^
	@echo "built $@"

# TZ=1: the TrustZone-M secure/non-secure split.
TZ_DIR      = $(BUILD_DIR)/tz/$(BOARD)
TZ_SRC     := src/hal/cortex-m/tz
TZ_CFLAGS   = -mcpu=$(CPU) -mthumb -mfloat-abi=soft -std=c11 -Os -g -ffreestanding \
              -Wall -Wextra -Werror -Wconversion -Wshadow \
              -include $(TZ_MAP) -Iinclude -I$(TZ_SRC)
TZ_LDFLAGS  = -nostdlib -nostartfiles -Wl,--no-warn-rwx-segments
TZ_S_SRCS   = $(TZ_SRC)/tz_s_main.c $(TZ_SRC)/tz_veneer.c $(TZ_SRC)/tz_regions.c $(TZ_SOC_SRCS)
TZ_NS_SRCS  = $(TZ_SRC)/tz_ns_main.c
TZ_HDRS    := $(wildcard $(TZ_SRC)/*.h) include/tfw/tz_s_api.h

tz-guard:
	@if [ -z "$(TZ_MAP)" ]; then \
	   echo "TZ=1: $(BOARD) has no TZ_MAP in its profile.mk (mps2-an505 and nucleo-h563zi have)"; exit 1; fi

# The linker scripts, through the C preprocessor with the board's map.
$(TZ_DIR)/%.ld: mk/arch/cortex-m/%.ld $(TZ_MAP)
	@mkdir -p $(@D)
	@$(ARCH_CC) -E -P -x c -include $(TZ_MAP) $< -o $@

$(TZ_DIR)/tz_s.elf: $(TZ_S_SRCS) $(TZ_HDRS) $(TZ_MAP) $(TZ_DIR)/tz_s.ld
	@$(ARCH_CC) $(TZ_CFLAGS) -mcmse $(TZ_LDFLAGS) -T $(TZ_DIR)/tz_s.ld \
	   -Wl,--section-start=.gnu.sgstubs=$$(echo TZ_NSC_BASE | $(ARCH_CC) -E -P -x c -include $(TZ_MAP) -) \
	   -Wl,--cmse-implib -Wl,--out-implib=$(TZ_DIR)/tz_s_veneers.o \
	   -o $@ $(TZ_S_SRCS) -lgcc
	@echo "built $@"

# Written by the secure link above.
$(TZ_DIR)/tz_s_veneers.o: $(TZ_DIR)/tz_s.elf
	@test -f $@

$(TZ_DIR)/tz_ns.elf: $(TZ_NS_SRCS) $(TZ_HDRS) $(TZ_MAP) $(TZ_DIR)/tz_ns.ld $(TZ_DIR)/tz_s_veneers.o
	@$(ARCH_CC) $(TZ_CFLAGS) $(TZ_LDFLAGS) -Wl,--gc-sections -ffunction-sections -T $(TZ_DIR)/tz_ns.ld \
	   -o $@ $(TZ_NS_SRCS) $(TZ_DIR)/tz_s_veneers.o -lgcc
	@echo "built $@"

## tz-firmware: TZ=1 boards (mps2-an505, nucleo-h563zi): the secure image, its veneer import library and the non-secure image, size-checked
tz-firmware: tz-guard
	@$(MAKE) --no-print-directory tz-size-check

# Each image against its regions in the board's map: code (load end), the NSC region's veneers (secure image), RAM (.data and .bss).
tz-size-check: tz-guard $(TZ_DIR)/tz_s.elf $(TZ_DIR)/tz_ns.elf
	@fail=0; for img in tz_s tz_ns; do \
	   tz_nsc_used=0; tz_nsc_size=0; \
	   eval "$$($(ARCH_NM) $(TZ_DIR)/$$img.elf | awk '$$3 ~ /^__tz_(code|ram|nsc)_(used|size)$$/ { print substr($$3, 3) "=$$((0x" $$1 "))" }')"; \
	   printf '%-14s %-5s code %6d / %-6d nsc %4d / %-4d ram %6d / %-6d\n' "$(BOARD)" "$$img" \
	     "$$tz_code_used" "$$tz_code_size" "$$tz_nsc_used" "$$tz_nsc_size" "$$tz_ram_used" "$$tz_ram_size"; \
	   if [ -z "$$tz_code_used" ] || [ -z "$$tz_ram_used" ]; then echo "tz-size-check: cannot measure $$img"; fail=1; continue; fi; \
	   if [ "$$tz_code_used" -gt "$$tz_code_size" ] || [ "$$tz_nsc_used" -gt "$$tz_nsc_size" ] || \
	      [ "$$tz_ram_used" -gt "$$tz_ram_size" ]; then echo "TZ BUDGET EXCEEDED for $(BOARD) $$img"; fail=1; fi; \
	 done; exit $$fail

## integration-test-cortex-m-tz: QEMU mps2-an505, TZ=1: the secure image sets up the SAU and enters the non-secure image, which calls tfw_s_version through its NSC veneer; a non-secure read of secure SRAM is a SecureFault the secure handler reports
integration-test-cortex-m-tz:
	@$(MAKE) --no-print-directory cortex-m-qemu-tz-run BOARD=mps2-an505 TZ=1
	@echo "$@: green"

cortex-m-qemu-tz-run: tz-firmware
	@command -v qemu-system-arm >/dev/null || { \
	   echo "$@: qemu-system-arm is not on PATH: install the Debian package qemu-system-arm"; exit 1; }
	@$(ARCH_OBJCOPY) -O binary $(TZ_DIR)/tz_s.elf $(TZ_DIR)/tz_s.bin
	@$(ARCH_OBJCOPY) -O binary $(TZ_DIR)/tz_ns.elf $(TZ_DIR)/tz_ns.bin
	@sym() { $(ARCH_NM) $(TZ_DIR)/$$1.elf | awk -v s="$$2" '$$3 == s { print "0x" $$1 }'; }; \
	 test/integration/cortex-m/qemu_tz_test.sh $(TZ_DIR) $(QEMU_MACHINE) \
	   "$$(sym tz_s __tz_load)" "$$(sym tz_ns __tz_load)" "$$(sym tz_ns __tz_s_ram)"

.PHONY: tz-guard tz-firmware tz-size-check integration-test-cortex-m-tz cortex-m-qemu-tz-run

.PHONY: integration-test-cortex-m cortex-m-qemu-run cortex-m-qemu-image \
        integration-test-cortex-m-wire cortex-m-qemu-wire-run cortex-m-qemu-pulse-wire cortex-m-qemu-store-wire cortex-m-qemu-ab-wire \
        cortex-m-qemu-spi-wire cortex-m-qemu-i2c-wire cortex-m-qemu-tone-wire cortex-m-qemu-onewire-wire cortex-m-qemu-pwm-wire \
        cortex-m-qemu-cycle cortex-m-dk-image cortex-m-dk-sign cm-dk-pack-test \
        $(addprefix integration-test-cortex-m-,$(CM_QEMU_BOARDS)) \
        $(addprefix integration-test-cortex-m-wire-,$(CM_QEMU_BOARDS))
