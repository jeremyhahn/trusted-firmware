# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# CI runs these same targets.

DEVCONTAINER         := build/docker/devcontainer
DEVCONTAINER_COMPOSE := $(DEVCONTAINER)/compose.yaml
DOCKER               ?= docker

# The toolchain and esp32 images are tagged by a hash of every input their stages read: the Dockerfile, the pin file, the package list (toolchain only) and the go.mod and go.sum the modules stage copies.
MODULE_INPUTS     := sdk/go/go.mod sdk/go/go.sum test/integration/esp32/go.mod \
                     test/integration/esp32/go.sum xkms/go.mod xkms/go.sum
TOOLCHAIN_INPUTS  := $(DEVCONTAINER)/Dockerfile scripts/tool-pins.sh docker/packages.txt $(MODULE_INPUTS)
ESP32_INPUTS      := $(DEVCONTAINER)/Dockerfile scripts/tool-pins.sh $(MODULE_INPUTS)
image_hash = $(shell sha256sum $(1) | sha256sum | cut -c1-16)
TOOLCHAIN_TAG     ?= $(call image_hash,$(TOOLCHAIN_INPUTS))
ESP32_TAG         ?= $(call image_hash,$(ESP32_INPUTS))
# The optee stage is from base, so it reads what the toolchain stage reads.
OPTEE_IMAGE_INPUTS := $(TOOLCHAIN_INPUTS)
OPTEE_IMAGE_TAG   ?= $(call image_hash,$(OPTEE_IMAGE_INPUTS))

# Where the images live and whose they are.
IMAGE_REGISTRY      ?=
IMAGE_CLASS         ?=
IMAGE_TRUSTED_CLASS ?=
# A build goes to <image>-$(IMAGE_BUILD_ID) and is then tagged <image>, so two runs building one hash on one daemon never export to the same name at once (buildkit refuses that); CI sets the run.
IMAGE_BUILD_ID      ?= build
docker_tag = DOCKER='$(DOCKER)' scripts/docker-tag.sh $(1) $(2)
image_ref = $(IMAGE_REGISTRY)trusted-firmware-$(1):$(2)$(if $(3),-$(3))
TOOLCHAIN_IMAGE   ?= $(call image_ref,toolchain,$(TOOLCHAIN_TAG),$(IMAGE_CLASS))
ESP32_IMAGE       ?= $(call image_ref,esp32,$(ESP32_TAG),$(IMAGE_CLASS))
TOOLCHAIN_TRUSTED := $(if $(IMAGE_TRUSTED_CLASS),$(call image_ref,toolchain,$(TOOLCHAIN_TAG),$(IMAGE_TRUSTED_CLASS)))
ESP32_TRUSTED     := $(if $(IMAGE_TRUSTED_CLASS),$(call image_ref,esp32,$(ESP32_TAG),$(IMAGE_TRUSTED_CLASS)))
OPTEE_IMAGE       ?= $(call image_ref,optee,$(OPTEE_IMAGE_TAG),$(IMAGE_CLASS))
OPTEE_IMAGE_TRUSTED := $(if $(IMAGE_TRUSTED_CLASS),$(call image_ref,optee,$(OPTEE_IMAGE_TAG),$(IMAGE_TRUSTED_CLASS)))
# The moving cache tags: the last pushed build of each class, the cache-from of the next build once an input moves the hash.
cache_ref = $(if $(IMAGE_REGISTRY),$(if $(2),$(IMAGE_REGISTRY)trusted-firmware-$(1):cache-$(2)))
TOOLCHAIN_CACHE   := $(call cache_ref,toolchain,$(IMAGE_CLASS))
ESP32_CACHE       := $(call cache_ref,esp32,$(IMAGE_CLASS))
OPTEE_IMAGE_CACHE := $(call cache_ref,optee,$(IMAGE_CLASS))
# The images carry their cache inline; with no registry the daemon's own BuildKit cache is the only one, so no --cache-from goes out.
cache_from_refs = $(if $(IMAGE_REGISTRY),$(foreach i,$(1),--cache-from $(i)))
cache_from = --build-arg BUILDKIT_INLINE_CACHE=1 $(call cache_from_refs,$(1))
TOOLCHAIN_CACHE_FROM = $(call cache_from,$(TOOLCHAIN_IMAGE) $(TOOLCHAIN_TRUSTED) $(TOOLCHAIN_CACHE) \
                       $(call cache_ref,toolchain,$(IMAGE_TRUSTED_CLASS)))
ESP32_CACHE_FROM     = $(call cache_from,$(ESP32_IMAGE) $(ESP32_TRUSTED) $(ESP32_CACHE) \
                       $(call cache_ref,esp32,$(IMAGE_TRUSTED_CLASS)))
