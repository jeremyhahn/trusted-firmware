# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The Makefile is the contract: CI runs the same targets a developer runs.

.DEFAULT_GOAL := help
.DELETE_ON_ERROR:
MAKEFLAGS += --no-builtin-rules
SHELL := /bin/bash

VERSION := $(shell cat VERSION)

# First: host.mk and firmware.mk both name their outputs after APP_DIR's application, and link the empty runner, app_runner_none.c, when there is none.
include mk/app.mk
include mk/host.mk
include mk/go.mk
# mk/firmware.mk reads the board's profile.mk and includes mk/arch/$(ARCH).mk.
include mk/firmware.mk
# After mk/firmware.mk: tfw-boot is a second image built by the same toolchain, and it reuses ARCH_CC, ARCH_OBJCOPY, ARCH_SIZE, HAL_DIR and the board profile that file establishes.
include mk/boot.mk
# After mk/boot.mk: board-restore writes the stock bootloader through ARCH_BOOT_FLASH's EESAVE guard.
include mk/board-restore.mk
include mk/docker.mk
include mk/integration.mk
# After mk/firmware.mk: the benchmark image is the firmware's own object list with main.c exchanged, so it needs FW_SRCS, FW_CFLAGS and the board profile.
include mk/bench.mk
include mk/release.mk
# After mk/release.mk: the SUIT manifest's to-be-signed bytes are emitted beside the image provenance-one writes.
include mk/suit.mk
# After mk/boot.mk and mk/suit.mk, for BOOT_TARGET and SUIT_OUT: the bench Mega's development-only signing key, kept outside the tree.
include mk/boot-bench.mk
# After mk/firmware.mk, for FW_CFLAGS, HAL_DIR and the board profile.
include mk/wiznet-probe.mk
# After mk/firmware.mk and mk/go.mk: the bench bake image is built with the firmware's flags and HAL, around a block tfwprov writes.
include mk/platform-bake.mk
# An architecture's own targets, for any invocation whatever its BOARD: those whose image a different build system produces (mk/arch/esp32-targets.mk: ESP-IDF) and the emulator suites (mk/arch/cortex-m-targets.mk: QEMU).
include $(wildcard mk/arch/*-targets.mk)
# OP-TEE on QEMU virt for go-enclave's arm-trustzone backend: the upstream release pin and the targets that fetch, build and boot it, under a cache outside the checkout.
include mk/optee.mk
# Last: the five CI stages are made of the targets every file above defines.
include mk/stages.mk

## help: list every target with its description
help:
	@grep -h '^## [A-Za-z0-9_.-]*:' $(MAKEFILE_LIST) | sed 's/^## //' | sort | awk -F': ' '{printf "  \033[36m%-22s\033[0m %s\n", $$1, $$2}'

## version: print the project version
version:
	@echo $(VERSION)

## clean: remove every build artefact
clean:
	@[ ! -d $(BUILD_DIR) ] || find $(BUILD_DIR) -mindepth 1 -maxdepth 1 ! -path $(BUILD_DIR)/docker ! -path $(BUILD_DIR)/records -exec rm -rf {} +
	@rm -rf coverage
	@echo "cleaned"

## verify-vendor: check every vendored dependency against its recorded hash
verify-vendor:
	@fail=0; \
	while read -r want file; do \
	  [ -n "$$want" ] || continue; \
	  path=$$(find test/third_party src/third_party -name "$$file" 2>/dev/null | head -1); \
	  if [ -z "$$path" ]; then echo "MISSING: $$file"; fail=1; continue; fi; \
	  got=$$(sha256sum "$$path" | cut -d' ' -f1); \
	  if [ "$$got" != "$$want" ]; then echo "HASH MISMATCH: $$path"; fail=1; \
	  else echo "  ok  $$path"; fi; \
	done < <(grep -oE '^    [0-9a-f]{64}  [A-Za-z0-9_.-]+$$' test/third_party/MANIFEST.md | sed 's/^    //'); \
	listed=$$(grep -cE '^    [0-9a-f]{64}  ' test/third_party/MANIFEST.md); \
	present=$$(find test/third_party src/third_party -type f \( -name '*.c' -o -name '*.h' -o -name '*.inc' -o -name 'LICENSE*' \) 2>/dev/null | wc -l); \
	if [ "$$listed" -ne "$$present" ]; then \
	  echo "UNRECORDED: $$present vendored files present, $$listed recorded in MANIFEST.md"; fail=1; fi; \
	exit $$fail

## ci: everything the CI gate requires: the five stages, in order
ci:
	@$(MAKE) --no-print-directory build
	@$(MAKE) --no-print-directory test
	@$(MAKE) --no-print-directory integration-test-host
	@$(MAKE) --no-print-directory security
	@echo "CI gate: green"

.PHONY: help version clean verify-vendor ci firmware-attested releasability releasability-test release-alpha in-toolchain-image-test

## releasability: no private-fleet value in any file that would be published
releasability:
	@scripts/releasability.sh

## releasability-test: the releasability gate fails on a plant, and export-ignore holds
releasability-test:
	@scripts/releasability-test.sh

## in-toolchain-image-test: scripts/in-toolchain-image.sh caps the stage's CPU bound at the daemon's NCPU
in-toolchain-image-test:
	@bash scripts/in-toolchain-image-test.sh
scripts-test: in-toolchain-image-test

## release-alpha: cut and push the next -alpha tag (vX.Y.Z-alpha and sdk/go/vX.Y.Z-alpha) to dev; DRY_RUN=1 prints them
DRY_RUN ?=
release-alpha:
	@python3 -B scripts/release_alpha.py $(if $(filter 1,$(DRY_RUN)),--dry-run)

## firmware-attested: build the DICE-HW image, which is a different image
firmware-attested:
	@$(MAKE) --no-print-directory firmware CRYPTO=atecc608a
