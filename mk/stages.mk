# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The five CI stages (handbook ci-standard.md §1). Each is a target, each runs
# in the devcontainer (build/docker/devcontainer/Dockerfile) and `ci` is all
# five, in order. Every gate `make ci` ran before the stages existed is in
# exactly one of them; a new gate goes into one, never beside them.
#
#   build             everything compiles, within its flash budget, reproducibly
#   test              the unit suites, the board sweep, coverage, the sanitizers
#   integration-test  the real images under simavr, tfwsim against the Go SDK
#   security          static analysis, the Go scanners, the secrets scan, the
#                     supply-chain and releasability gates
#   deps-check        no pin behind its latest release without a written reason
#
# Each stage starts by checking for its tools and fails closed when one is
# missing (devcontainer.md §7): a gate that cannot run is a failure, not a pass.
#
# The contracts are the fleet template's (ci-template v1.18, go-quicraft
# 0cf97cd). build, test, security and deps-check run in place, in the toolchain
# image in CI. integration-test builds the integration image and runs
# integration-test-host in it under the hardened compose service; run
# integration-test-host itself where the toolchains are installed.
#
# xkms/ is in four of the five through its own targets (xkms-coverage in test,
# xkms-test in integration-test-host, xkms-security, xkms-deps-check): it needs
# the private go-xkms, which the image's module cache carries, fetched with the
# forge credentials at build time (the gomodauth secret, mk/docker.mk).

# The Go modules every module loop below walks. xkms/ has its own targets: it
# builds only with XKMS_TAGS.
GO_MODULES := $(SDK_DIR) test/integration/esp32

# require-tools: fail the calling target when any named tool is not on PATH.
define require-tools
	@missing=; for t in $(1); do command -v $$t >/dev/null 2>&1 || missing="$$missing $$t"; done; \
	 if [ -n "$$missing" ]; then \
	   echo "$@: missing:$$missing -- run it in the devcontainer: make docker-build && make docker-run CMD='make $@'"; \
	   exit 1; fi
endef

# ------------------------------------------------------------------- build
## build: stage 1 -- every image and binary compiles, within budget, reproducibly
# firmware-matrix runs size-check per image and bootloader-check boot-size-check,
# so the flash budgets are build gates; platform-bake-build-check holds the bake
# image's flash ceiling. header-deps-check is here because it is about what
# gets built: a header edit rebuilding what includes it. CI runs firmware-matrix
# itself, in the image-toolchain job, so there is no workflow matrix to keep in step.
# app-firmware-check builds for the ATmega2560 whatever BOARD is: the example
# application does not fit the 328P, element-only since owner ruling 2026-10-17
# (m522), and applications target boards with room.
build: build-tools firmware-matrix bootloader-check sdk-build tfwsim tfwsim-atecc608a \
       tfwsim-app-example reproducible-check bootloader-reproducible-check provenance \
       platform-bake-no-link-check platform-bake-build-check header-deps-check
	@$(MAKE) --no-print-directory app-firmware-check BOARD=atmega2560
	@echo "stage build: green"

build-tools:
	$(call require-tools,avr-gcc avr-objcopy avr-size gcc $(GO) sha256sum)

# -------------------------------------------------------------------- test
## test: stage 2 -- the host unit suites, board sweep, coverage floor, sanitizers
# verify-vectors is here: the C and Go wire vectors are test data, and a
# mismatch is a wire break (CLAUDE.md), not a build failure. xkms-coverage and
# sdk-coverage hold the two Go modules' statement-coverage floors, as coverage
# holds the firmware's.
#
# The gates run one after another, in a sub-make each: coverage and test-asan
# start with clean-host, which deletes $(HOST_DIR) while host-test, boot-test
# or board-sweep could be building into it under `make -j`. Serial here, so
# `make -j8 ci` and `make ci` run the same thing; each sub-make still uses -j.
TEST_GATES := host-test boot-test board-sweep coverage test-asan verify-vectors xkms-coverage sdk-coverage scripts-test

test: test-tools
	@for g in $(TEST_GATES); do $(MAKE) --no-print-directory $$g || exit 1; done
	@echo "stage test: green"

test-tools:
	$(call require-tools,gcc gcovr python3)

## scripts-test: the unit tests of the Python scripts (scripts/test_*.py), no network
scripts-test:
	@python3 -B -m unittest discover -s scripts -p 'test_*.py'

