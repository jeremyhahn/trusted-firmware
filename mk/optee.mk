# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# OP-TEE on QEMU virt, the secure world go-enclave's arm-trustzone backend
# runs against (#253; the design is docs/optee-trustzone/README.md). This file
# pins one upstream OP-TEE release and fetches, builds and boots it. Upstream
# is composed, never vendored or patched: the release's manifest names the
# revision of every component, QEMU and the toolchains included.
#
# Outside the AVR and Cortex-M object lists, and NOT IN `make ci`: one build is
# several GB and tens of minutes, once per pin, and needs host packages the
# lane image does not carry. CI runs it as a job of its own (the design's
# slice 4).
#
# go-enclave's CI calls `make optee-run` at a trusted-firmware tag, so the
# targets and their two variables, OPTEE_SHARE and OPTEE_GUEST_CMD, are an
# interface: they extend, and do not change meaning.

# ------------------------------------------------------------------- the pin
# One OP-TEE release tag. The manifest commit is the tag's, recorded so a tag
# that moved upstream is refused, not followed. The manifest pins every
# repository it names to a tag or a commit of that release, never to a branch.
# A newer release is a PR of its own that re-runs every OP-TEE test.
OPTEE_RELEASE         := 4.11.0
OPTEE_MANIFEST_URL    := https://github.com/OP-TEE/manifest.git
OPTEE_MANIFEST_COMMIT := b95d496b47c6548e3e196adad75e78bde115a18c
OPTEE_MANIFEST_FILE   := qemu_v8.xml

# The GlobalPlatform TEE Internal Core API version that release's optee_os
# declares (TEE_CORE_API_{MAJOR,MINOR,MAINTENANCE}_VERSION in
# lib/libutee/include/tee_api_defines.h). The fetch checks the tree against it,
# so a pin and the version docs/reference/README.md names cannot drift apart.
OPTEE_GP_CORE_API := 1.3.1

# Google's repo, the tool a manifest is written for, pinned the same way: a
# tag, and the commit it must resolve to.
OPTEE_REPO_URL    := https://gerrit.googlesource.com/git-repo
OPTEE_REPO_TAG    := v2.70
OPTEE_REPO_COMMIT := 46e9866ca0f161604bd45a0c3b7c7887c9415dcb

# ------------------------------------------------------------------ the cache
# Sources, toolchains and the build live outside the checkout, one directory
# per pin, and survive `make clean`. A cache inside the checkout is refused:
# several GB of upstream trees would sit under every gate that walks the tree.
OPTEE_CACHE ?= $(HOME)/.cache/trusted-firmware/optee
OPTEE_ROOT     = $(OPTEE_CACHE)/$(OPTEE_RELEASE)
OPTEE_REPO_DIR = $(OPTEE_CACHE)/git-repo-$(OPTEE_REPO_TAG)
OPTEE_SYNCED   = $(OPTEE_ROOT)/.tfw-synced
OPTEE_TOOLS    = $(OPTEE_ROOT)/.tfw-toolchains
# Named for the TA: a cache built before the TA existed has only .tfw-built.
OPTEE_BUILT    = $(OPTEE_ROOT)/.tfw-built-ta

# The TA and tfw-ta-check (#253), built from optee/ into the cache and put in
# the guest's root filesystem by a second Buildroot overlay: the TA where
# tee-supplicant loads TAs from, the check on PATH. The UUID is the TA's file
# name: optee/ta/include/tfw_ta.h.
OPTEE_TA_UUID := 23751b49-3a76-4e49-b63c-e5853742fa1f
OPTEE_TFW      = $(OPTEE_ROOT)/tfw
OPTEE_OVERLAY  = $(OPTEE_TFW)/overlay
# Buildroot's staging tree: the guest's C library and the libteec it staged.
OPTEE_STAGING  = $(OPTEE_ROOT)/out-br/staging

OPTEE_JOBS ?= $(shell nproc 2>/dev/null || echo 1)

