# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The SUIT manifest's to-be-signed bytes.

SUIT_EMIT_BIN  := $(BUILD_DIR)/tools/suit-emit-$(BOARD)
SUIT_EMIT_SRCS := tools/suit-emit/main.c tools/suit-emit/emit.c src/crypto/sha256.c
SUIT_OUT       := $(REL_DIR)/$(BOARD)-$(CRYPTO)

$(SUIT_EMIT_BIN): $(SUIT_EMIT_SRCS) tools/suit-emit/emit.h $(BOARD_DIR)/board_profile.h include/tfw/config.h
	@mkdir -p $(dir $@)
	@$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -Wconversion \
	   -Iinclude -Isrc/crypto -Itools/suit-emit -I$(BOARD_DIR) \
	   -DTFW_BOARD_HEADER='"board_profile.h"' -o $@ $(SUIT_EMIT_SRCS)

suit-tbs: provenance-one $(SUIT_EMIT_BIN)
	@if [ -z "$(IMAGE_VERSION)" ]; then \
	  echo "suit-tbs: name the image version: IMAGE_VERSION=<n>, the trailer's image_version."; \
	  echo "  The manifest's sequence number must equal it, or the device refuses the image."; \
	  exit 1; \
	fi
	@$(SUIT_EMIT_BIN) tbs $(SUIT_OUT).bin $(IMAGE_VERSION) $(SUIT_OUT).suit-manifest $(SUIT_OUT).suit-tbs
	@echo "  $(SUIT_OUT).suit-manifest"
	@echo "  $(SUIT_OUT).suit-tbs"

suit-envelope: $(SUIT_EMIT_BIN)
	@if [ -z "$(SUIT_SIG)" ]; then \
	  echo "suit-envelope: name go-xkms's signature over $(SUIT_OUT).suit-tbs: SUIT_SIG=<file>"; \
	  exit 1; \
	fi
	@$(SUIT_EMIT_BIN) envelope $(SUIT_OUT).suit-manifest $(SUIT_SIG) $(SUIT_OUT).suit
	@echo "  $(SUIT_OUT).suit"

.PHONY: suit-tbs suit-envelope

# The trailer's to-be-signed bytes, and the signed images (row ota; ADR decision 2): what every tfw-boot row reads before it measures.
IMAGE_SEAL_BIN  := $(BUILD_DIR)/tools/image-seal-$(BOARD)
IMAGE_SEAL_SRCS := tools/image-seal/main.c tools/image-seal/seal.c \
                   boot/boot_image.c boot/boot_platform.c src/crypto/sha256.c \
                   src/proto/crc16.c src/proto/crc16_update.c
IMAGE_SEAL_ROW  := $(filter $(BOARD):%,$(BOOT_SUPPORTED))

$(IMAGE_SEAL_BIN): $(IMAGE_SEAL_SRCS) tools/image-seal/seal.h boot/boot_image.h include/tfw/boot.h \
                   src/core/suit.h $(BOARD_DIR)/board_profile.h include/tfw/config.h
	@mkdir -p $(dir $@)
	@$(HOST_CC) -std=c11 -O1 -Wall -Wextra -Werror -Wconversion \
	   -Iinclude -Iboot -Isrc/core -Isrc/crypto -Isrc/proto -Itools/image-seal -I$(BOARD_DIR) \
	   -DTFW_BOARD_HEADER='"board_profile.h"' -o $@ $(IMAGE_SEAL_SRCS)

image-seal-row:
	@if [ -z "$(IMAGE_SEAL_ROW)" ]; then \
	  echo "image-seal: $(BOARD) has no tfw-boot row in mk/boot.mk's BOOT_SUPPORTED;"; \
	  echo "  nothing on it reads a trailer, so no image is sealed for it."; \
	  exit 1; \
	fi

image-tbs: image-seal-row provenance-one $(IMAGE_SEAL_BIN)
	@if [ -z "$(IMAGE_VERSION)" ]; then \
	  echo "image-tbs: name the image version: IMAGE_VERSION=<n>, at or above the device's floor."; \
	  exit 1; \
	fi
	@$(IMAGE_SEAL_BIN) tbs $(SUIT_OUT).bin $(IMAGE_VERSION) $(IMAGE_FLAGS) $(SUIT_OUT).tbs
	@echo "  $(SUIT_OUT).tbs"

image-seal: image-seal-row $(IMAGE_SEAL_BIN)
	@if [ -z "$(IMAGE_SIG)" ]; then \
	  echo "image-seal: name go-xkms's signature over $(SUIT_OUT).tbs: IMAGE_SIG=<file>"; \
	  exit 1; \
	fi
	@if [ -n "$(MANIFEST_SRCS)" ] && [ ! -f "$(SUIT_OUT).suit" ]; then \
	  echo "image-seal: $(BOARD) $(CRYPTO) checks the SUIT manifest at OTA_ACTIVATE (ruling 121),"; \
	  echo "  and refuses a stream without one: make suit-tbs and suit-envelope first."; \
	  exit 1; \
	fi
	@$(IMAGE_SEAL_BIN) seal $(SUIT_OUT).bin $(SUIT_OUT).tbs $(IMAGE_SIG) \
	   $(SUIT_OUT).stream $(SUIT_OUT).region $(if $(MANIFEST_SRCS),$(SUIT_OUT).suit)
	@echo "  $(SUIT_OUT).stream"
	@echo "  $(SUIT_OUT).region"

# The sealer's test, test/unit/test_image_seal.c: its trailer, signed as go-xkms signs, through tfw-boot's own tfw_boot_evaluate with the p256-soft backend.
$(HOST_DIR)/test_image_seal: test/unit/test_image_seal.c tools/image-seal/seal.c \
        boot/boot_policy.c boot/boot_image.c boot/boot_platform.c boot/measure/measure_soft.c \
        boot/verify/verify_p256_soft.c src/crypto/sha256.c src/proto/crc16.c src/proto/crc16_update.c \
        test/mock/boot_hal_mock.c $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(UECC_HOST_DEFS) -Itools/image-seal \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

# The sealer's command line, test/unit/test_image_seal_cli.c: main.c as make image-tbs and make image-seal run it, included into the suite (its main renamed), so it is a prerequisite and not linked on its own.
$(HOST_DIR)/test_image_seal_cli: test/unit/test_image_seal_cli.c tools/image-seal/main.c tools/image-seal/seal.c \
        boot/boot_policy.c boot/boot_image.c boot/boot_platform.c boot/measure/measure_soft.c \
        boot/verify/verify_p256_soft.c src/crypto/sha256.c src/proto/crc16.c src/proto/crc16_update.c \
        test/mock/boot_hal_mock.c $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(UECC_HOST_DEFS) -Itools/image-seal \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ \
	   $(filter-out %image-seal/main.c,$^) $(EXTRA_LDFLAGS)

.PHONY: image-seal-row image-tbs image-seal
