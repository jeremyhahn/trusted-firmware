# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# CI runs these same targets. A developer who can run `make docker-ci` locally
# gets the same answer the gate will give, which is the only way a gate stays
# trusted rather than argued with.
#
# The image is the devcontainer, build/docker/devcontainer/Dockerfile (handbook
# devcontainer.md §1, §6): its `toolchain` stage runs build, test, security and
# deps-check, its `integration` stage the integration suite, its `esp32` stage
# the ESP-IDF build and the QEMU test. `make docker-build` builds all three.
# CI splits them over three parallel jobs, each building only the images it
# runs: `image-toolchain` (docker-build-toolchain), `image-security`
# (docker-build-toolchain and docker-build-it, for the trivy scan) and
# `image-esp32` (docker-build-esp32), each stage through
# scripts/in-toolchain-image.sh; the `integration` job runs `make
# integration-test` (mk/stages.mk).

DEVCONTAINER         := build/docker/devcontainer
DEVCONTAINER_COMPOSE := $(DEVCONTAINER)/compose.yaml
DOCKER               ?= docker

# One tag per commit, so two runs on one daemon never build into the same name
# (buildkit refuses to export two images to one tag at once).
TOOLCHAIN_TAG     ?= $(shell git rev-parse --short=12 HEAD 2>/dev/null || echo local)
TOOLCHAIN_IMAGE   ?= trusted-firmware-toolchain:$(TOOLCHAIN_TAG)
ESP32_IMAGE       ?= trusted-firmware-esp32:$(TOOLCHAIN_TAG)
IT_IMAGE          ?= trusted-firmware-it:local
IT_PROJECT        ?= trusted-firmware-it-local
# What the integration service runs: the suites, in place (mk/stages.mk).
IT_CMD            ?= make integration-test-host

# The forge credentials (the K=V lines dependency-resolution.md §6 names), as
# the gomodauth BuildKit secret: never a build arg, never a layer. Unset or
# empty, the modules resolve from the public proxy and xkms/'s go-xkms fails
# the build, loudly.
MODULE_ENV_FILE   ?=

# The pins, from the one pin file (devcontainer.md §2): FROM cannot read a file,
# so they go in as build args. The RUN steps source the file.
pin = $(shell . scripts/tool-pins.sh && echo "$$$(1)")
DOCKER_BUILD = DOCKER_BUILDKIT=1 $(DOCKER) build -f $(DEVCONTAINER)/Dockerfile \
               --build-arg GO_IMAGE=$(call pin,GO_IMAGE) \
               --build-arg BASE_IMAGE=$(call pin,BASE_IMAGE) \
               --build-arg BASE_DIGEST=$(call pin,BASE_DIGEST) \
               $(if $(GO_BUILD_PROXY),--build-arg GO_BUILD_PROXY=$(GO_BUILD_PROXY)) \
               $(if $(MODULE_ENV_FILE),--secret id=gomodauth$(comma)src=$(MODULE_ENV_FILE))
comma := ,

# Run as the invoking user, not root. Without this the container writes
# build artefacts owned by root into a bind-mounted tree, and the next `make
# clean` on the host fails with a permission error on files the developer
# appears to own the directory of. Any uid works (devcontainer.md §5).
DOCKER_USER := --user $(shell id -u):$(shell id -g)
# HOME so the Go build cache lands somewhere writable rather than in /. The
# module cache is the image's own, pre-warmed and read-only, with GOPROXY=off.
DOCKER_ENV  := -e HOME=/tmp

CMD ?= bash

## toolchain-image: print the toolchain image's name (`make -s toolchain-image`)
toolchain-image:
	@echo $(TOOLCHAIN_IMAGE)

## esp32-image: print the esp32 image's name (`make -s esp32-image`)
esp32-image:
	@echo $(ESP32_IMAGE)

## docker-build: build the toolchain, integration and esp32 images
docker-build: docker-build-it docker-build-toolchain docker-build-esp32

## docker-build-toolchain: build the toolchain image (TOOLCHAIN_IMAGE)
docker-build-toolchain:
	@$(DOCKER_BUILD) --target toolchain -t $(TOOLCHAIN_IMAGE) .
	@$(DOCKER) run --rm $(TOOLCHAIN_IMAGE) cat /etc/ci-toolchain.txt

## docker-build-esp32: build the esp32 image (ESP32_IMAGE)
docker-build-esp32:
	@$(DOCKER_BUILD) --target esp32 -t $(ESP32_IMAGE) .

## docker-build-it: build the integration image (IT_IMAGE), the source baked in
docker-build-it:
	@$(DOCKER_BUILD) --target integration -t $(IT_IMAGE) .

## docker-run: run CMD, a shell by default, in the toolchain image with the tree mounted
docker-run:
	@$(DOCKER) run --rm -it $(DOCKER_USER) $(DOCKER_ENV) -v "$(CURDIR)":/src $(TOOLCHAIN_IMAGE) $(CMD)

## docker-ci: run the whole CI gate inside the toolchain image
docker-ci:
	@$(DOCKER) run --rm $(DOCKER_USER) $(DOCKER_ENV) -v "$(CURDIR)":/src $(TOOLCHAIN_IMAGE) make ci

## docker-firmware: build the firmware for BOARD inside the toolchain image
docker-firmware:
	@$(DOCKER) run --rm $(DOCKER_USER) $(DOCKER_ENV) -v "$(CURDIR)":/src $(TOOLCHAIN_IMAGE) make firmware BOARD=$(BOARD)

.PHONY: toolchain-image esp32-image docker-build docker-build-toolchain docker-build-esp32 docker-build-it docker-run docker-ci docker-firmware
