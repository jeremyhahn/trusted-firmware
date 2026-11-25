# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# trusted-firmware devcontainer: one Dockerfile, the shipped stages built from base, in the fleet template's shape.

ARG GO_IMAGE
ARG BASE_IMAGE=debian:trixie-slim
ARG BASE_DIGEST=sha256:a72b4433de1cf7d6bae0c4cee74d0547f58601a643b3375f070c51600d63b6b2
# espressif/idf:v5.5.5, by the digest of its multi-architecture index.
ARG ESP_IDF_IMAGE=espressif/idf:v5.5.5
ARG ESP_IDF_DIGEST=sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2
# The esp32 image's own base: the Ubuntu release the ESP-IDF image is built on, by the digest of its multi-architecture index, so the IDF Python environment copied out of it finds the python3 it was made with.
ARG ESP_BASE_IMAGE=ubuntu:24.04
ARG ESP_BASE_DIGEST=sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8
# Build-time module and tool downloads go through this proxy.
ARG GO_BUILD_PROXY=https://proxy.golang.org

# ---------------------------------------------------------------------------
FROM ${GO_IMAGE} AS tools
ARG GO_BUILD_PROXY
ARG TARGETARCH
ENV GOTOOLCHAIN=local CGO_ENABLED=0 GOPROXY=${GO_BUILD_PROXY} GOBIN=/out
RUN apk add --no-cache curl tar
COPY scripts/tool-pins.sh /tmp/tool-pins.sh
# gosec builds in a throwaway module so x/tools can be raised to XTOOLS_VERSION (see tool-pins.sh); the others build with the x/tools their go.mod names.
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

# github.com/jeremyhahn/* modules resolve from the forge through git, so this stage has git and base does not.
FROM ${GO_IMAGE} AS modules
ARG GO_BUILD_PROXY
ENV GOTOOLCHAIN=local GOFLAGS=-mod=readonly GOMODCACHE=/gomodcache GOPROXY=${GO_BUILD_PROXY}
RUN apk add --no-cache git
WORKDIR /src

# Download the module cache in its own layer.
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
    retry() { n=1; until "$@"; do [ "$n" -ge 3 ] && return 1; echo "retry $n: $*" >&2; n=$((n+1)); sleep $((n*5)); done; }; \
    for d in sdk/go test/integration/esp32 xkms; do (cd "$d" && retry go mod download); done
RUN for d in sdk/go test/integration/esp32 xkms; do (cd "$d" && go mod verify) || exit 1; done

# ---------------------------------------------------------------------------
FROM ${BASE_IMAGE}@${BASE_DIGEST} AS base

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

COPY scripts/tool-pins.sh /etc/tfw/tool-pins.sh
COPY docker/packages.txt /etc/tfw/packages.txt

# The compiler and runtime: every packages.txt line not tagged [toolchain].
ARG DEBIAN_REFRESH=2026-10-24
RUN echo "==> Debian security refresh ${DEBIAN_REFRESH}" \
    && apt-get update \
    && apt-get upgrade -y --no-install-recommends \
    && apt-get install -y --no-install-recommends \
      $(grep -v '#[[:space:]]*\[toolchain\]' /etc/tfw/packages.txt | sed 's/#.*//' | tr '\n' ' ') \
      ca-certificates curl xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Go, by version and checksum: the compiler is an input to the artefact the reproducible-build check pins.
RUN . /etc/tfw/tool-pins.sh \
    && curl -fsSLo /tmp/go.tgz "https://go.dev/dl/go${GO_VERSION}.linux-amd64.tar.gz" \
    && echo "${GO_SHA256}  /tmp/go.tgz" | sha256sum -c - \
    && tar -C /usr/local -xzf /tmp/go.tgz \
    && rm /tmp/go.tgz
ENV PATH="/usr/local/go/bin:${PATH}" \
    GOTOOLCHAIN=local \
    GOMODCACHE=/opt/go/mod

