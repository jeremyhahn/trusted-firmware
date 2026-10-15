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
