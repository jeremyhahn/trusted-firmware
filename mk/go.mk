# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# sdk/go is a real Go module and carries the FULL Go gate set -- gofmt, vet,
# govulncheck, `go mod verify`. The substitution table in the plan applies to
# the C firmware, where those tools have no meaning; it does not excuse the Go
# half of the repository from the standards every other Go module here meets.

GO       ?= go
SDK_DIR  := sdk/go

## sdk-build: build the Go SDK and its commands
sdk-build:
	@cd $(SDK_DIR) && $(GO) build ./...
	@echo "sdk/go builds"

## sdk-test: run the Go tests, including the cross-implementation suite
# Depends on both simulators: the cross-implementation tests talk to the REAL
# firmware compiled for the host, so a stale or missing binary would leave the
# only check that the two codecs agree silently untested. tfwsim-atecc608a is the
# element tier, the only peer that produces DICE evidence; tfwsim-app-example the
# only one with an application, so the only one that answers APP_INFO.
sdk-test: tfwsim tfwsim-atecc608a tfwsim-app-example
	@cd $(SDK_DIR) && $(GO) test -count=1 ./...

## hw-test: run the cross-implementation suite against a real board (BOARD=, PORT=)
# The same TestAgainstFirmware* suite sdk-test runs against tfwsim, pointed at
# the board on PORT through the suite's one transport seam,
# sdk/go/internal/peer. BOARD tells the suite which board's pins to expect, and
# CAPS must report that board. Not part of `make ci`: CI has no board. A missing
# PORT, a port that does not exist and a board that does not answer all fail;
# none of them is a skip. Flash the image first (`make flash`); the suite does
# not. -p 1 because every test opens, and so resets, the one board.
HW_TEST_RUN ?= ^TestAgainstFirmware
hw-test:
	@if [ -z "$(PORT)" ]; then echo "hw-test: name the board's serial port: make hw-test BOARD=<board> PORT=<dev>"; exit 1; fi
	@if [ ! -e "$(PORT)" ]; then echo "hw-test: $(PORT) does not exist -- is the board plugged in?"; exit 1; fi
	@cd $(SDK_DIR) && TFW_HW_PORT=$(PORT) TFW_HW_BOARD=$(BOARD) $(GO) test -count=1 -p 1 -run '$(HW_TEST_RUN)' .

## sdk-lint: gofmt and vet the Go SDK
sdk-lint:
	@cd $(SDK_DIR) && out=$$($(GO) fmt ./... ) && \
	  if [ -n "$$out" ]; then echo "gofmt rewrote:"; echo "$$out"; exit 1; fi
	@cd $(SDK_DIR) && $(GO) vet ./...
	@echo "sdk/go: gofmt and vet clean"

## sdk-verify: check the module graph carries no in-house dependency
# THE LAYER 0 INVARIANT, enforced rather than trusted. This module is imported
# by go-xkms (Layer 3) and go-trusted-platform (Layer 5); a dependency on
# anything in the stack would invert the graph and make it unimportable by the
# very projects it exists for. The check is one grep because the property is
# one property.
sdk-verify:
	@cd $(SDK_DIR) && $(GO) mod verify >/dev/null
	@if cd $(SDK_DIR) && $(GO) list -m all | grep -E 'jeremyhahn/(go-|xkey)' | grep -v 'trusted-firmware'; then \
	  echo "LAYER 0 VIOLATION: sdk/go depends on an in-house module (see above)"; exit 1; \
	fi
	@echo "sdk/go: zero in-house dependencies -- Layer 0 invariant holds"

# xkms/ is a SEPARATE module from sdk/go and sits above it: it adapts the AVR
# device to go-xkms's backend interfaces, so it imports go-xkms (Layer 3) and
# the SDK (Layer 0), which sdk/go itself must never do. It requires a TAGGED
# sdk/go, not this tree, so the SDK's source sits in the read-only module cache
# and the simulators cannot be found relative to it: the targets below hand the
# test the built binaries' directory through TFWSIM_DIR.
#
# codec_json is go-xkms's build tag for its ACME client, which the end-to-end
# test drives; without it that test would silently not be compiled.
XKMS_DIR            := xkms
XKMS_TAGS           ?= codec_json
XKMS_COVERAGE_FLOOR ?= 90
XKMS_SIM_DIR        ?= $(abspath build/host)

## xkms-test: run the xkms adapter's tests, including the end-to-end test
# Depends on both simulators for the reason sdk-test does: the end-to-end test
# enrolls the REAL firmware compiled for the host, never a downloaded binary.
xkms-test: tfwsim tfwsim-atecc608a
	@cd $(XKMS_DIR) && TFWSIM_DIR=$(XKMS_SIM_DIR) $(GO) test -count=1 -tags $(XKMS_TAGS) ./...

## xkms-coverage: xkms adapter statement coverage, failing under the floor
xkms-coverage: tfwsim tfwsim-atecc608a
	@mkdir -p coverage
	@cd $(XKMS_DIR) && TFWSIM_DIR=$(XKMS_SIM_DIR) $(GO) test -count=1 -tags $(XKMS_TAGS) \
	  -coverprofile=../coverage/xkms.out ./... && \
	  total=$$($(GO) tool cover -func=../coverage/xkms.out | tail -n 1 | tr -s ' \t' ' ' | cut -d' ' -f3 | tr -d '%') && \
	  echo "xkms: $$total% statement coverage (floor $(XKMS_COVERAGE_FLOOR)%)" && \
	  if [ "$${total%.*}" -lt "$(XKMS_COVERAGE_FLOOR)" ]; then \
	    echo "xkms coverage is below the floor"; exit 1; \
	  fi

## verify-vectors: check the C and Go wire vectors carry identical values
verify-vectors:
	@./scripts/verify-vectors.sh

.PHONY: sdk-build sdk-test hw-test sdk-lint sdk-verify verify-vectors xkms-test xkms-coverage
