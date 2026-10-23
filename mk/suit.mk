# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The SUIT manifest's to-be-signed bytes (issue #78; ADR decision 3).
#
#   make suit-tbs BOARD=atmega2560 CRYPTO=atecc608a IMAGE_VERSION=<n>
#       writes, beside $(BOARD)-$(CRYPTO).bin in $(REL_DIR):
#         .suit-manifest  the bstr-wrapped manifest
#         .suit-tbs       the COSE Sig_structure go-xkms signs with ES256
#   make suit-envelope BOARD=... CRYPTO=... SUIT_SIG=<64-byte r||s file>
#         .suit           the envelope, sent after the image in OTA_DATA
#
# NO SIGNING HERE. The key is go-xkms's (decision 10); this tree emits the bytes
# to sign and assembles what comes back.
#
# IMAGE_VERSION is required and has no default, for the reason SOURCE_COMMIT
# has none in mk/release.mk: the manifest's sequence number must be the
# trailer's image_version or the device's gate refuses it, and a default would
# make a mismatch look like a value.
#
# The emitter is a host tool built per board, against that board's profile, so
# its vendor and class ids are the ones the device's gate compares against.

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

# The trailer's to-be-signed bytes, and the signed images (row ota; ADR
# decision 2): what every tfw-boot row reads before it measures.
#
#   make image-tbs BOARD=atmega2560 CRYPTO=atecc608a IMAGE_VERSION=<n> [IMAGE_FLAGS=0x01]
#       writes, beside $(BOARD)-$(CRYPTO).bin in $(REL_DIR):
#         .tbs     trailer v1's 48-byte signed span, board id in byte 6, that
#                  go-xkms signs with ES256
#   make image-seal BOARD=... CRYPTO=... IMAGE_SIG=<64-byte r||s file>
#         .stream  the image and trailer padded to 256-byte pages, trailer
#                  last: what `tfwctl ota-stage` sends. On a board that
#                  checks the manifest at OTA_ACTIVATE (OTA_MANIFEST := 1,
#                  element tier; ruling 121) suit-envelope's .suit goes after
#                  the image, a little-endian u16 length then the envelope,
#                  and image-seal refuses to run without it
#         .region  the application region, trailer at its end: what tfw-boot's
#                  serial session and an ISP write take
#
# The same IMAGE_VERSION as suit-tbs, for the reason given there. IMAGE_FLAGS
# is mk/boot.mk's, 0x01 marking a development image. NO SIGNING HERE either.
#
# A board with no tfw-boot row in mk/boot.mk's BOOT_SUPPORTED (the ATtiny85)
# has nothing that reads a trailer, so it gets no span to sign.
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

# The sealer's test, test/unit/test_image_seal.c: its trailer, signed as
# go-xkms signs, through tfw-boot's own tfw_boot_evaluate with the p256-soft
# backend. The ATmega2560's profile, the soft row's board. An explicit link,
# so mk/host.mk's pattern rule for test/unit/ does not apply.
$(HOST_DIR)/test_image_seal: test/unit/test_image_seal.c tools/image-seal/seal.c \
        boot/boot_policy.c boot/boot_image.c boot/boot_platform.c boot/measure/measure_soft.c \
        boot/verify/verify_p256_soft.c src/crypto/sha256.c src/proto/crc16.c src/proto/crc16_update.c \
        test/mock/boot_hal_mock.c $(BOOT_TEST_DIR)/uecc.o $(HOST_DIR)/unity.o | $(HOST_DIR)
	@$(HOST_CC) $(BOOT_TEST_2560_CFLAGS) $(UECC_HOST_DEFS) -Itools/image-seal \
	   -Isrc/third_party/micro-ecc $(EXTRA_CFLAGS) -o $@ $^ $(EXTRA_LDFLAGS)

.PHONY: image-seal-row image-tbs image-seal
