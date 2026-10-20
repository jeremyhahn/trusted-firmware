# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# sdk/go is a real Go module and carries the FULL Go gate set -- gofmt, vet,
# govulncheck, `go mod verify`. The substitution table in the plan applies to
# the C firmware, where those tools have no meaning; it does not excuse the Go
# half of the repository from the standards every other Go module here meets.

GO       ?= go
SDK_DIR  := sdk/go

# GO_BUILD_TAGS is the CI template's tag set (devcontainer.md §6), space
# separated: ci.yml passes it to every stage it calls. go-tags joins a module's
# own tags (its second argument) with it into the deduplicated comma list the Go
# tools take, behind the tool's flag (the first): $(call go-tags,-tags,a) is
# " -tags a,b" with GO_BUILD_TAGS=b. With no tag at all it is nothing, so an
# unset or empty GO_BUILD_TAGS leaves every command line as it was. Every Go
# build, test, vet and scanner below takes its tags from here, and
# go-tags-selftest holds them to it.
GO_BUILD_TAGS ?=
go_tags_empty :=
go_tags_space := $(go_tags_empty) $(go_tags_empty)
go_tags_comma := ,
go-tag-list = $(subst $(go_tags_space),$(go_tags_comma),$(sort $(1) $(GO_BUILD_TAGS)))
go-tags = $(if $(call go-tag-list,$(2)), $(1) $(call go-tag-list,$(2)))

# go-cover-floor prints a `go test -cover` log ($(2)) and fails if any one
# package in it is under the floor ($(1)). The owner's standard is 90 % per
# package (m655, #163), so a well-covered package must not carry a thin one
# past a floor taken on the module's total. A package with no statements has
# no percentage and is not counted.
go-cover-floor = ( low=0; while IFS= read -r l; do printf '%s\n' "$$l"; \
  case $$l in *'coverage: '*'% of statements'*) p=$${l\#\#*coverage: }; p=$${p%%.*}; \
    if [ "$$p" -lt "$(1)" ]; then echo "package under the $(1)% floor (m655)"; low=1; fi ;; esac; \
  done < $(2); exit $$low )

## sdk-build: build the Go SDK and its commands
sdk-build:
	@cd $(SDK_DIR) && $(GO) build$(call go-tags,-tags) ./...
	@echo "sdk/go builds"

## sdk-test: run the Go tests, including the cross-implementation suite
# Depends on both simulators: the cross-implementation tests talk to the REAL
# firmware compiled for the host, so a stale or missing binary would leave the
# only check that the two codecs agree silently untested. tfwsim-atecc608a is the
# element tier, the only peer that produces DICE evidence; tfwsim-app-example the
# only one with an application, so the only one that answers APP_INFO;
# tfwsim-auto the Mega's default tier, which picks one of those two at startup.
# Every test that starts a simulator carries the tfwsim build tag (#163), so
# this is the one target that compiles them in.
sdk-test: tfwsim tfwsim-atecc608a tfwsim-auto tfwsim-app-example
	@cd $(SDK_DIR) && $(GO) test -count=1$(call go-tags,-tags,tfwsim) ./...

## sdk-coverage: Go SDK unit-test statement coverage, failing if it or any package is under the floor
# Unit tests only (#163): without the tfwsim tag no simulator-driven test is
# compiled, so the floor is what the unit tests reach on their own and no
# simulator is built for it. sdk-test runs the cross-implementation suite.
SDK_COVERAGE_FLOOR ?= 90
sdk-coverage:
	@mkdir -p coverage
	@cd $(SDK_DIR) && { $(GO) test -count=1$(call go-tags,-tags) -coverprofile=../../coverage/sdk.out ./... \
	    > ../../coverage/sdk-packages.txt 2>&1; rc=$$?; \
	  $(call go-cover-floor,$(SDK_COVERAGE_FLOOR),../../coverage/sdk-packages.txt) && [ $$rc -eq 0 ]; } && \
	  total=$$($(GO) tool cover -func=../../coverage/sdk.out | tail -n 1 | tr -s ' \t' ' ' | cut -d' ' -f3 | tr -d '%') && \
	  echo "sdk/go: $$total% statement coverage (floor $(SDK_COVERAGE_FLOOR)%)" && \
	  if [ "$${total%.*}" -lt "$(SDK_COVERAGE_FLOOR)" ]; then \
	    echo "sdk/go coverage is below the floor"; exit 1; \
	  fi

