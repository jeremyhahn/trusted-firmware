# shellcheck shell=bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The TF-M pins, sourced by scripts/tfm-measure.sh (#483) and
# scripts/tfm-build.sh (#608): one place, so the build is the release the
# footprint was measured on. Variables only; nothing runs.
# shellcheck disable=SC2034

TFM_URL=https://git.trustedfirmware.org/TF-M/trusted-firmware-m.git
TFM_TAG=TF-Mv2.3.1
TFM_COMMIT=0b40d7806136baadf489febf8e2f91bf87aca63d
EXTRAS_URL=https://git.trustedfirmware.org/TF-M/tf-m-extras.git
# The tf-m-extras commit TF-M v2.3.1 itself fetches
# (lib/ext/tf-m-extras in its build tree).
EXTRAS_COMMIT=2ac90aff6da35cde4a67f2d34076b1adf6ca9161
CMAKE_PIN=3.31.6
NINJA_PIN=1.11.1.4
PROFILE=profile_medium
BUILD_TYPE=MinSizeRel
