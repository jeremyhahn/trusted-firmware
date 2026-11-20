# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# trusted-firmware devcontainer: one Dockerfile, the shipped stages built FROM
# base (handbook devcontainer.md §1), in the fleet template's shape (ci-template
# v1.18, go-quicraft 0cf97cd).
#
#   tools        build-only: the pinned Go analysis tools, trivy and gh, built or
#                downloaded into /out
#   modules      build-only: the module cache of every Go module here, fetched
#                with the forge credentials (xkms/ needs the private go-xkms)
#   base         the pinned Debian, the AVR and host toolchains, Go, a non-root
#                user and the verified module cache; GOPROXY=off
#   toolchain    base + every tool the Makefile calls. `make build`, `make test`,
#                `make security` and `make deps-check` run here, in CI too.
#   integration  base + the source. `make integration-test` runs it under the
#                hardened compose service (compose.yaml beside this file).
#   esp32-build  build-only: Espressif's ESP-IDF image, its tools narrowed to
#                the chips the boards build and its git history dropped
#   esp32        Ubuntu (the ESP-IDF image's own OS) + what esp32-build
#                settled in /opt/esp, its QEMU, Go and the module cache:
#                `make esp32-build` and `make esp32-qemu-test`. Not FROM base:
#                ESP-IDF's toolchains and Python environment are Espressif's
#                (L2), and avr-gcc is Debian's; neither image can host the other
#                without moving a pinned compiler (docs/multi-arch/
#                adr-esp-idf-integration.md, "Toolchain pins").
#   optee        base + the host packages the pinned OP-TEE build needs:
#                `make optee-qemu` and `make integration-test-optee` (#253).
#
# Build with `make docker-build`: it passes GO_IMAGE and the base images' pins
# from scripts/tool-pins.sh, the one pin file the RUN steps also source, and
# the forge credentials as the gomodauth BuildKit secret. GO_IMAGE has no
# default here, so a build that bypasses the pin file fails instead of drifting;
# `make deps-check` fails when an ARG default below and the pin file disagree.
# Requires BuildKit.
#
# The build context is the repository root, filtered by the root .dockerignore.
# A CI runner reaches its Docker daemon through a socket, so a bind mount would
# resolve on the daemon's host, where the checkout does not exist: integration
# bakes the source in, and CI copies the checkout into a toolchain container
# (scripts/in-toolchain-image.sh) instead of mounting it.

ARG GO_IMAGE
ARG BASE_IMAGE=debian:trixie-slim
ARG BASE_DIGEST=sha256:a72b4433de1cf7d6bae0c4cee74d0547f58601a643b3375f070c51600d63b6b2
# espressif/idf:v5.5.5, by the digest of its multi-architecture index. Keep the
# version in step with ESP_IDF_VERSION in tool-pins.sh and mk/arch/esp32.mk.
ARG ESP_IDF_IMAGE=espressif/idf:v5.5.5
ARG ESP_IDF_DIGEST=sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2
# The esp32 image's own base: the Ubuntu release the ESP-IDF image is built on,
# by the digest of its multi-architecture index, so the IDF Python environment
# copied out of it finds the python3 it was made with. Move it with ESP_IDF_*.
ARG ESP_BASE_IMAGE=ubuntu:24.04
ARG ESP_BASE_DIGEST=sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8
# Build-time module and tool downloads go through this proxy (devcontainer.md
# §4). The caller's environment supplies another; no tracked file does.
ARG GO_BUILD_PROXY=https://proxy.golang.org

