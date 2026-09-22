# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# CI runs these same targets. A developer who can run `make docker-ci` locally
# gets the same answer the gate will give, which is the only way a gate stays
# trusted rather than argued with.

CI_IMAGE ?= arduino-generic-ci:local
DOCKER   ?= docker

## docker-build: build the CI toolchain image
docker-build:
	@$(DOCKER) build -f docker/Dockerfile.ci -t $(CI_IMAGE) .
	@$(DOCKER) run --rm $(CI_IMAGE) cat /etc/ci-toolchain.txt

## docker-run: open a shell in the toolchain image with the tree mounted
docker-run:
	@$(DOCKER) run --rm -it -v "$(CURDIR)":/src $(CI_IMAGE) bash

## docker-ci: run the whole CI gate inside the toolchain image
docker-ci:
	@$(DOCKER) run --rm -v "$(CURDIR)":/src $(CI_IMAGE) make ci

## docker-firmware: build the firmware for BOARD inside the toolchain image
docker-firmware:
	@$(DOCKER) run --rm -v "$(CURDIR)":/src $(CI_IMAGE) make firmware BOARD=$(BOARD)

.PHONY: docker-build docker-run docker-ci docker-firmware
