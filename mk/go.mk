# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# sdk/go is a real Go module and carries the FULL Go gate set, gofmt, vet, govulncheck, `go mod verify`.

GO       ?= go
SDK_DIR  := sdk/go

# GO_BUILD_TAGS is the CI template's tag set, space separated: ci.yml passes it to every stage it calls.
GO_BUILD_TAGS ?=
go_tags_empty :=
go_tags_space := $(go_tags_empty) $(go_tags_empty)
go_tags_comma := ,
go-tag-list = $(subst $(go_tags_space),$(go_tags_comma),$(sort $(1) $(GO_BUILD_TAGS)))
go-tags = $(if $(call go-tag-list,$(2)), $(1) $(call go-tag-list,$(2)))

# go-cover-floor prints a `go test -cover` log ($(2)) and fails if any one package in it is under the floor ($(1)).
go-cover-floor = ( low=0; while IFS= read -r l; do printf '%s\n' "$$l"; \
  case $$l in *'coverage: '*'% of statements'*) p=$${l\#\#*coverage: }; p=$${p%%.*}; \
    if [ "$$p" -lt "$(1)" ]; then echo "package under the $(1)% floor (m655)"; low=1; fi ;; esac; \
  done < $(2); exit $$low )

## sdk-build: build the Go SDK and its commands
sdk-build:
	@cd $(SDK_DIR) && $(GO) build$(call go-tags,-tags) ./...
	@echo "sdk/go builds"

## sdk-test: run the Go tests, including the cross-implementation suite
sdk-test: tfwsim tfwsim-atecc608a tfwsim-auto tfwsim-app-example tfwsim-prov tfwsim-otaboot tfwsim-manifest
	@cd $(SDK_DIR) && $(GO) test -count=1$(call go-tags,-tags,tfwsim) ./...

## sdk-coverage: Go SDK unit-test statement coverage, failing if it or any package is under the floor
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

## hw-test: run the cross-implementation suite against a real board (BOARD=, PORT=, HW_ETH=, HW_ETH_LISTEN=, HW_RESET=)
HW_TEST_RUN ?=
HW_RESET_stm32h573i-dk ?= cmd:STM32_Programmer_CLI -c port=SWD sn=$(CM_PROBE_SERIAL) mode=HOTPLUG -rst
hw-test: HW_RESET_DEFAULT := $(if $(HW_RESET)$(filter tcp:% tcp-listen:%,$(PORT)),,$(HW_RESET_$(BOARD)))
hw-test: HW_RESET := $(or $(HW_RESET),$(HW_RESET_DEFAULT))
hw-test:
	@if [ -z "$(PORT)" ]; then echo "hw-test: name the board's serial port: make hw-test BOARD=<board> PORT=<dev> [HW_ETH=<ip:port the board dials> [HW_ETH_LISTEN=<ip:port to listen on, e.g. 0.0.0.0:7020 behind a published port>]]"; exit 1; fi
	@if [ -n "$(HW_RESET_DEFAULT)" ]; then case "$(CM_PROBE_SERIAL)" in \
	    '') echo "hw-test: REFUSED. $(BOARD) resets through its probe: name it, make hw-test BOARD=$(BOARD) PORT=$(PORT) CM_PROBE_SERIAL=<serial>" >&2; exit 1;; \
	    *[!A-Za-z0-9_-]*) echo "hw-test: REFUSED. CM_PROBE_SERIAL '$(CM_PROBE_SERIAL)' is not letters, digits, - and _" >&2; exit 1;; esac; fi
	@if [ -z "$(filter tcp:% tcp-listen:%,$(PORT))" ] && [ ! -e "$(PORT)" ]; then echo "hw-test: $(PORT) does not exist -- is the board plugged in?"; exit 1; fi
	@cd $(SDK_DIR) && TFW_HW_PORT=$(PORT) TFW_HW_BOARD=$(BOARD) TFW_HW_ESP_TARGET='$(ESP_TARGET)' TFW_HW_ETH='$(HW_ETH)' TFW_HW_ETH_LISTEN='$(HW_ETH_LISTEN)' TFW_HW_RESET='$(HW_RESET)' TFW_HW_RESET_PORT='$(RESET_PORT)' TFW_HW_DEVICEID='$(DEVICEID)' TFW_HW_CRI_DIGEST='$(CRI_DIGEST)' TFW_HW_OPERATOR_KEY='$(if $(OPERATOR_KEY),$(abspath $(OPERATOR_KEY)))' TFW_HW_ROWS='$(if $(HW_TEST_RUN),,1)' $(GO) test -count=1$(call go-tags,-tags,tfwsim hwtest) -p 1$(if $(HW_TEST_RUN), -run '$(HW_TEST_RUN)') ./cmd/tfwctl