# ---------------------------------------------------------------------------
FROM ${GO_IMAGE} AS tools
ARG GO_BUILD_PROXY
ARG TARGETARCH
ENV GOTOOLCHAIN=local CGO_ENABLED=0 GOPROXY=${GO_BUILD_PROXY} GOBIN=/out
RUN apk add --no-cache curl tar
COPY scripts/tool-pins.sh /tmp/tool-pins.sh
# gosec builds in a throwaway module so x/tools can be raised to XTOOLS_VERSION
# (see tool-pins.sh); the others build with the x/tools their go.mod names.
# Each download retries: one proxy stream reset (x/text, main 72bc7770, run
# 44757017330, DS-9630) failed the whole image. go.sum and sha256 still verify.
RUN set -eu; \
    . /tmp/tool-pins.sh; \
    retry() { n=1; until "$@"; do [ "$n" -ge 3 ] && return 1; echo "retry $n: $*" >&2; n=$((n+1)); sleep $((n*5)); done; }; \
    retry go install "github.com/golangci/golangci-lint/v2/cmd/golangci-lint@${GOLANGCI_VERSION}"; \
    retry go install "github.com/rhysd/actionlint/cmd/actionlint@${ACTIONLINT_VERSION}"; \
    ( d="$(mktemp -d)"; cd "$d"; \
      go mod init gosec-build; \
      retry go get "github.com/securego/gosec/v2/cmd/gosec@${GOSEC_VERSION}" "golang.org/x/tools@${XTOOLS_VERSION}"; \
      retry go install github.com/securego/gosec/v2/cmd/gosec; \
      cd /; rm -rf "$d" ); \
    retry go install "golang.org/x/vuln/cmd/govulncheck@${GOVULNCHECK_VERSION}"; \
    case "${TARGETARCH:-amd64}" in \
      amd64) arch=64bit; sum="${TRIVY_SHA256_LINUX_AMD64}" ;; \
      arm64) arch=ARM64; sum="${TRIVY_SHA256_LINUX_ARM64}" ;; \
      *) echo "no trivy pin for ${TARGETARCH}" >&2; exit 1 ;; \
    esac; \
    v="${TRIVY_VERSION#v}"; \
    f="trivy_${v}_Linux-${arch}.tar.gz"; \
    retry curl -fsSL -o "/tmp/${f}" "https://github.com/aquasecurity/trivy/releases/download/${TRIVY_VERSION}/${f}"; \
    echo "${sum}  /tmp/${f}" > /tmp/trivy.sha256; \
    sha256sum -c /tmp/trivy.sha256; \
    tar -xzf "/tmp/${f}" -C /out trivy; \
    rm -f "/tmp/${f}"; \
    case "${TARGETARCH:-amd64}" in \
      amd64) sum="${GH_SHA256_LINUX_AMD64}" ;; \
      arm64) sum="${GH_SHA256_LINUX_ARM64}" ;; \
      *) echo "no gh pin for ${TARGETARCH}" >&2; exit 1 ;; \
    esac; \
    d="gh_${GH_VERSION#v}_linux_${TARGETARCH:-amd64}"; \
    retry curl -fsSL -o "/tmp/${d}.tar.gz" "https://github.com/cli/cli/releases/download/${GH_VERSION}/${d}.tar.gz"; \
    echo "${sum}  /tmp/${d}.tar.gz" > /tmp/gh.sha256; \
    sha256sum -c /tmp/gh.sha256; \
    tar -xzf "/tmp/${d}.tar.gz" -C /out --strip-components=2 "${d}/bin/gh"; \
    rm -f "/tmp/${d}.tar.gz"

# ---------------------------------------------------------------------------
# github.com/jeremyhahn/* modules resolve from the forge through git, so this
# stage has git and base does not.
FROM ${GO_IMAGE} AS modules
ARG GO_BUILD_PROXY
ENV GOTOOLCHAIN=local GOFLAGS=-mod=readonly GOMODCACHE=/gomodcache GOPROXY=${GO_BUILD_PROXY}
RUN apk add --no-cache git
WORKDIR /src

# Download the module cache in its own layer. Credentials for the project
# forge arrive as the optional gomodauth BuildKit secret (the K=V lines of
# .devcontainer/.env), mounted for this RUN only: sourced, never written to a
# layer or a command line. With no secret, modules resolve from the public
# proxy, so a module only the forge serves (go-xkms, for xkms/) fails the build
# here, loudly. The template's RUN, over the three modules rather than a root
# one: there is no go.mod at the root. test/integration/esp32's `replace
# ../../../sdk/go` lands on the sdk/go/go.mod copied beside it. Requires
# BuildKit.
COPY sdk/go/go.mod sdk/go/go.sum ./sdk/go/
COPY test/integration/esp32/go.mod test/integration/esp32/go.sum ./test/integration/esp32/
COPY xkms/go.mod xkms/go.sum ./xkms/
RUN --mount=type=secret,id=gomodauth \
    set -eu; \
    if [ -s /run/secrets/gomodauth ]; then \
      set -a; . /run/secrets/gomodauth; set +a; \
      echo "==> go mod download: Gitea module credentials mounted"; \
    else \
      echo "==> go mod download: no module credentials mounted; github.com/jeremyhahn/* resolves from the public proxy only"; \
    fi; \
    for d in sdk/go test/integration/esp32 xkms; do (cd "$d" && go mod download); done