OPTEE_IMAGE_CACHE_FROM = $(call cache_from,$(OPTEE_IMAGE) $(OPTEE_IMAGE_TRUSTED) $(OPTEE_IMAGE_CACHE) \
                       $(call cache_ref,optee,$(IMAGE_TRUSTED_CLASS)))
# IMAGE_PULL=1 lets scripts/ensure-image.sh pull from IMAGE_REGISTRY, and IMAGE_PUSH=1 push what it built (CI sets both only once it has logged in).
IMAGE_PULL        ?= 0
IMAGE_PUSH        ?= 0

# The integration image bakes the source in, so it is tagged by its whole build context: the commit's tree, on a dirty checkout the diff and every untracked file .gitignore keeps, and the one build arg the tree does not hold, GO_BUILD_PROXY (the pins are in scripts/tool-pins.sh).
IT_TAG             = $(eval IT_TAG := $(shell { git rev-parse 'HEAD^{tree}'; \
                       echo '$(GO_BUILD_PROXY)'; git diff HEAD --binary; \
                       git ls-files -z --others --exclude-standard | xargs -0r sha256sum; } | \
                       sha256sum | cut -c1-16))$(IT_TAG)
IT_IMAGE          ?= trusted-firmware-ci-it:$(IT_TAG)
IT_PROJECT        ?= trusted-firmware-it-local
# What the integration service runs: the suites, in place (mk/stages.mk).
IT_CMD            ?= make integration-test-host

# The forge credentials, as the gomodauth BuildKit secret: never a build arg, never a layer.
MODULE_ENV_FILE   ?=

# The pins, from the one pin file: from cannot read a file, so they go in as build args.
pin = $(shell . scripts/tool-pins.sh && echo "$$$(1)")
DOCKER_BUILD = DOCKER_BUILDKIT=1 $(DOCKER) build -f $(DEVCONTAINER)/Dockerfile \
               --build-arg GO_IMAGE=$(call pin,GO_IMAGE) \
               --build-arg BASE_IMAGE=$(call pin,BASE_IMAGE) \
               --build-arg BASE_DIGEST=$(call pin,BASE_DIGEST) \
               $(if $(GO_BUILD_PROXY),--build-arg "GO_BUILD_PROXY=$(GO_BUILD_PROXY)") \
               $(if $(MODULE_ENV_FILE),--secret id=gomodauth$(comma)src=$(MODULE_ENV_FILE))
comma := ,

# Run as the invoking user, not root.
DOCKER_USER := --user $(shell id -u):$(shell id -g)
# HOME so the Go build cache lands somewhere writable rather than in /.
DOCKER_ENV  := -e HOME=/tmp

CMD ?= bash

## toolchain-image: print the toolchain image's name (`make -s toolchain-image`)
toolchain-image:
	@echo $(TOOLCHAIN_IMAGE)

## esp32-image: print the esp32 image's name (`make -s esp32-image`)
esp32-image:
	@echo $(ESP32_IMAGE)

## optee-image: print the optee image's name (`make -s optee-image`)
optee-image:
	@echo $(OPTEE_IMAGE)

## it-image: print the integration image's name (`make -s it-image`)
it-image:
	@echo $(IT_IMAGE)

## toolchain-hash: print the hash of the toolchain image's inputs, its tag
toolchain-hash:
	@echo $(TOOLCHAIN_TAG)

## esp32-hash: print the hash of the esp32 image's inputs, its tag
esp32-hash:
	@echo $(ESP32_TAG)

## docker-build: build the toolchain, integration and esp32 images
docker-build: docker-build-it docker-build-toolchain docker-build-esp32

## docker-images: reuse the toolchain and esp32 images by hash, else build them (CI's image job)
docker-images: docker-image-toolchain docker-image-esp32

## docker-image-toolchain: reuse the toolchain image by hash, else build it (a CI leg that needs only it)
docker-image-toolchain:
	@IMAGE_PULL=$(IMAGE_PULL) IMAGE_PUSH=$(IMAGE_PUSH) IMAGE_CACHE_REF=$(TOOLCHAIN_CACHE) \
	  scripts/ensure-image.sh docker-build-toolchain $(TOOLCHAIN_IMAGE) $(TOOLCHAIN_TRUSTED)

## docker-image-esp32: reuse the esp32 image by hash, else build it (the CI leg that runs the ESP32 stages)
docker-image-esp32:
	@IMAGE_PULL=$(IMAGE_PULL) IMAGE_PUSH=$(IMAGE_PUSH) IMAGE_CACHE_REF=$(ESP32_CACHE) \
	  scripts/ensure-image.sh docker-build-esp32 $(ESP32_IMAGE) $(ESP32_TRUSTED)

