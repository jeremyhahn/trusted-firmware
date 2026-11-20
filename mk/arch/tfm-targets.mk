# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# TF-M v2.3.1, the TrustZone-M secure world (#608 slice 2, ruling 1024 (a)):
# Profile Medium, BL2 on, Initial Attestation off (ruling 935: no PSA token),
# with tf-m-extras' example non-secure application. Included for every
# invocation (mk/arch/*-targets.mk), as the Cortex-M QEMU targets are.
#
# TF-M stays out of tree: scripts/tfm-build.sh fetches it at the pins
# scripts/tfm-measure.sh measured into TFM_WORK and builds there. Not in
# `make ci`, and no CI job yet (slice 2 is host-only).

# Our board name -> TF-M's platform. The QEMU target is TF-M's own an521 (an
# M33 QEMU emulates as mps2-an521), not a port of ours for AN505; the
# NUCLEO-H563ZI is a port derived from stm32h573i_dk in slice 4.
TFM_BOARDS := an521 stm32h573i-dk
TFM_BOARD  ?= an521

# Outside the checkout, as OPTEE_CACHE is: nothing TF-M builds enters the tree.
TFM_WORK ?= $(HOME)/.cache/trusted-firmware/tfm
TFM_OUT   = $(TFM_WORK)/out/$(TFM_BOARD)

# The first line tf-m-extras' example non-secure application prints, at the
# pinned tf-m-extras commit (examples/tf-m-example-ns-app/main.c).
TFM_QEMU_NS_MARKER ?= Non-Secure system starting

## tfm-image: build TF-M v2.3.1 (BL2, secure, example non-secure; signed) for TFM_BOARD=an521|stm32h573i-dk, out of tree in TFM_WORK
tfm-image:
	@case " $(TFM_BOARDS) " in *" $(TFM_BOARD) "*) ;; *) \
	   echo "$@: TFM_BOARD=$(TFM_BOARD) is not one of: $(TFM_BOARDS)"; exit 1;; esac
	@case "$(abspath $(TFM_WORK))/" in "$(CURDIR)/"*) \
	   echo "$@: TFM_WORK=$(TFM_WORK) is inside the checkout: set it outside $(CURDIR)"; \
	   exit 1;; esac
	@scripts/tfm-build.sh $(TFM_BOARD) $(TFM_WORK)

## tfm-qemu-test: build TF-M for an521 and boot BL2, the secure and the non-secure image on QEMU mps2-an521 to the non-secure application's first line
tfm-qemu-test:
	@$(MAKE) --no-print-directory tfm-image TFM_BOARD=an521
	@test/integration/cortex-m/qemu_tfm_test.sh $(TFM_WORK)/out/an521 '$(TFM_QEMU_NS_MARKER)'

.PHONY: tfm-image tfm-qemu-test