## hw-test: run the cross-implementation suite against a real board (BOARD=, PORT=, ETH=)
# The same tfwctl TestFirmware* scenarios sdk-test runs against tfwsim, pointed
# at the board on PORT through their one transport seam, sdk/go/internal/peer.
# BOARD tells the scenarios which board's pins to expect, and CAPS must report
# that board. Not part of `make ci`: CI has no board. A missing PORT, a port that
# does not exist and a board that does not answer all fail; none of them is a
# skip. Flash the image first (`make flash`); the scenarios do not. -p 1 because
# every scenario opens, and so resets, the one board. Every TestFirmware*
# scenario reaches its peer through that seam; a test that starts a simulator
# itself is named otherwise, so none needs a host binary here. They live in
# sdk/go/cmd/tfwctl's e2e_firmware, e2e_dice, e2e_auth, e2e_attestcsr, e2e_ota
# and e2e_app test files, which carry the tfwsim build tag; e2e_peer_test.go is
# the seam. Each row checks the tier it needs and names it when the board's
# differs, so one command runs them all and a row for another image fails, not
# skips: docs/development/testing.md tables which board each row needs.
#
# ETH adds the TestEthernet* rows (e2e_ethernet_test.go, and the staging rows in
# e2e_ota_test.go): the ip:port on the bench host that the board's network
# block dials, which the rows listen on. It has no default: the address is the
# bench's, and none is written here. The identity rows also take the board's
# DeviceID key, DEVICEID (the hex X||Y, as hw-enroll takes it), and
# CRI_DIGEST, the hex SHA-256 of the CertificationRequestInfo the board signs
# for the CSR rows, which go-xkms reproduces and tfwctl does not. The auth and
# OTA rows take OPERATOR_KEY, the PEM file of the operator key the board was
# provisioned to trust.
HW_TEST_RUN ?= ^TestFirmware$(if $(ETH),|^TestEthernet)
hw-test:
	@if [ -z "$(PORT)" ]; then echo "hw-test: name the board's serial port: make hw-test BOARD=<board> PORT=<dev> [ETH=<ip:port the board dials>]"; exit 1; fi
	@if [ ! -e "$(PORT)" ]; then echo "hw-test: $(PORT) does not exist -- is the board plugged in?"; exit 1; fi
	@cd $(SDK_DIR) && TFW_HW_PORT=$(PORT) TFW_HW_BOARD=$(BOARD) TFW_HW_ETH='$(ETH)' TFW_HW_DEVICEID='$(DEVICEID)' TFW_HW_CRI_DIGEST='$(CRI_DIGEST)' TFW_HW_OPERATOR_KEY='$(if $(OPERATOR_KEY),$(abspath $(OPERATOR_KEY)))' $(GO) test -count=1$(call go-tags,-tags,tfwsim) -p 1 -run '$(HW_TEST_RUN)' ./cmd/tfwctl

## sdk-lint: gofmt and vet the Go SDK
# vet compiles the tests, so it downloads their modules. That download goes
# first, through deps-check.py's bounded retry: a proxy that is down for a
# moment is asked again, a module that is wrong or missing fails at once, and
# gofmt and vet then run once, from the cache, so a real finding fails first time.
sdk-lint:
	@python3 scripts/deps-check.py download $(SDK_DIR)
	@cd $(SDK_DIR) && out=$$($(GO) fmt ./... ) && \
	  if [ -n "$$out" ]; then echo "gofmt rewrote:"; echo "$$out"; exit 1; fi
	@cd $(SDK_DIR) && $(GO) vet$(call go-tags,-tags) ./...
	@cd $(SDK_DIR) && $(GO) vet$(call go-tags,-tags,tfwsim) ./...
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

## xkms-test: run the xkms adapter's tests, all but the e2e package
# Depends on both simulators for the reason sdk-test does: the adapter's tests
# talk to the REAL firmware compiled for the host, never a downloaded binary.
# ./e2e is left out: it is TestHardwareEnrollment's package, which hw-enroll runs
# against a board (#163 decision 2). Out of the list, not skipped inside it.
xkms-test: tfwsim tfwsim-atecc608a
	@cd $(XKMS_DIR) && TFWSIM_DIR=$(XKMS_SIM_DIR) $(GO) test -count=1$(call go-tags,-tags,$(XKMS_TAGS)) \
	  $$($(GO) list$(call go-tags,-tags,$(XKMS_TAGS)) ./... | grep -v '/e2e$$')

