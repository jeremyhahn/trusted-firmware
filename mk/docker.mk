# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# CI runs these same targets. A developer who can run `make docker-ci` locally
# gets the same answer the gate will give, which is the only way a gate stays
# trusted rather than argued with.

CI_IMAGE ?= trusted-firmware-ci:local
DOCKER   ?= docker

# Run as the invoking user, not root. Without this the container writes
# build artefacts owned by root into a bind-mounted tree, and the next `make
# clean` on the host fails with a permission error on files the developer
# appears to own the directory of. Costs nothing and removes a papercut that
# otherwise gets "fixed" with sudo.
DOCKER_USER := --user $(shell id -u):$(shell id -g)
# HOME so the Go build cache lands somewhere writable rather than in /
DOCKER_ENV  := -e HOME=/tmp -e GOCACHE=/tmp/.gocache -e GOMODCACHE=/tmp/.gomodcache

## docker-build: build the CI toolchain image
docker-build:
	@$(DOCKER) build -f docker/Dockerfile.ci -t $(CI_IMAGE) .
	@$(DOCKER) run --rm $(CI_IMAGE) cat /etc/ci-toolchain.txt

## docker-run: open a shell in the toolchain image with the tree mounted
docker-run:
	@$(DOCKER) run --rm -it $(DOCKER_USER) $(DOCKER_ENV) -v "$(CURDIR)":/src $(CI_IMAGE) bash

## docker-ci: run the whole CI gate inside the toolchain image
docker-ci:
	@$(DOCKER) run --rm $(DOCKER_USER) $(DOCKER_ENV) -v "$(CURDIR)":/src $(CI_IMAGE) make ci

## docker-firmware: build the firmware for BOARD inside the toolchain image
docker-firmware:
	@$(DOCKER) run --rm $(DOCKER_USER) $(DOCKER_ENV) -v "$(CURDIR)":/src $(CI_IMAGE) make firmware BOARD=$(BOARD)

.PHONY: docker-build docker-run docker-ci docker-firmware
