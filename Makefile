# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The Makefile is the contract: CI runs the same targets a developer runs.
# Every target carries a `## name: description` line, harvested by `make help`.

.DEFAULT_GOAL := help
.DELETE_ON_ERROR:
MAKEFLAGS += --no-builtin-rules
SHELL := /bin/bash

VERSION := $(shell cat VERSION)

# First: host.mk and firmware.mk both name their outputs after APP_DIR's
# application, and link the empty runner, app_runner_none.c, when there is none.
include mk/app.mk
include mk/host.mk
include mk/go.mk
# mk/firmware.mk reads the board's profile.mk and includes mk/arch/$(ARCH).mk.
include mk/firmware.mk
# After mk/firmware.mk: tfw-boot is a second image built by the same toolchain,
# and it reuses ARCH_CC, ARCH_OBJCOPY, ARCH_SIZE, HAL_DIR and the board profile
# that file establishes.
include mk/boot.mk
include mk/docker.mk
include mk/integration.mk
# After mk/firmware.mk: the benchmark image is the firmware's own object list
# with main.c exchanged, so it needs FW_SRCS, FW_CFLAGS and the board profile.
include mk/bench.mk
include mk/release.mk
# After mk/firmware.mk, for FW_CFLAGS, HAL_DIR and the board profile. A bench
# image only: nothing in `ci` depends on it.
include mk/wiznet-probe.mk
# After mk/firmware.mk and mk/go.mk: the bench bake image (#7) is built with the
# firmware's flags and HAL, around a block tfwprov writes. The image is a bench
# image only; its no-link check is in `ci`.
include mk/platform-bake.mk
# An architecture's own targets, for architectures whose image a different
# build system produces (mk/arch/esp32-targets.mk: ESP-IDF). Never in `ci`.
include $(wildcard mk/arch/*-targets.mk)

## help: list every target with its description
help:
	@grep -h '^## [A-Za-z0-9_.-]*:' $(MAKEFILE_LIST) | sed 's/^## //' | sort | awk -F': ' '{printf "  \033[36m%-22s\033[0m %s\n", $$1, $$2}'

## version: print the project version
version:
	@echo $(VERSION)

## clean: remove every build artefact
clean:
	@rm -rf $(BUILD_DIR) coverage
	@echo "cleaned"

## verify-vendor: check every vendored dependency against its recorded hash
# The substitute for `go mod verify`. A C project has no package manager, so
# the integrity guarantee is written down in test/third_party/MANIFEST.md and
# checked here. An unlisted vendored file fails, because a dependency nobody
# recorded is one nobody is watching.
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

## ci: everything the CI gate requires
# EVERYTHING. `coverage` was missing from this list while the workflow ran it,
# so a coverage failure could only be discovered on the server -- which is the
# same defect as a gate written out in the workflow file instead of in a target,
# and it cost a red main. If CI runs it, it belongs here.
#
# `bench-check` is DELIBERATELY NOT HERE (owner ruling, 2026-09-23). Five of the
# six performance targets are missed by between 3x and 11x, and two of them are
# unreachable as the plan states them. A gate pinned at today's cost would keep
# `make ci` green while the firmware is that far over budget, and a green `ci`
# has to keep meaning what it has always meant. `make bench-check` is run
# deliberately and its numbers are in docs/development/performance.md; the debt
# is tracked in #11, #12 and #13. It goes back in when the rows it measures are
# met, not before.
#
# `xkms-test` and `xkms-coverage` are NOT HERE either, for a different reason:
# the xkms adapter module depends on the private go-xkms module, and the
# toolchain image behind `make docker-ci` has no route to it, so `make ci` there
# would fail on a cold cache for want of a credential that must not be baked
# into a layer. They are their own targets, run by the `xkms` CI job on the
# runner, which is part of the CI Gate. Run `make ci xkms-test xkms-coverage`
# where go-xkms is reachable to run both gates.
ci: verify-vendor verify-vectors releasability releasability-test lint lint-arch-leak-selftest sdk-verify sdk-lint test boot-test board-sweep header-deps-check \
    coverage test-asan sdk-test firmware-matrix bootloader-check provenance integration-test \
    integration-test-boot reproducible-check ci-matrix-check app-sdk-check app-firmware-check \
    platform-bake-no-link-check platform-bake-build-check
	@echo "CI gate: green"

.PHONY: help version clean verify-vendor ci firmware-attested releasability releasability-test

## releasability: no private-fleet value in any file that would be published
# The pattern list is scripts/releasability.patterns, the one place the fleet's
# subnet, hostnames and paths are written down. No network. Rule, classes and
# the export-ignore list: docs/development/releasability.md.
releasability:
	@scripts/releasability.sh

## releasability-test: the releasability gate fails on a plant, and export-ignore holds
# Plants go into throwaway repositories, never into this tree. No skip guard.
releasability-test:
	@scripts/releasability-test.sh

## firmware-attested: build the DICE-HW image, which is a different image
# Different provider, different size. The default build would never exercise
# the element path, and the 328P is where it is tightest.
firmware-attested:
	@$(MAKE) --no-print-directory firmware CRYPTO=atecc608a