RUN for d in sdk/go test/integration/esp32 xkms; do (cd "$d" && go mod verify) || exit 1; done

# ---------------------------------------------------------------------------
FROM ${BASE_IMAGE}@${BASE_DIGEST} AS base

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

COPY scripts/tool-pins.sh /etc/tfw/tool-pins.sh
COPY docker/packages.txt /etc/tfw/packages.txt

# The compiler and runtime: every packages.txt line not tagged [toolchain].
# The upgrade takes the base digest's packages to the current Debian security
# fixes, which the integration image's `trivy image` gate would otherwise flag.
# DEBIAN_REFRESH exists only to rebuild this layer for Debian security fixes:
# the image tags hash this file and nothing else here changes when Debian
# ships a fix, so CI would reuse the image (and BuildKit this layer) built
# before it. Move the date to take a fix the base digest predates.
# 2026-10-24: libbrotli1 1.1.0-2+deb13u1, CVE-2026-111497 (DS-3479).
ARG DEBIAN_REFRESH=2026-10-24
RUN echo "==> Debian security refresh ${DEBIAN_REFRESH}" \
    && apt-get update \
    && apt-get upgrade -y --no-install-recommends \
    && apt-get install -y --no-install-recommends \
      $(grep -v '#[[:space:]]*\[toolchain\]' /etc/tfw/packages.txt | sed 's/#.*//' | tr '\n' ' ') \
      ca-certificates curl xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Go, by version and checksum: the compiler is an input to the artefact the
# reproducible-build check pins. GOTOOLCHAIN=local: never fetch another.
RUN . /etc/tfw/tool-pins.sh \
    && curl -fsSLo /tmp/go.tgz "https://go.dev/dl/go${GO_VERSION}.linux-amd64.tar.gz" \
    && echo "${GO_SHA256}  /tmp/go.tgz" | sha256sum -c - \
    && tar -C /usr/local -xzf /tmp/go.tgz \
    && rm /tmp/go.tgz
ENV PATH="/usr/local/go/bin:${PATH}" \
    GOTOOLCHAIN=local \
    GOMODCACHE=/opt/go/mod

# The verified module cache of every module (devcontainer.md §4), read-only to
# everyone. Nothing fetches a module at run time.
COPY --from=modules /gomodcache /opt/go/mod
RUN chmod -R a+rX /opt/go
ENV GOPROXY=off

# Any non-root uid runs it (devcontainer.md §5): HOME is /tmp, the build cache
# lives there. /src is the user's: in-toolchain-image.sh streams the checkout
# in owned by 10001, but `docker cp` leaves the existing destination directory
# as it was, and a root-owned /src (what WORKDIR alone makes) refuses every new
# top-level entry the stages create, `make coverage`'s coverage/ the first.
RUN useradd --uid 10001 --user-group --no-create-home --home-dir /tmp dev \
    && install -d -o 10001 -g 10001 /src
ENV HOME=/tmp \
    GOCACHE=/tmp/.gocache
WORKDIR /src

# --- toolchain ----------------------------------------------------------------
FROM base AS toolchain