# -------------------------------------------------------- integration-test
## integration-test: stage 3 -- integration-test-host in the integration image, under compose
# The template's contract: the integration image built at this commit (with the
# forge credentials as the gomodauth secret), the hardened compose service in
# build/docker/devcontainer/compose.yaml (read-only, no network, no
# capabilities), in its own compose project, torn down with its volumes however
# the run ends. Needs docker; CI's `integration` job runs it.
integration-test: docker-build-it
	@echo "==> Running integration tests in $(IT_IMAGE) (project $(IT_PROJECT))..."
	@status=0; \
	TRUSTED_FIRMWARE_IT_IMAGE='$(IT_IMAGE)' $(DOCKER) compose -f $(DEVCONTAINER_COMPOSE) -p '$(IT_PROJECT)' \
		run --rm integration $(IT_CMD) || status=$$?; \
	TRUSTED_FIRMWARE_IT_IMAGE='$(IT_IMAGE)' $(DOCKER) compose -f $(DEVCONTAINER_COMPOSE) -p '$(IT_PROJECT)' \
		down --volumes >/dev/null 2>&1 || true; \
	exit $$status

## integration-test-host: the real images under simavr, and tfwsim against the Go SDK and xkms
# What stage 3 runs inside the integration image, and what a developer runs in
# place. sdk-test is the Go unit tests and the cross-implementation suite
# together, the suite compiled in by the tfwsim build tag rather than picked
# by test name, which would be a list that drifts. app-sdk-check runs the out-of-tree example application on
# tfwsim; xkms-test runs the xkms adapter suites, the board enrollment harness
# (TestHardwareEnrollment, hw-enroll's) deselected; xkms-e2e-soft runs the one
# xkms/e2e test that needs no board, the soft root's enrolment (#130).
integration-test-host: integration-test-tools integration-test-simavr integration-test-boot sdk-test app-sdk-check xkms-test xkms-e2e-soft
	@echo "stage integration-test: green"

integration-test-tools:
	$(call require-tools,avr-gcc gcc $(GO))
	@printf '#include <simavr/sim_avr.h>\n' | $(CC) -E -x c - >/dev/null 2>&1 || \
	  { echo "$@: missing: libsimavr-dev -- run it in the devcontainer"; exit 1; }

# ---------------------------------------------------------------- security
## security: stage 4 -- static analysis, Go scanners, secrets scan, supply chain
# devops.md §8 for C and Go:
#
#   C: cppcheck at its full level (`lint`, with the preprocessor-confinement,
#      __DATE__ and test-sleep greps and the arch-leak gate), verify-vendor (the vendored
#      code's hashes) and releasability.
#   Go, every module in GO_MODULES: gofmt and vet (sdk-lint, go-vet), gosec,
#      govulncheck, golangci-lint with .golangci.yml, go mod verify (sdk-verify,
#      go-mod-verify), and sdk/go's Layer 0 invariant.
#   Both: trivy fs --scanners secret over the tree (secrets-scan).
#
# Where a §8 tool does not apply to the C firmware, and why:
#   gosec, govulncheck, golangci-lint, go vet  read Go only. cppcheck stands in
#        for vet and the linters; clang-tidy is installed but not yet a gate.
#   govulncheck's C equivalent, a CVE scan of dependencies, has no input: the C
#        firmware has no package manifest, its only third-party code is vendored
#        and pinned by hash (verify-vendor).
#   trivy fs --scanners vuln  finds no C manifest for the same reason; for the
#        Go modules it duplicates govulncheck, and §8a runs it nightly during
#        alpha (nightly.yml), so it is not here.
#   trivy image  needs a docker daemon and a built image: `make trivy-image`,
#        run where the integration image is built.
security: security-tools lint lint-arch-leak-selftest lint-grep-selftest go-tags-selftest esp32-overlay-selftest verify-vendor releasability releasability-test \
          sdk-lint sdk-verify go-vet go-mod-verify go-gosec go-govulncheck go-golangci xkms-security secrets-scan
	@echo "stage security: green"

security-tools:
	$(call require-tools,cppcheck $(GO) gosec govulncheck golangci-lint trivy git)

# sdk-lint and sdk-verify cover sdk/go; these cover every module in GO_MODULES.
go-vet:
	@set -e; for d in $(filter-out $(SDK_DIR),$(GO_MODULES)); do \
	  out=$$(cd $$d && gofmt -l .); if [ -n "$$out" ]; then echo "$$d: gofmt:"; echo "$$out"; exit 1; fi; \
	  (cd $$d && $(GO) vet$(call go-tags,-tags) ./...); echo "$$d: gofmt and vet clean"; done

go-mod-verify:
	@set -e; for d in $(filter-out $(SDK_DIR),$(GO_MODULES)); do (cd $$d && $(GO) mod verify); done

go-gosec:
	@fail=0; for d in $(GO_MODULES); do echo "--- gosec $$d"; \
	  (cd $$d && gosec -quiet$(call go-tags,-tags) ./...) || fail=1; done; exit $$fail

go-govulncheck:
	@fail=0; for d in $(GO_MODULES); do echo "--- govulncheck $$d"; \
	  (cd $$d && govulncheck$(call go-tags,-tags) ./...) || fail=1; done; exit $$fail

