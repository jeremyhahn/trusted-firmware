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
BASE_DIGEST=sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f

# Go, for sdk/go, xkms/ and test/integration/esp32. 1.27.2 because xkms/
# requires go-xkms, whose go.mod says `go 1.27.1`; GOTOOLCHAIN=local in the
# image means it never fetches a newer one.
GO_VERSION=1.27.2
GO_SHA256=ecbadb99091a3f46e31f5f934b068b1864eafa7995211b39eaddf76996045fe5

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
GO_IMAGE=golang:1.27.2-alpine3.24@sha256:f92b6ef800e499660581efdabdf25d9d817a9d124eaf900924f0504e7e27e12d

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
GOSEC_VERSION=v2.29.0
# gosec is built against this x/tools: the v0.49.0 its go.mod names cannot read
# the export data (version 5) Go 1.27 writes, so every import fails to type-check.
XTOOLS_VERSION=v0.52.0
GOVULNCHECK_VERSION=v1.8.0
HELD_GOVULNCHECK="the fleet template's pin (ci-template v1.18); the next one comes from the Company Agent's versions.conf (devops.md §9)"

# trivy: a release archive, checked against these digests (upstream's
# trivy_<version>_checksums.txt) before it is unpacked.
TRIVY_VERSION=v0.76.0
TRIVY_SHA256_LINUX_AMD64=e24b1c51293c1b15e4ba8d45bbc7891ba053f90278ec2698f3562c993e145fe7
TRIVY_SHA256_LINUX_ARM64=aab21e677a7b7071fc79f1711480840754c18a1b9d9cefa7ccd06af63a0e8f4c