## api-doc-check: check docs/host-sdk/api.md names every exported symbol of sdk/go
api-doc-check:
	@python3 -B scripts/api-doc-check.py $(CURDIR)

## sdk-lint: gofmt and vet the Go SDK, and check its API page
sdk-lint: api-doc-check
	@python3 scripts/deps-check.py download $(SDK_DIR)
	@cd $(SDK_DIR) && out=$$($(GO) fmt ./... ) && \
	  if [ -n "$$out" ]; then echo "gofmt rewrote:"; echo "$$out"; exit 1; fi
	@cd $(SDK_DIR) && $(GO) vet$(call go-tags,-tags) ./...
	@cd $(SDK_DIR) && $(GO) vet$(call go-tags,-tags,tfwsim) ./...
	@echo "sdk/go: gofmt and vet clean"

## sdk-verify: check the module graph carries no in-house dependency
sdk-verify:
	@cd $(SDK_DIR) && $(GO) mod verify >/dev/null
	@if cd $(SDK_DIR) && $(GO) list -m all | grep -E 'jeremyhahn/(go-|xkey)' | grep -v 'trusted-firmware'; then \
	  echo "LAYER 0 VIOLATION: sdk/go depends on an in-house module (see above)"; exit 1; \
	fi
	@echo "sdk/go: zero in-house dependencies -- Layer 0 invariant holds"

# xkms/ is a separate module from sdk/go and sits above it: it adapts the AVR device to go-xkms's backend interfaces, so it imports go-xkms (Layer 3) and the SDK (Layer 0), which sdk/go itself must never do.
XKMS_DIR            := xkms
XKMS_TAGS           ?= codec_json
XKMS_COVERAGE_FLOOR ?= 90
XKMS_SIM_DIR        ?= $(abspath build/host)

## xkms-test: run the xkms adapter's tests, all but the e2e package
xkms-test: tfwsim tfwsim-atecc608a
	@cd $(XKMS_DIR) && TFWSIM_DIR=$(XKMS_SIM_DIR) $(GO) test -count=1$(call go-tags,-tags,$(XKMS_TAGS) tfwsim) \
	  $$($(GO) list$(call go-tags,-tags,$(XKMS_TAGS) tfwsim) ./... | grep -v '/e2e$$')

## xkms-e2e-soft: enrol the no-element CRYPTO=auto image through device-attest-01 on its soft root
xkms-e2e-soft: tfwsim-auto
	@cd $(XKMS_DIR) && TFWSIM_DIR=$(XKMS_SIM_DIR) $(GO) test -count=1$(call go-tags,-tags,$(XKMS_TAGS)) \
	  -run '^(TestSoftRootEnrollsThroughDeviceAttest01|TestHardwareEnrollmentAdmitsASoftRootWhenAsked|TestEnrollTargetsTransportFromDevice)$$' ./e2e

## xkms-coverage: xkms adapter unit-test statement coverage, failing if it or any package is under the floor
xkms-coverage:
	@mkdir -p coverage
	@cd $(XKMS_DIR) && pkgs=$$($(GO) list$(call go-tags,-tags,$(XKMS_TAGS)) ./... | grep -v '/e2e$$') && \
	  { $(GO) test -count=1$(call go-tags,-tags,$(XKMS_TAGS)) -coverprofile=../coverage/xkms.out $$pkgs \
	    > ../coverage/xkms-packages.txt 2>&1; rc=$$?; \
	  $(call go-cover-floor,$(XKMS_COVERAGE_FLOOR),../coverage/xkms-packages.txt) && [ $$rc -eq 0 ]; } && \
	  total=$$($(GO) tool cover -func=../coverage/xkms.out | tail -n 1 | tr -s ' \t' ' ' | cut -d' ' -f3 | tr -d '%') && \
	  echo "xkms: $$total% unit-test statement coverage (floor $(XKMS_COVERAGE_FLOOR)%)" && \
	  if [ "$${total%.*}" -lt "$(XKMS_COVERAGE_FLOOR)" ]; then \
	    echo "xkms coverage is below the floor"; exit 1; \
	  fi

## hw-enroll: enroll one board through device-attest-01 and write its run record (#7)
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
	  TFW_HW_SOFT_ROOT='$(SOFT_ROOT)' TFW_HW_RECORD_DIR=$(abspath $(HW_ENROLL_RECORDS)) \
	  $(GO) test -count=1$(call go-tags,-tags,$(XKMS_TAGS)) -run '^TestHardwareEnrollment$$' -v ./e2e/

## go-tags-selftest: every Go invocation the CI stages reach carries GO_BUILD_TAGS; empty changes none
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

.PHONY: sdk-build sdk-test sdk-coverage hw-test api-doc-check sdk-lint sdk-verify verify-vectors xkms-test xkms-e2e-soft xkms-coverage hw-enroll \
        go-tags-selftest go-tags-bridge-prereqs