# The guest mounts OPTEE_SHARE (virtio-9p, tag host) at this path from its
# fstab, and OPTEE_GUEST_CMD runs there. With no OPTEE_SHARE, an empty
# directory in the cache is mounted. Both reach the recipes through the
# environment, as written: their quotes, and a $, are the guest shell's to
# read, not make's.
OPTEE_SHARE     ?=
OPTEE_GUEST_CMD ?=
override OPTEE_SHARE     := $(value OPTEE_SHARE)
override OPTEE_GUEST_CMD := $(value OPTEE_GUEST_CMD)
OPTEE_MOUNT     := /mnt/host
export OPTEE_SHARE OPTEE_GUEST_CMD

# The pinned build repository's own options, nothing patched:
#   RUST_ENABLE=n   the Rust examples need a rustup toolchain nothing here runs
#   QEMU_VIRTFS_*   QEMU built with virtfs, and the guest's fstab mounting it
#   BR2_ROOTFS_OVERLAY  qemu_v8.mk's own overlay, then OPTEE_OVERLAY, both as
#                   full paths from OPTEE_ROOT: a $(ROOT) passed through reaches
#                   Buildroot unexpanded, where ROOT is empty (run 70157)
OPTEE_BUILD_FLAGS = RUST_ENABLE=n QEMU_VIRTFS_ENABLE=y QEMU_VIRTFS_AUTOMOUNT=y \
                    QEMU_VIRTFS_MOUNTPOINT=$(OPTEE_MOUNT) \
                    'BR2_ROOTFS_OVERLAY=$(OPTEE_ROOT)/build/br-ext/board/qemu/overlay $(OPTEE_OVERLAY)'

# Upstream's make runs with none of this make's flags or command-line
# variables: --no-builtin-rules, or a BOARD or ARCH given here, is not its.
OPTEE_MAKE = env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL -u MAKEOVERRIDES \
             make --no-print-directory -C $(OPTEE_ROOT)/build $(OPTEE_BUILD_FLAGS)

# ------------------------------------------------------------- host packages
# What the pinned build needs from the host, as `command:Debian package`. The
# toolchains and QEMU are not here: the manifest fetches or builds them.
# `file` is checked at /usr/bin/file because Buildroot checks that path, not PATH.
OPTEE_HOST_CMDS := git:git curl:curl python3:python3 make:make gcc:build-essential \
                   g++:build-essential patch:patch perl:perl /usr/bin/file:file bzip2:bzip2 \
                   xz:xz-utils unzip:unzip rsync:rsync wget:wget cpio:cpio bc:bc \
                   bison:bison flex:flex dtc:device-tree-compiler ninja:ninja-build \
                   pkg-config:pkg-config
OPTEE_HOST_LIBS := glib-2.0:libglib2.0-dev pixman-1:libpixman-1-dev slirp:libslirp-dev \
                   gnutls:libgnutls28-dev openssl:libssl-dev
OPTEE_HOST_PY   := cryptography:python3-cryptography elftools:python3-pyelftools
# And QEMU's configure, which makes a venv with pip: ensurepip. pip and
# setuptools in its place do not do (its mkvenv asks for ensurepip regardless).

# Refuses a cache inside the checkout, before anything is fetched or made.
define optee-cache-check
	@case "$(abspath $(OPTEE_CACHE))/" in "$(CURDIR)/"*) \
	   echo "$@: OPTEE_CACHE=$(OPTEE_CACHE) is inside the checkout: set it outside $(CURDIR)"; \
	   exit 1;; esac
endef

## optee-pin: print the pinned OP-TEE release tag (`make -s optee-pin`), CI's cache key for OPTEE_CACHE
optee-pin:
	@echo $(OPTEE_RELEASE)

