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
# element tier, the only peer that produces DICE evidence.
sdk-test: tfwsim tfwsim-atecc608a
	@cd $(SDK_DIR) && $(GO) test -count=1 ./...

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

## verify-vectors: check the C and Go wire vectors carry identical values
verify-vectors:
	@./scripts/verify-vectors.sh

.PHONY: sdk-build sdk-test sdk-lint sdk-verify verify-vectors