## docker-image-optee: reuse the optee image by hash, else build it (the CI leg that runs the OP-TEE stack)
docker-image-optee:
	@IMAGE_PULL=$(IMAGE_PULL) IMAGE_PUSH=$(IMAGE_PUSH) IMAGE_CACHE_REF=$(OPTEE_IMAGE_CACHE) \
	  scripts/ensure-image.sh docker-build-optee $(OPTEE_IMAGE) $(OPTEE_IMAGE_TRUSTED)

## docker-build-toolchain: build the toolchain image (TOOLCHAIN_IMAGE)
docker-build-toolchain:
	@$(DOCKER_BUILD) --target toolchain $(TOOLCHAIN_CACHE_FROM) --label tfw.image.inputs=$(TOOLCHAIN_TAG) -t $(TOOLCHAIN_IMAGE)-$(IMAGE_BUILD_ID) .
	@$(call docker_tag,$(TOOLCHAIN_IMAGE)-$(IMAGE_BUILD_ID),$(TOOLCHAIN_IMAGE))
	@$(DOCKER) run --rm $(TOOLCHAIN_IMAGE)-$(IMAGE_BUILD_ID) cat /etc/ci-toolchain.txt
	@$(DOCKER) image rm $(TOOLCHAIN_IMAGE)-$(IMAGE_BUILD_ID) >/dev/null

## docker-build-esp32: build the esp32 image (ESP32_IMAGE)
docker-build-esp32:
	@$(DOCKER_BUILD) --target esp32 $(ESP32_CACHE_FROM) --label tfw.image.inputs=$(ESP32_TAG) -t $(ESP32_IMAGE)-$(IMAGE_BUILD_ID) .
	@$(call docker_tag,$(ESP32_IMAGE)-$(IMAGE_BUILD_ID),$(ESP32_IMAGE))
	@$(DOCKER) image rm $(ESP32_IMAGE)-$(IMAGE_BUILD_ID) >/dev/null

## docker-build-optee: build the optee image (OPTEE_IMAGE)
docker-build-optee:
	@$(DOCKER_BUILD) --target optee $(OPTEE_IMAGE_CACHE_FROM) --label tfw.image.inputs=$(OPTEE_IMAGE_TAG) -t $(OPTEE_IMAGE)-$(IMAGE_BUILD_ID) .
	@$(call docker_tag,$(OPTEE_IMAGE)-$(IMAGE_BUILD_ID),$(OPTEE_IMAGE))
	@$(DOCKER) image rm $(OPTEE_IMAGE)-$(IMAGE_BUILD_ID) >/dev/null

## docker-build-it: build the integration image (IT_IMAGE), the source baked in, unless the daemon holds it
docker-build-it:
	@if $(DOCKER) image inspect $(IT_IMAGE) >/dev/null 2>&1; then \
	  echo "==> $(IT_IMAGE) is on the daemon; reusing it"; \
	else \
	  $(DOCKER_BUILD) --target integration \
	    $(call cache_from_refs,$(TOOLCHAIN_IMAGE) $(TOOLCHAIN_TRUSTED) $(TOOLCHAIN_CACHE)) \
	    -t $(IT_IMAGE)-$(IMAGE_BUILD_ID) . && \
	  $(DOCKER) tag $(IT_IMAGE)-$(IMAGE_BUILD_ID) $(IT_IMAGE) && \
	  $(DOCKER) image rm $(IT_IMAGE)-$(IMAGE_BUILD_ID) >/dev/null; \
	fi

## docker-run: run CMD, a shell by default, in the toolchain image with the tree mounted
docker-run:
	@$(DOCKER) run --rm -it $(DOCKER_USER) $(DOCKER_ENV) -v "$(CURDIR)":/src $(TOOLCHAIN_IMAGE) $(CMD)

## docker-ci: run the whole CI gate inside the toolchain image
docker-ci:
	@$(DOCKER) run --rm $(DOCKER_USER) $(DOCKER_ENV) -v "$(CURDIR)":/src $(TOOLCHAIN_IMAGE) make ci

## docker-firmware: build the firmware for BOARD inside the toolchain image
docker-firmware:
	@$(DOCKER) run --rm $(DOCKER_USER) $(DOCKER_ENV) -v "$(CURDIR)":/src $(TOOLCHAIN_IMAGE) make firmware BOARD=$(BOARD)

.PHONY: optee-image docker-image-optee docker-build-optee toolchain-image esp32-image it-image toolchain-hash esp32-hash docker-build docker-images docker-image-toolchain docker-image-esp32 docker-build-toolchain docker-build-esp32 docker-build-it docker-run docker-ci docker-firmware