# sdk/go's simulator-driven tests carry the tfwsim tag (#163), so its second
# pass lints them too.
go-golangci:
	@fail=0; for d in $(GO_MODULES); do echo "--- golangci-lint $$d"; \
	  (cd $$d && golangci-lint run --config $(CURDIR)/.golangci.yml$(call go-tags,--build-tags) ./...) || fail=1; done; \
	echo "--- golangci-lint $(SDK_DIR) (tfwsim)"; \
	(cd $(SDK_DIR) && golangci-lint run --config $(CURDIR)/.golangci.yml$(call go-tags,--build-tags,tfwsim) ./...) || fail=1; \
	exit $$fail

## secrets-scan: trivy's secret scanner over the tree, failing on any finding
secrets-scan:
	@trivy fs --quiet --scanners secret --exit-code 1 --cache-dir $${HOME:-/tmp}/.cache/trivy \
	   --skip-dirs $(BUILD_DIR) --skip-dirs coverage .
	@echo "secrets-scan: no secret in the tree"

## trivy-image: build and scan the integration image (needs docker; devops.md §8)
# CI's `image-security` job runs the same scan on the image it builds at the commit under
# test. TRIVY_IMAGE_FLAGS are that step's flags, word for word (the CUSTOMIZE
# block in .github/workflows/ci.yml): exclusions live there, in the template
# form, never in a .trivyignore (CA m269). Change both together.
TRIVY_IMAGE_FLAGS := --ignore-unfixed --skip-dirs /opt/go/mod --severity CRITICAL,HIGH --exit-code 1

trivy-image: docker-build-it
	@trivy image $(TRIVY_IMAGE_FLAGS) $(IT_IMAGE)

## xkms-security: the security stage's Go gates for xkms/ (needs go-xkms)
xkms-security:
	$(call require-tools,$(GO) gosec govulncheck golangci-lint)
	@cd $(XKMS_DIR) && $(GO) mod verify
	@cd $(XKMS_DIR) && gosec -quiet$(call go-tags,-tags,$(XKMS_TAGS)) ./...
	@cd $(XKMS_DIR) && govulncheck$(call go-tags,-tags,$(XKMS_TAGS)) ./...
	@cd $(XKMS_DIR) && golangci-lint run --config $(CURDIR)/.golangci.yml$(call go-tags,--build-tags,$(XKMS_TAGS)) ./...

# -------------------------------------------------------------- deps-check
## deps-check: stage 5 -- fail when a pin is behind its latest release without a reason
# scripts/deps-check.py: the toolchain pins in scripts/tool-pins.sh (Go, the
# base image tag and digest, avr-gcc, avr-libc and simavr as installed, ESP-IDF,
# the §8 tools) and every require of every Go module, first- and third-party.
# A held pin carries its reason in place (devops.md §9). A Go module release
# younger than 7 days is reported, not failed, and fails from then on (CA m823);
# the toolchain pins fail at once as before. It asks upstream, so
# it needs the network; the module cache it fills is its own, under build/.
DEPS_GOPROXY ?= https://proxy.golang.org
DEPS_GOMODCACHE ?= $(abspath $(BUILD_DIR))/deps-check/gomod
# The module graph's go.mod files come from the cache the image pre-fetched (the
# Dockerfile's modules stage), so a cold DEPS_GOMODCACHE does not git-fetch a
# whole tree per private version and outlast deps-check.py's 600 s bound, which
# stays: a real hang still fails. Latest versions are still asked of upstream.
DEPS_SEED_GOMODCACHE ?= $(or $(wildcard /opt/go/mod),$(shell go env GOMODCACHE 2>/dev/null))
DEPS_ENV = GOPROXY=$(DEPS_GOPROXY) GOFLAGS=-modcacherw GOMODCACHE=$(DEPS_GOMODCACHE) \
	   DEPS_SEED_GOMODCACHE=$(DEPS_SEED_GOMODCACHE)

deps-check: deps-check-tools xkms-deps-check
	@rc=0; python3 scripts/deps-check.py toolchain || rc=1; \
	 $(DEPS_ENV) python3 scripts/deps-check.py go $(GO_MODULES) || rc=1; \
	 if [ $$rc -eq 0 ]; then echo "stage deps-check: green"; fi; exit $$rc

## xkms-deps-check: the latest-version check for xkms/ (needs go-xkms)
xkms-deps-check: deps-check-tools
	@$(DEPS_ENV) python3 scripts/deps-check.py go $(XKMS_DIR)

deps-check-tools:
	$(call require-tools,python3 git $(GO) dpkg-query)

.PHONY: build build-tools test test-tools integration-test integration-test-host integration-test-tools \
        security security-tools go-vet go-mod-verify go-gosec go-govulncheck go-golangci \
        secrets-scan trivy-image xkms-security deps-check xkms-deps-check deps-check-tools scripts-test