## optee-tools: check the host packages OP-TEE's build needs, fetch the pinned release and its toolchains into OPTEE_CACHE
optee-tools:
	$(optee-cache-check)
	@miss=0; \
	for t in $(OPTEE_HOST_CMDS); do command -v $${t%%:*} >/dev/null || { \
	   echo "$@: missing $${t%%:*}: install the Debian package $${t#*:}"; miss=1; }; done; \
	for t in $(OPTEE_HOST_LIBS); do pkg-config --exists $${t%%:*} 2>/dev/null || { \
	   echo "$@: missing the library $${t%%:*}: install the Debian package $${t#*:}"; miss=1; }; done; \
	for t in $(OPTEE_HOST_PY); do python3 -c "import $${t%%:*}" 2>/dev/null || { \
	   echo "$@: missing the Python module $${t%%:*}: install the Debian package $${t#*:}"; miss=1; }; done; \
	python3 -c "import ensurepip" 2>/dev/null || { \
	   echo "$@: missing the Python module ensurepip (QEMU's configure makes a venv with pip): install the Debian package python3-venv"; miss=1; }; \
	[ $$miss = 0 ] || { echo "$@: the host cannot build OP-TEE $(OPTEE_RELEASE)"; exit 1; }
	@$(MAKE) --no-print-directory $(OPTEE_TOOLS)
	@echo "optee-tools: OP-TEE $(OPTEE_RELEASE) and its toolchains are in $(OPTEE_ROOT)"