USER root
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
      $(grep '#[[:space:]]*\[toolchain\]' /etc/tfw/packages.txt | sed 's/#.*//' | tr '\n' ' ') \
      git jq bash make shellcheck \
    && rm -rf /var/lib/apt/lists/* \
    && git config --system --add safe.directory '*'
COPY --from=tools /out/ /usr/local/bin/

# The toolchain versions are an input to every measurement this image makes --
# the size budget, the reproducible-build hash -- so a number that moved can be
# attributed to the code or to the compiler.
RUN { echo "# trusted-firmware devcontainer (toolchain)"; \
      avr-gcc --version | head -1; \
      simavr --help 2>&1 | head -1 || true; \
      gcc --version | head -1; \
      go version; \
      cppcheck --version; \
      for t in gosec govulncheck golangci-lint actionlint; do \
        go version -m "$(command -v "$t")" | awk -v t="$t" '$1=="mod"{print t, $3; exit}'; \
      done; \
      trivy --version | head -1; \
      gh --version | head -1; \
    } > /etc/ci-toolchain.txt

USER 10001:10001
# As the user, the writes the stages make outside the copied-in tree: a new
# top-level entry in /src, and HOME. One that needs root fails here, not in CI.
RUN mkdir /src/.probe "$HOME/.probe" && rmdir /src/.probe "$HOME/.probe"

# --- integration --------------------------------------------------------------
FROM base AS integration

# The source is baked in, so the suite runs on exactly what was scanned. No
# setuid bit survives, and it runs as the non-root user; compose.yaml adds the
# read-only root, no network and no capabilities (devcontainer.md §3). The CI
# image job runs `trivy image` over this stage.
COPY --chown=10001:10001 . /src
RUN find / -xdev -perm /6000 -type f -exec chmod a-s {} + \
    && { echo "# trusted-firmware devcontainer (integration)"; \
         avr-gcc --version | head -1; gcc --version | head -1; go version; } > /etc/ci-toolchain.txt
ENV TFW_IMAGE_STAGE=integration
USER 10001:10001

# --- esp32-build --------------------------------------------------------------
# Build-only (#470): Espressif's image carries every chip's tools, the full git
# history of ESP-IDF and its submodules, and a compiler suite, which made the
# esp32 image 18 GB. This stage only settles what the esp32 stage copies out:
# /opt/esp, holding the ESP-IDF tree, the tools and the IDF Python environment.
FROM ${ESP_IDF_IMAGE}@${ESP_IDF_DIGEST} AS esp32-build
# No pipefail: `qemu --version | head -n 1` may take SIGPIPE once head is done.
SHELL ["/bin/bash", "-c"]

# What the ADR pins, installed by ESP-IDF's own idf_tools.py. Its tools.json at
# the v5.5.5 tag names the version and the checksum of each archive, so the pin
# is ESP-IDF's, not ours. The base image carries the tools already; naming them
# again makes the build fail here, not in the job, if it does not.
#   Espressif QEMU   esp_develop_9.2.2_20260417  (qemu-riscv32, qemu-xtensa)
#   CMake / Ninja    3.30.2 / 1.12.1
# ESP_CHIPS are the chips the boards build: ESP_TARGETS of boards/esp32-qemu
# (esp32c3, esp32) and ESP_TARGET of boards/esp32p4-poe. idf-env.json is
# started again so it records these, not the base image's "all": export.sh
# then exports, and checks, only their tools, and a tool directory nothing
# exports is dropped. Every idf_tools.py call names --targets: its default is
# all. The git history goes too, with version.txt kept in its place, which
# ESP-IDF's CMake and idf.py read when there is no .git.
# Keep the QEMU version in step with ESP_QEMU_VERSION in mk/arch/esp32.mk.
ARG ESP_IDF_IMAGE
ARG ESP_CHIPS=esp32,esp32c3,esp32p4
ARG ESP_QEMU_VERSION=esp_develop_9.2.2_20260417
RUN rm -f "$IDF_TOOLS_PATH/idf-env.json" \
    && python3 "$IDF_PATH/tools/idf_tools.py" --non-interactive install --targets="${ESP_CHIPS}" required \
    && python3 "$IDF_PATH/tools/idf_tools.py" --non-interactive install --targets="${ESP_CHIPS}" \
         cmake ninja qemu-riscv32 qemu-xtensa \
    && python3 "$IDF_PATH/tools/idf_tools.py" export --format key-value > /tmp/idf-export \
    && . "$IDF_PATH/export.sh" >/dev/null \
    && qemu-system-riscv32 --version | head -n 1 | grep -qF "${ESP_QEMU_VERSION}" \
    && qemu-system-xtensa --version | head -n 1 | grep -qF "${ESP_QEMU_VERSION}" \
    && for d in "$IDF_TOOLS_PATH"/tools/*/; do \
         grep -qF "$d" /tmp/idf-export || { echo "esp32-build: dropping $d"; rm -rf "$d"; }; \
       done \
    && git -C "$IDF_PATH" describe --tags > "$IDF_PATH/version.txt" \
    && [ "$(cat "$IDF_PATH/version.txt")" = "${ESP_IDF_IMAGE##*:}" ] \
    && find "$IDF_PATH" -name .git -prune -exec rm -rf {} + \
    && rm -rf "$IDF_PATH/docs" "$IDF_TOOLS_PATH/dist" /root/.cache /tmp/idf-export

