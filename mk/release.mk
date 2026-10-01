# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Release artifacts, and the one that matters is the provenance manifest.
#
# FWHash is SIGNED EVIDENCE. A device attests to a SHA-256 of its own
# application flash, and a verifier decides whether to trust it partly on
# whether that hash matches a known-good image. For that to mean anything the
# verifier must be able to obtain the expected value -- and better, recompute it
# from source, which is what `make reproducible-check` exists to make possible.
#
# So this target does not package a build. It records what the build IS, in a
# form a verifier can act on without having the build machine.

REL_DIR := $(BUILD_DIR)/release

## provenance: record what each image is, so a verifier has a golden measurement
#
# One manifest per board and crypto tier, because those are different firmware
# with different measurements -- the same reason the tier is part of the build
# path. Emitted as JSON because the consumer is go-trusted-platform, not a
# person.
# Tiers are asked for PER BOARD, not taken from a global list.
#
# This used to expand $(FIRMWARE_TIERS), which stopped existing when tiers
# became per-board -- the ATtiny85 has no I2C, so it cannot build the element
# tier at all. `for t in ; do` is a legal empty loop, so `make provenance`
# printed its success line, wrote ZERO manifests and exited 0: a target whose
# entire purpose is to stop a record being vacuous, being vacuous.
#
# It also could not be caught by `make ci`, because provenance is not in it.
# It is now, with a count check, so an empty run fails.
provenance:
	@mkdir -p $(REL_DIR)
	@n=0; \
	for b in $(FIRMWARE_BOARDS); do \
	  for t in $$($(MAKE) -s board-tiers BOARD=$$b); do \
	    $(MAKE) --no-print-directory provenance-one BOARD=$$b CRYPTO=$$t \
	        SOURCE_COMMIT=$(SOURCE_COMMIT) || exit 1; \
	    n=$$((n+1)); \
	  done; \
	done; \
	if [ "$$n" -eq 0 ]; then \
	  echo "provenance: wrote NO manifests. An empty release record is worse"; \
	  echo "  than none -- it looks like provenance and proves nothing."; \
	  exit 1; \
	fi; \
	echo "provenance: $$n manifest(s) in $(REL_DIR)"

# SOURCE_COMMIT is REQUIRED and is not allowed to default to "unknown".
#
# The whole point of this manifest is that a verifier can rebuild the image and
# get the same bytes. A manifest that does not say which source produced it
# cannot be used for that, and one that says "unknown" looks like a record while
# being useless -- the same failure as a gate that passes vacuously. Inside the
# CI container there is no .git, so the value is passed in.
# Resolved from the checkout, or from the record the CI image carries.
#
# The image has no .git -- the source is COPYed in, not cloned -- so a build
# inside it cannot ask git. /etc/ci-source-commit is written at image build for
# the same reason /etc/ci-toolchain.txt is: a container that cannot say what it
# is built from cannot produce provenance, and provenance is the one thing this
# target exists for. Still refuses rather than defaulting if neither is there.
SOURCE_COMMIT ?= $(shell git rev-parse HEAD 2>/dev/null || cat /etc/ci-source-commit 2>/dev/null)