# The TA against the TA dev kit the first pass exported, and tfw-ta-check
# against the libteec Buildroot staged, each with the toolchain the pinned
# build fetched, both read from its make; then both into OPTEE_OVERLAY.
# Upstream's make flags reach neither sub-make, as for OPTEE_MAKE.
define optee-tfw-build
	@set -e; vars=$$($(OPTEE_MAKE) -s \
	   --eval 'tfw-ta-vars: ; @:$$(info $$(AARCH64_CROSS_COMPILE) $$(OPTEE_OS_TA_DEV_KIT_DIR))' \
	   tfw-ta-vars | tail -n 1); \
	cc=$${vars%% *}; kit=$${vars#* }; \
	[ -f "$$kit/mk/ta_dev_kit.mk" ] || { echo "$@: no TA dev kit at $$kit"; exit 1; }; \
	[ -f $(OPTEE_STAGING)/usr/include/tee_client_api.h ] || { \
	   echo "$@: Buildroot staged no tee_client_api.h in $(OPTEE_STAGING)/usr"; exit 1; }; \
	env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL -u MAKEOVERRIDES make --no-print-directory \
	   -C $(CURDIR)/optee/ta CROSS_COMPILE="$$cc" TA_DEV_KIT_DIR="$$kit" O=$(OPTEE_TFW)/ta; \
	env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL -u MAKEOVERRIDES make --no-print-directory \
	   -C $(CURDIR)/optee/check CROSS_COMPILE="$$cc" \
	   SYSROOT=$(OPTEE_STAGING) TEEC_EXPORT=$(OPTEE_STAGING)/usr O=$(OPTEE_TFW)/check; \
	install -D -m 0444 $(OPTEE_TFW)/ta/$(OPTEE_TA_UUID).ta \
	   $(OPTEE_OVERLAY)/lib/optee_armtz/$(OPTEE_TA_UUID).ta; \
	install -D -m 0755 $(OPTEE_TFW)/check/tfw-ta-check $(OPTEE_OVERLAY)/usr/bin/tfw-ta-check
endef

## optee-qemu: build the pinned OP-TEE stack for QEMU virt (v8): TF-A, OP-TEE OS, Linux, Buildroot with xtest, and QEMU, the TA and tfw-ta-check in its root filesystem
# Two passes of upstream's all: the first exports the TA dev kit and stages
# libteec, which the TA and the check build against; the second puts
# OPTEE_OVERLAY, holding both, in the root filesystem. No upstream file changes.
optee-qemu: optee-tools
	@mkdir -p $(OPTEE_OVERLAY)
	@$(OPTEE_MAKE) -j$(OPTEE_JOBS) all
	$(optee-tfw-build)
	@$(OPTEE_MAKE) -j$(OPTEE_JOBS) all
	@ln -sf ../../out-br/images/rootfs.cpio.gz $(OPTEE_ROOT)/out/bin/rootfs.cpio.gz
	@touch $(OPTEE_BUILT)
	@echo "optee-qemu: OP-TEE $(OPTEE_RELEASE) is built in $(OPTEE_ROOT)"

## optee-run: boot the OP-TEE stack on QEMU virt headless, OPTEE_SHARE mounted, and exit with OPTEE_GUEST_CMD's status
# Every refusal comes before anything is fetched or built. The QEMU command
# line is the pinned build's own headless one (QEMU_CHECK_ARGS) with its
# virtfs share (QEMU_RUN_ARGS_COMMON), read from its make; QEMU parses the
# share's path as a list, so a comma or a space in it is refused.
optee-run:
	$(optee-cache-check)
	@[ -n "$$OPTEE_GUEST_CMD" ] || { \
	   echo "$@: OPTEE_GUEST_CMD names nothing to run, e.g. OPTEE_GUEST_CMD='xtest -l 0'"; exit 1; }
	@[ -z "$$OPTEE_SHARE" ] || [ -d "$$OPTEE_SHARE" ] || { \
	   echo "$@: OPTEE_SHARE=$$OPTEE_SHARE is not a directory"; exit 1; }
	@case "$$OPTEE_SHARE" in *[,\ ]*) \
	   echo "$@: OPTEE_SHARE=$$OPTEE_SHARE has a comma or a space, which QEMU cannot mount"; exit 1;; esac
	@[ -f $(OPTEE_BUILT) ] || $(MAKE) --no-print-directory optee-qemu
	@share=$${OPTEE_SHARE:-$(OPTEE_ROOT)/share}; mkdir -p "$$share" && share=$$(cd "$$share" && pwd) && \
	qemu=$$($(OPTEE_MAKE) -s QEMU_VIRTFS_HOST_DIR="$$share" \
	   --eval 'tfw-qemu-cmd: ; @:$$(info $$(QEMU_BIN) $$(QEMU_CHECK_ARGS) $$(QEMU_RUN_ARGS_COMMON))' \
	   tfw-qemu-cmd | tail -n 1) && \
	cd $(OPTEE_ROOT)/out/bin && \
	python3 -B $(CURDIR)/scripts/optee_run.py --mount $(OPTEE_MOUNT) --cmd "$$OPTEE_GUEST_CMD" "$$qemu"

## integration-test-optee: optee-run with xtest and then tfw-ta-check, which calls the TA's version, rng, SEAL and UNSEAL commands; fails on any failure of either
# The guest's status is the gate; the console, kept in the cache, must also
# show both commands answering and every one of the check's 14 cases passing.
OPTEE_IT_LOG = $(OPTEE_TFW)/integration-test.log
integration-test-optee:
	$(optee-cache-check)
	@[ -f $(OPTEE_BUILT) ] || $(MAKE) --no-print-directory optee-qemu
	@mkdir -p $(OPTEE_TFW) && rm -f $(OPTEE_IT_LOG) $(OPTEE_IT_LOG).rc
	@{ $(MAKE) --no-print-directory optee-run OPTEE_SHARE= \
	     OPTEE_GUEST_CMD='xtest -l 0 && tfw-ta-check'; echo $$? > $(OPTEE_IT_LOG).rc; } | \
	   tee $(OPTEE_IT_LOG)
	@rc=$$(cat $(OPTEE_IT_LOG).rc); [ "$$rc" = 0 ] || { \
	   echo "$@: the guest's xtest and tfw-ta-check exited $$rc: $(OPTEE_IT_LOG)"; exit 1; }
	@tr -d '\r' < $(OPTEE_IT_LOG) > $(OPTEE_IT_LOG).txt && \
	for line in 'tfw-ta-check: ok get-info' 'tfw-ta-check: ok get-random' \
	            'tfw-ta-check: ok seal-unseal' 'tfw-ta-check: ok unseal-mac-invalid' \
	            'tfw-ta-check: PASS, 18 of 18 passed'; do \
	   grep -qxF "$$line" $(OPTEE_IT_LOG).txt || { \
	     echo "$@: the console has no line '$$line': $(OPTEE_IT_LOG)"; exit 1; }; done
	@echo "integration-test-optee: xtest and tfw-ta-check pass on OP-TEE $(OPTEE_RELEASE)"