## xkms-coverage: xkms adapter statement coverage, failing if it or any package is under the floor
xkms-coverage: tfwsim tfwsim-atecc608a
	@mkdir -p coverage
	@cd $(XKMS_DIR) && { TFWSIM_DIR=$(XKMS_SIM_DIR) $(GO) test -count=1$(call go-tags,-tags,$(XKMS_TAGS)) \
	  -coverprofile=../coverage/xkms.out ./... > ../coverage/xkms-packages.txt 2>&1; rc=$$?; \
	  $(call go-cover-floor,$(XKMS_COVERAGE_FLOOR),../coverage/xkms-packages.txt) && [ $$rc -eq 0 ]; } && \
	  total=$$($(GO) tool cover -func=../coverage/xkms.out | tail -n 1 | tr -s ' \t' ' ' | cut -d' ' -f3 | tr -d '%') && \
	  echo "xkms: $$total% statement coverage (floor $(XKMS_COVERAGE_FLOOR)%)" && \
	  if [ "$${total%.*}" -lt "$(XKMS_COVERAGE_FLOOR)" ]; then \
	    echo "xkms coverage is below the floor"; exit 1; \
	  fi

## hw-enroll: enroll one board through device-attest-01 and write its run record (#7)
# NOT in `ci`, for the reason bench-check is not: it needs a board on the bench.
# The harness it runs is xkms/e2e TestHardwareEnrollment; `xkms-test` leaves
# its package out (#163 decision 2).
# DEVICE is the board; there is no default and no fallback to the simulator,
# because a hardware run that cannot reach hardware is a failure. Its form picks
# the transport: a host:port is TCP, the address this host listens on for the
# board to dial (CONNECT_WAIT bounds the wait); anything else is a serial port.
# DEVICEID is the hex X||Y tfwprov --commit printed, for the in-process ACME
# server; ACME_DIRECTORY names a control plane instead, ACME_ROOTS the PEM its
# TLS chains to. Procedure: docs/development/hardware-enrollment.md.
HW_ENROLL_RECORDS ?= docs/development/enrollment-runs
hw-enroll:
	@if [ -z "$(DEVICE)" ]; then \
	  echo "hw-enroll: set DEVICE=<serial port> or DEVICE=<listen host:port>, e.g. make hw-enroll DEVICE=/dev/ttyACM0 DEVICEID=<hex X||Y>"; \
	  echo "           or DEVICE=<port> ACME_DIRECTORY=<url> [ACME_ROOTS=<pem>] (docs/development/hardware-enrollment.md)"; \
	  exit 1; \
	fi
	@mkdir -p $(HW_ENROLL_RECORDS)
	@cd $(XKMS_DIR) && TFW_HW_DEVICE='$(DEVICE)' TFW_HW_DEVICEID='$(DEVICEID)' TFW_HW_BOOT_WAIT='$(BOOT_WAIT)' TFW_HW_CONNECT_WAIT='$(CONNECT_WAIT)' \
	  TFW_HW_ACME_DIRECTORY='$(ACME_DIRECTORY)' TFW_HW_ACME_ROOTS='$(if $(ACME_ROOTS),$(abspath $(ACME_ROOTS)))' \
	  TFW_HW_RECORD_DIR=$(abspath $(HW_ENROLL_RECORDS)) \
	  $(GO) test -count=1$(call go-tags,-tags,$(XKMS_TAGS)) -run '^TestHardwareEnrollment$$' -v ./e2e/

## go-tags-selftest: every Go invocation the CI stages reach carries GO_BUILD_TAGS; empty changes none
# The boot bridge runs on integration-test-boot's store row; its AVR
# prerequisites are taken as built, since a dry run cannot link them.
go-tags-selftest:
	@GO_TAGS_BRIDGE='BOARD=$(BOOT_INT_BOARD) $(BOOT_INT_STORE_ROW) ETH=0 OTA_STAGE=1 CRYPTO=soft' \
	 GO_TAGS_BRIDGE_PREREQS="$$($(MAKE) --no-print-directory -s go-tags-bridge-prereqs \
	   BOARD=$(BOOT_INT_BOARD) $(BOOT_INT_STORE_ROW) ETH=0 OTA_STAGE=1 CRYPTO=soft)" \
	 TFWPROV_BIN='$(TFWPROV_BIN)' ./scripts/go-tags-selftest.sh

go-tags-bridge-prereqs:
	@echo $(BOOT_INT_DIR)/boot_harness $(BOOT_TARGET).elf $(TARGET).elf

## verify-vectors: check the C and Go wire vectors carry identical values
verify-vectors:
	@./scripts/verify-vectors.sh

.PHONY: sdk-build sdk-test sdk-coverage hw-test sdk-lint sdk-verify verify-vectors xkms-test xkms-coverage hw-enroll \
        go-tags-selftest go-tags-bridge-prereqs