provenance-one: $(TARGET).elf $(TARGET).hex
	@mkdir -p $(REL_DIR)
	@if [ -z "$(SOURCE_COMMIT)" ]; then 	  echo "provenance: SOURCE_COMMIT is empty and no .git is reachable."; 	  echo "  A manifest that cannot name its source cannot be rebuilt from, which is"; 	  echo "  the only thing it is for. Pass it: make provenance SOURCE_COMMIT=\$$(git rev-parse HEAD)"; 	  exit 1; 	fi
	@# THE MEASUREMENT, which is the field a verifier actually needs.
	@# Taken over the contiguous application binary, which is exactly the span
	@# [0, image_len) that ag_boot_measure walks and that a signing tool writes
	@# into the trailer. Emitting only file hashes -- and a note saying a
	@# verifier wants a different number -- documented the omission instead of
	@# closing it.
	@#
	@# -R .eeprom only, not -R .handoff too: mk/handoff.ld (linked into
	@# $(TARGET).elf already) makes the CDI handoff's SRAM section genuinely
	@# NOLOAD, so it carries no bytes here to exclude -- verified below rather
	@# than assumed, because this .bin IS the FWHash a verifier recomputes, and
	@# a device's own measurement covers application flash only.
	@$(OBJCOPY) -O binary -R .eeprom $(TARGET).elf $(REL_DIR)/$(BOARD)-$(CRYPTO).bin
	@img_len_check=$$(stat -c %s $(REL_DIR)/$(BOARD)-$(CRYPTO).bin); \
	 if [ "$$img_len_check" -gt "$(FLASH_TOTAL)" ]; then \
	   echo "provenance: $(BOARD)-$(CRYPTO).bin is $$img_len_check bytes,"; \
	   echo "  past this part's $(FLASH_TOTAL)-byte flash -- a non-application"; \
	   echo "  section (SRAM, EEPROM) rode into the measured image. This must"; \
	   echo "  cover application flash only; it is what a verifier recomputes"; \
	   echo "  FWHash from."; \
	   exit 1; \
	 fi
	@sha_elf=$$(sha256sum $(TARGET).elf | cut -d' ' -f1); \
	 fwhash=$$(sha256sum $(REL_DIR)/$(BOARD)-$(CRYPTO).bin | cut -d' ' -f1); \
	 img_len=$$(stat -c %s $(REL_DIR)/$(BOARD)-$(CRYPTO).bin); \
	 sha_hex=$$(sha256sum $(TARGET).hex | cut -d' ' -f1); \
	 flash=$$($(SIZE) --format=berkeley $(TARGET).elf | awk 'NR==2{print $$1+$$2}'); \
	 ram=$$($(SIZE)  --format=berkeley $(TARGET).elf | awk 'NR==2{print $$2+$$3}'); \
	 printf '{\n' > $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "board": "%s",\n'            "$(BOARD)"   >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "crypto_tier": "%s",\n'      "$(CRYPTO)"  >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "mcu": "%s",\n'              "$(MCU)"     >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "f_cpu_hz": %s,\n'           "$(F_CPU)"   >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "version": "%s",\n'          "$$(cat VERSION)" >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "source_commit": "%s",\n'    "$(SOURCE_COMMIT)" >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "toolchain": "%s",\n'        "$$($(CC_AVR) -dumpversion)" >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "flash_bytes": %s,\n'        "$$flash"    >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "ram_bytes": %s,\n'          "$$ram"      >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "elf_sha256": "%s",\n'       "$$sha_elf"  >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "hex_sha256": "%s",\n'       "$$sha_hex"  >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "reproducible": true,\n'                  >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "image_len": %s,\n'         "$$img_len"  >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "fwhash_sha256": "%s",\n'   "$$fwhash"   >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '  "note": "fwhash_sha256 is SHA-256 over [0, image_len) of the application binary -- the value a device attests and a verifier compares, and the value a signing tool writes into the image trailer. elf_sha256 and hex_sha256 identify the ARTEFACT and are different numbers; comparing one of those against device evidence would report a good device as tampered with."\n' >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json; \
	 printf '}\n'                                        >> $(REL_DIR)/$(BOARD)-$(CRYPTO).json
	@echo "  $(REL_DIR)/$(BOARD)-$(CRYPTO).json"

## sbom: what this image is built from, by content hash
#
# A C project has no package manager, so an SBOM here is the vendored manifest
# plus the toolchain -- which is exactly what `make verify-vendor` already
# checks. Emitting it separately means a consumer does not have to parse a
# Markdown table to learn what is inside an image.
sbom:
	@mkdir -p $(REL_DIR)
	@printf '{\n  "format": "arduino-generic/sbom/v1",\n' > $(REL_DIR)/sbom.json
	@printf '  "version": "%s",\n' "$$(cat VERSION)" >> $(REL_DIR)/sbom.json
	@printf '  "toolchain": { "avr_gcc": "%s", "libc": "avr-libc" },\n' \
	   "$$($(CC_AVR) -dumpversion)" >> $(REL_DIR)/sbom.json
	@printf '  "vendored": [\n' >> $(REL_DIR)/sbom.json
	@grep -oE '^    [0-9a-f]{64}  [A-Za-z0-9_.-]+$$' test/third_party/MANIFEST.md \
	   | sed 's/^    //' \
	   | awk '{printf "    { \"file\": \"%s\", \"sha256\": \"%s\" }%s\n", $$2, $$1, (NR==n?"":",")}' n=$$(grep -cE '^    [0-9a-f]{64}  ' test/third_party/MANIFEST.md) \
	   >> $(REL_DIR)/sbom.json
	@printf '  ]\n}\n' >> $(REL_DIR)/sbom.json
	@echo "sbom: $(REL_DIR)/sbom.json"

.PHONY: provenance provenance-one sbom
