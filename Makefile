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

include mk/host.mk
include mk/go.mk
include mk/avr.mk
# After mk/avr.mk: ag-boot is a second image built by the same toolchain, and it
# reuses CC_AVR, OBJCOPY, SIZE and the board profile that file establishes.
include mk/boot.mk
include mk/docker.mk
include mk/integration.mk

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
	present=$$(find test/third_party src/third_party -type f \( -name '*.c' -o -name '*.h' -o -name 'LICENSE*' \) 2>/dev/null | wc -l); \
	if [ "$$listed" -ne "$$present" ]; then \
	  echo "UNRECORDED: $$present vendored files present, $$listed recorded in MANIFEST.md"; fail=1; fi; \
	exit $$fail

## ci: everything the CI gate requires
# EVERYTHING. `coverage` was missing from this list while the workflow ran it,
# so a coverage failure could only be discovered on the server -- which is the
# same defect as a gate written out in the workflow file instead of in a target,
# and it cost a red main. If CI runs it, it belongs here.
ci: verify-vendor verify-vectors lint sdk-verify sdk-lint test boot-test board-sweep \
    coverage test-asan sdk-test firmware-matrix bootloader-check integration-test \
    reproducible-check
	@echo "CI gate: green"

.PHONY: help version clean verify-vendor ci firmware-attested

## firmware-attested: build the DICE-HW image, which is a different image
# Different provider, different size. The default build would never exercise
# the element path, and the 328P is where it is tightest.
firmware-attested:
	@$(MAKE) --no-print-directory firmware CRYPTO=atecc608a