# --- esp32 --------------------------------------------------------------------
# The image `make esp32-build` and `make esp32-qemu-test` run in: the OS the
# ESP-IDF image is built on (Ubuntu 24.04, its python3 the 3.12 the IDF Python
# environment was made with, so the environment runs where it is copied), the
# runtime packages, and what esp32-build settled. Go and the verified module
# cache are base's, the same pin and the same read-only cache.
FROM ${ESP_BASE_IMAGE}@${ESP_BASE_DIGEST} AS esp32

ENV DEBIAN_FRONTEND=noninteractive
# `export.sh` puts the toolchains and ESP-IDF's Python environment on PATH. The
# entrypoint, ESP-IDF's own, sources it for `docker run` and for the container
# scripts/in-toolchain-image.sh starts; a RUN step has no entrypoint, so each
# one that needs a tool sources it itself.
SHELL ["/bin/bash", "-c"]

# make drives the targets, git names the build's version, python3 runs the IDF
# Python environment, ccache is what idf.py turns on when it finds it, binutils
# is the host objcopy platform-bake calls, and gcc the cgo compiler the ESP-IDF
# image had. The rest is the runtime of the tools: QEMU's, and libusb for
# openocd-esp32, which export.sh checks by running it.
RUN apt-get update && apt-get install -y --no-install-recommends \
      make git ca-certificates python3 python3-venv ccache binutils gcc libc6-dev \
      libgcrypt20 libglib2.0-0 libpixman-1-0 libsdl2-2.0-0 libslirp0 libusb-1.0-0 libbsd0 \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/python python /usr/bin/python3 10 \
    && git config --system --add safe.directory '*'

COPY --from=esp32-build /opt/esp /opt/esp
ENV IDF_PATH=/opt/esp/idf \
    IDF_TOOLS_PATH=/opt/esp \
    IDF_GITHUB_ASSETS="" \
    IDF_PYTHON_CHECK_CONSTRAINTS=no \
    IDF_CCACHE_ENABLE=1
ENTRYPOINT ["/opt/esp/entrypoint.sh"]
CMD ["/bin/bash"]

# Go, for the `wirecheck` host of the QEMU test and the Go tools esp32-build
# builds: base's install, from the same pin file, and base's module cache.
COPY scripts/tool-pins.sh /etc/tfw/tool-pins.sh
COPY --from=base /usr/local/go /usr/local/go
COPY --from=base /opt/go /opt/go
ENV PATH="/usr/local/go/bin:${PATH}" \
    GOTOOLCHAIN=local \
    GOMODCACHE=/opt/go/mod \
    GOPROXY=off

# The QEMUs again, now on this image's libraries.
ARG ESP_QEMU_VERSION=esp_develop_9.2.2_20260417
RUN . "$IDF_PATH/export.sh" >/dev/null \
    && qemu-system-riscv32 --version | head -n 1 | grep -qF "${ESP_QEMU_VERSION}" \
    && qemu-system-xtensa --version | head -n 1 | grep -qF "${ESP_QEMU_VERSION}"

# The versions are an input to every measurement the job makes; recording them
# lets a result that moved be attributed to the code or to the toolchain.
RUN . "$IDF_PATH/export.sh" >/dev/null \
    && { echo "# trusted-firmware devcontainer (esp32)"; \
         idf.py --version; \
         riscv32-esp-elf-gcc --version | head -n 1; \
         xtensa-esp-elf-gcc --version | head -n 1; \
         cmake --version | head -n 1; \
         ninja --version; \
         qemu-system-riscv32 --version | head -n 1; \
         qemu-system-xtensa --version | head -n 1; \
         go version; \
       } > /etc/ci-toolchain.txt