# The pinned repo tool, at the tag, and the commit checked. A full clone: repo
# init clones repo again from this one by tag, which a shallow clone cannot
# serve.
$(OPTEE_REPO_DIR)/.tfw-fetched:
	@rm -rf $(OPTEE_REPO_DIR)
	@git -c advice.detachedHead=false clone -q --branch $(OPTEE_REPO_TAG) $(OPTEE_REPO_URL) $(OPTEE_REPO_DIR)
	@got=$$(git -C $(OPTEE_REPO_DIR) rev-parse HEAD); [ "$$got" = $(OPTEE_REPO_COMMIT) ] || { \
	   echo "repo $(OPTEE_REPO_TAG) is $$got upstream, not the pinned $(OPTEE_REPO_COMMIT)"; exit 1; }
	@touch $@

# The pinned manifest and every repository it names. repo's own signature
# check is off because the commit check below is the pin; the manifest's
# clone-depth keeps each tree to its one revision.
$(OPTEE_SYNCED): $(OPTEE_REPO_DIR)/.tfw-fetched
	@mkdir -p $(OPTEE_ROOT)
	@cd $(OPTEE_ROOT) && python3 $(OPTEE_REPO_DIR)/repo init -q --no-repo-verify \
	   --repo-url=$(OPTEE_REPO_DIR) --repo-rev=$(OPTEE_REPO_TAG) \
	   -u $(OPTEE_MANIFEST_URL) -b refs/tags/$(OPTEE_RELEASE) -m $(OPTEE_MANIFEST_FILE)
	@got=$$(git -C $(OPTEE_ROOT)/.repo/manifests rev-parse HEAD); [ "$$got" = $(OPTEE_MANIFEST_COMMIT) ] || { \
	   echo "OP-TEE $(OPTEE_RELEASE) is $$got upstream, not the pinned $(OPTEE_MANIFEST_COMMIT)"; exit 1; }
	@cd $(OPTEE_ROOT) && python3 $(OPTEE_REPO_DIR)/repo sync -q -j$(OPTEE_JOBS) --no-clone-bundle
	@printf '#include <tee_api_defines.h>\n_Static_assert(TEE_CORE_API_MAJOR_VERSION == $(word 1,$(subst ., ,$(OPTEE_GP_CORE_API))) && TEE_CORE_API_MINOR_VERSION == $(word 2,$(subst ., ,$(OPTEE_GP_CORE_API))) && TEE_CORE_API_MAINTENANCE_VERSION == $(word 3,$(subst ., ,$(OPTEE_GP_CORE_API))), "GP");\n' | \
	   gcc -fsyntax-only -x c -I$(OPTEE_ROOT)/optee_os/lib/libutee/include \
	       -I$(OPTEE_ROOT)/optee_os/lib/libutils/ext/include - || { \
	   echo "optee_os $(OPTEE_RELEASE) does not declare the GP Internal Core API $(OPTEE_GP_CORE_API)"; exit 1; }
	@touch $@

# The toolchains the pinned build repository names, fetched by its own target.
$(OPTEE_TOOLS): $(OPTEE_SYNCED)
	@$(OPTEE_MAKE) toolchains
	@touch $@

.PHONY: optee-pin optee-tools optee-qemu optee-run integration-test-optee
