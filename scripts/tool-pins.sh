# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Every pinned tool, in one file (handbook devcontainer.md §2). Read by
# build/docker/devcontainer/Dockerfile, which sources it, by the Makefile's
# tool check (mk/stages.mk) and by `make deps-check` (scripts/deps-check.py),
# so the image, the gates and the latest-version check cannot disagree.
#
# Plain NAME=value lines, no expansion; a value with spaces is double-quoted.
# A shell sources it and a script parses it.
#
# A pin held below the latest release carries HELD_<NAME>, the reason in one
# line (devops.md §9: "unless a pin is documented in place with the reason").
# `make deps-check` fails on a pin that is behind and has none.

# --- base: the image every stage starts from -------------------------------
# Debian, not alpine (devops.md §9a): avr-gcc, avr-libc and simavr come as
# Debian packages with a known provenance, a shorter trust chain than a
# community AVR image. Pinned by the digest of the
# multi-architecture index; the tag is recorded beside it for a reader.
BASE_IMAGE=debian:trixie-slim
BASE_DIGEST=sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad

# Go, for sdk/go, xkms/ and test/integration/esp32. At least 1.27.1 because
# xkms/ requires go-xkms, whose go.mod says `go 1.27.1`; 1.27.3 because
# crypto/x509 before it is GO-2026-6809, which govulncheck finds in xkms/.
# GOTOOLCHAIN=local in the image means it never fetches a newer one, so this
# pin is the Go every CI stage runs: the image job builds the toolchain image
# from build/docker/devcontainer/Dockerfile at the PR's commit.
GO_VERSION=1.27.3
GO_SHA256=65efbc80fdc1fa2cd2631cad5b481777a68e8725f34e89f50fc6ac714e379b40
HELD_GO="GO_VERSION, GO_SHA256 and GO_IMAGE name one Go and move together in one commit; CI builds its toolchain image from these pins at the PR's commit, so a bump is tested by the PR that makes it"

# --- the AVR toolchain: Debian trixie's packages -----------------------------
# Installed from docker/packages.txt at the base digest above, so the version
# is the one the pinned Debian release ships. deps-check reads the installed
# version with dpkg-query and compares it with the upstream release.
HELD_AVR_GCC="Debian trixie's gcc-avr: the toolchain comes from Debian packages for their provenance (docker/packages.txt), and a compiler change moves every size budget and the reproducible-build hash"
HELD_AVR_LIBC="Debian trixie's avr-libc, for the reason HELD_AVR_GCC gives"
HELD_SIMAVR="Debian trixie's simavr, for the reason HELD_AVR_GCC gives"

# --- ESP-IDF: the devcontainer's `esp32` stage, FROM espressif/idf -----------
# Kept in step with ESP_IDF_VERSION in mk/arch/esp32.mk and the pin table in
# docs/multi-arch/adr-esp-idf-integration.md. The ADR states why the toolchain
# is pinned; HELD_ESP_IDF states why it is this version.
ESP_IDF_VERSION=v5.5.5
HELD_ESP_IDF="6.x is a major-version port of the ESP32-P4 work (#48, the #16 ADR) that is not done yet; the bump lands with that port, see #48"

# --- the Go image the analysis tools are built in --------------------------
# The devcontainer's `tools` and `modules` stages, as in the fleet template
# (go-quicraft 0cf97cd): the tools are static (CGO_ENABLED=0) and copied into
# the Debian toolchain stage. GO_VERSION above and this tag must name the same
# Go. Pinned by index digest.
GO_IMAGE=golang:1.27.3-alpine3.24@sha256:f2c872bcee949b7be9c490efd02b48a8a2fbf947cdf533f1c58525ec9d8170fd

# --- CI tools (devops.md §8) -------------------------------------------------
# The fleet template's pins (ci-template v1.18, go-quicraft 0cf97cd), built
# into the toolchain image; nothing is installed on a host. The Go-built tools
# need no digest: the Go checksum database verifies the module they come from.
#
# The golangci-lint version. Every literal pin in .github/workflows/ci.yml must
# match it, and each of these.
GOLANGCI_VERSION=v2.14.0
HELD_GOLANGCI="the fleet template's pin (ci-template v1.20 checks v2.14.0 outside the CUSTOMIZE blocks); the next one comes from the Company Agent's versions.conf (devops.md §9)"
# The workflow linter, for .github/workflows.
ACTIONLINT_VERSION=v1.7.12
GOSEC_VERSION=v2.30.0
# gosec is built against this x/tools: an x/tools older than v0.52.0 cannot read
# the export data (version 5) Go 1.27 writes, so every import fails to type-check.
# gosec v2.30.0's go.mod names v0.52.0 itself; the pin keeps the floor explicit.
XTOOLS_VERSION=v0.52.0
GOVULNCHECK_VERSION=v1.8.0
HELD_GOVULNCHECK="the fleet template's pin (ci-template v1.18); the next one comes from the Company Agent's versions.conf (devops.md §9)"

# trivy: a release archive, checked against these digests (upstream's
# trivy_<version>_checksums.txt) before it is unpacked.
TRIVY_VERSION=v0.76.1
TRIVY_SHA256_LINUX_AMD64=c7cbe3137fb8a48a17574bede3dd6beb16742d1e9772929940fc90082c39d9c0
TRIVY_SHA256_LINUX_ARM64=e174dff2a6dba5e3ae41f8d0537a6324405bd08132497340304c6b20e0f16a1f