# The same non-root uid as base, so scripts/in-toolchain-image.sh runs either
# image the same way. What ESP-IDF writes when it builds, and where each lands:
#   the build tree           -B under the checkout's build/ (mk/arch/esp32-targets.mk)
#   idf-env.json             $IDF_TOOLS_PATH, which idf_tools.py rewrites from
#                            export.sh: the directory and the file are chowned to
#                            the user, nothing under them (a recursive chown would
#                            copy the toolchains into a second layer)
#   the component manager    IDF_COMPONENT_CACHE_PATH, under HOME
#   ccache                   CCACHE_DIR, under HOME (idf.py turns it on when found)
#   Python bytecode          PYTHONPYCACHEPREFIX, under HOME: the Python
#                            environment and IDF_PATH stay root-owned, read-only
# HOME is /tmp, which in-toolchain-image.sh mounts as a tmpfs the user owns.
# /src, the checkout, is the user's for the reason base gives.
RUN useradd --uid 10001 --user-group --no-create-home --home-dir /tmp dev \
    && install -d -o 10001 -g 10001 /src \
    && touch "$IDF_TOOLS_PATH/idf-env.json" \
    && chown 10001:10001 "$IDF_TOOLS_PATH" "$IDF_TOOLS_PATH/idf-env.json"
ENV HOME=/tmp \
    GOCACHE=/tmp/.gocache \
    IDF_COMPONENT_CACHE_PATH=/tmp/.cache/Espressif/ComponentManager \
    CCACHE_DIR=/tmp/.cache/ccache \
    PYTHONPYCACHEPREFIX=/tmp/.cache/pycache
WORKDIR /src
USER 10001:10001

# The proof, as the user: export.sh and an ESP-IDF example built the way
# esp32-build builds, once on each toolchain (esp32c3 RISC-V, esp32 Xtensa), so
# a write that still needs root, or a tool esp32-build dropped, fails the image
# build here rather than the job. The output and the caches are removed after.
RUN mkdir /src/.probe && rmdir /src/.probe \
    && . "$IDF_PATH/export.sh" >/dev/null \
    && for t in esp32c3 esp32; do \
         rm -rf /tmp/esp32-probe \
         && cp -r "$IDF_PATH/examples/get-started/hello_world" /tmp/esp32-probe \
         && { idf.py -C /tmp/esp32-probe -B /tmp/esp32-probe/build -DIDF_TARGET="$t" build \
                >/tmp/esp32-probe.log 2>&1 \
              || { tail -n 40 /tmp/esp32-probe.log; exit 1; }; } \
         || exit 1; \
       done \
    && rm -rf /tmp/esp32-probe /tmp/esp32-probe.log /tmp/.cache /tmp/.gocache

# --- optee --------------------------------------------------------------------
FROM base AS optee

# The host packages the pinned OP-TEE build needs (#253, docs/optee-trustzone/
# README.md "CI"): every Debian package `make optee-tools` names
# (OPTEE_HOST_CMDS, OPTEE_HOST_LIBS, OPTEE_HOST_PY and python3-venv in
# mk/optee.mk), and meson and expect, which the page's CI table names. The
# cross toolchains, QEMU and repo are not here: `make optee-tools` fetches them
# at the pin into OPTEE_CACHE, which CI caches by the pin.
USER root
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
      git curl python3 make build-essential patch perl file bzip2 xz-utils \
      unzip rsync wget cpio bc bison flex device-tree-compiler ninja-build \
      pkg-config libglib2.0-dev libpixman-1-dev libslirp-dev libgnutls28-dev \
      libssl-dev python3-cryptography python3-pyelftools python3-venv meson expect \
    && rm -rf /var/lib/apt/lists/* \
    && git config --system --add safe.directory '*'

# OPTEE_CACHE in CI: on disk in the container's layer, not in the 3 GB /tmp
# tmpfs, and the user's. scripts/in-toolchain-image.sh copies the runner's
# cached copy in here and out again (IN_IMAGE_CACHE).
RUN install -d -o 10001 -g 10001 /var/cache/tfw-optee

RUN { echo "# trusted-firmware devcontainer (optee)"; \
      gcc --version | head -1; \
      python3 --version; \
      ninja --version; \
      meson --version; \
    } > /etc/ci-toolchain.txt

USER 10001:10001
RUN mkdir /src/.probe /var/cache/tfw-optee/.probe \
    && rmdir /src/.probe /var/cache/tfw-optee/.probe