# The verified module cache of every module, read-only to everyone.
COPY --from=modules /gomodcache /opt/go/mod
RUN chmod -R a+rX /opt/go
ENV GOPROXY=off

# Any non-root uid runs it: HOME is /tmp, the build cache lives there.
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
      git jq bash make shellcheck cmake \
    && rm -rf /var/lib/apt/lists/* \
    && git config --system --add safe.directory '*'
COPY --from=tools /out/ /usr/local/bin/

# The toolchain versions are an input to every measurement this image makes, the size budget, the reproducible-build hash, so a number that moved can be attributed to the code or to the compiler.
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
# As the user, the writes the stages make outside the copied-in tree: a new top-level entry in /src, and HOME.
RUN mkdir /src/.probe "$HOME/.probe" && rmdir /src/.probe "$HOME/.probe"

# --- integration --------------------------------------------------------------
FROM base AS integration

# The source is baked in, so the suite runs on exactly what was scanned.
COPY --chown=10001:10001 . /src
RUN find / -xdev -perm /6000 -type f -exec chmod a-s {} + \
    && { echo "# trusted-firmware devcontainer (integration)"; \
         avr-gcc --version | head -1; gcc --version | head -1; go version; } > /etc/ci-toolchain.txt
ENV TFW_IMAGE_STAGE=integration
USER 10001:10001

# esp32-build: build-only; Espressif's image carries every chip's tools, the full ESP-IDF history and a compiler suite.
FROM ${ESP_IDF_IMAGE}@${ESP_IDF_DIGEST} AS esp32-build
# No pipefail: `qemu --version | head -n 1` may take SIGPIPE once head is done.
SHELL ["/bin/bash", "-c"]

# What the ADR pins, installed by ESP-IDF's own idf_tools.py.
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

# esp32: the image `make esp32-build` and `make esp32-qemu-test` run in, on the OS the ESP-IDF image is built on.
FROM ${ESP_BASE_IMAGE}@${ESP_BASE_DIGEST} AS esp32

ENV DEBIAN_FRONTEND=noninteractive
# `export.sh` puts the toolchains and ESP-IDF's Python environment on PATH.
SHELL ["/bin/bash", "-c"]

# make drives the targets, git names the build's version, python3 runs the IDF Python environment, ccache is what idf.py turns on when it finds it, binutils is the host objcopy platform-bake calls, and gcc the cgo compiler the ESP-IDF image had.
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

# Go, for the `wirecheck` host of the QEMU test and the Go tools esp32-build builds: base's install, from the same pin file, and base's module cache.
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

# The versions are an input to every measurement the job makes; recording them lets a result that moved be attributed to the code or to the toolchain.
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

# The same non-root uid as base, so scripts/in-toolchain-image.sh runs either image the same way.
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

# The proof, as the user: export.sh and an ESP-IDF example built the way esp32-build builds, once on each toolchain (esp32c3 RISC-V, esp32 Xtensa), so a write that still needs root, or a tool esp32-build dropped, fails the image build here rather than the job.
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

# The host packages the pinned OP-TEE build needs: every Debian package `make optee-tools` names (OPTEE_HOST_CMDS, OPTEE_HOST_LIBS, OPTEE_HOST_PY and python3-venv in mk/optee.mk), and meson and expect, which the page's CI table names.
USER root
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
      git curl python3 make build-essential patch perl file bzip2 xz-utils \
      unzip rsync wget cpio bc bison flex device-tree-compiler ninja-build \
      pkg-config libglib2.0-dev libpixman-1-dev libslirp-dev libgnutls28-dev \
      libssl-dev python3-cryptography python3-pyelftools python3-venv meson expect \
    && rm -rf /var/lib/apt/lists/* \
    && git config --system --add safe.directory '*'

# OPTEE_CACHE in CI: on disk in the container's layer, not in the 3 GB /tmp tmpfs, and the user's. scripts/in-toolchain-image.sh copies the runner's cached copy in here and out again (IN_IMAGE_CACHE).
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
