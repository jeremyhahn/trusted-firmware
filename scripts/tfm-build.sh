#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Build TF-M v2.3.1, the TrustZone-M secure world (#608 slice 2, ruling 1024
# (a)), for one board: BL2 (MCUboot), the secure image at Profile Medium with
# the Initial Attestation partition off (ruling 935: no PSA token), and
# tf-m-extras' example non-secure application against the secure build's
# api_ns, all three signed by TF-M's default image signing.
#
# No TF-M source enters this repository: TF-M and tf-m-extras are fetched at
# the pins in scripts/tfm-pins.sh (the ones scripts/tfm-measure.sh measured)
# into the work directory, with CMake, Ninja and TF-M's Python tools from PyPI
# in a venv there, and built there. What a board needs goes to
# $WORK/out/<board>/:
#
#   bl2.axf bl2.bin           BL2
#   tfm_s.axf tfm_ns.axf      the unsigned images, for their sizes
#   tfm_s_signed.bin          the secure image, signed
#   tfm_ns_signed.bin         the non-secure image, signed
#   tfm_s_ns_signed.bin       both, at their primary slots from the first
#   sizes.txt                 arm-none-eabi-size of the three ELF files
#   load.txt                  an521 only: "<file> <address>" lines for QEMU
#                             (test/integration/cortex-m/qemu_tfm_test.sh)
#
# Needs: git, python3 with venv, arm-none-eabi-gcc (the toolchain image's,
# docker/packages.txt), and network access to git.trustedfirmware.org and
# PyPI. Logs go to $WORK/logs.
#
# Usage: scripts/tfm-build.sh an521|stm32h573i-dk [work-dir]
#        (default work-dir $HOME/.cache/trusted-firmware/tfm)
#        A relative work-dir is taken from the invocation directory.
set -euo pipefail

# shellcheck source=scripts/tfm-pins.sh
. "$(dirname "$0")/tfm-pins.sh"

# The checkout root, absolute: TF-M's CMake takes the partition paths below.
REPO=$(cd "$(dirname "$0")/.." && pwd -P)
BOARD=${1:-}
WORK=${2:-$HOME/.cache/trusted-firmware/tfm}

die() { echo "tfm-build: $*" >&2; exit 1; }
step() { echo "tfm-build: $*" >&2; }

# Our board name -> TF-M's platform. an521 is TF-M's own MPS2 M33 port, run on
# QEMU's mps2-an521; the NUCLEO-H563ZI comes as a port derived from
# stm32h573i_dk in slice 4.
case $BOARD in
an521) PLATFORM=arm/mps2/an521 ;;
stm32h573i-dk) PLATFORM=stm/stm32h573i_dk ;;
*) die "usage: tfm-build.sh an521|stm32h573i-dk [work-dir]" ;;
esac

# CMake resolves a relative TFM_TOOLCHAIN_FILE or CONFIG_SPE_PATH against its
# own source/build directories, so every path below starts absolute.
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd -P)
LOGS=$WORK/logs
BUILD_S=$WORK/build-$BOARD-s
BUILD_NS=$WORK/build-$BOARD-ns
OUT=$WORK/out/$BOARD
mkdir -p "$LOGS"

command -v arm-none-eabi-gcc >/dev/null || die "arm-none-eabi-gcc not found"
command -v arm-none-eabi-size >/dev/null || die "arm-none-eabi-size not found"

# fetch DIR URL REF COMMIT: a shallow clone of REF that must be COMMIT.
fetch() {
	local dir=$1 url=$2 ref=$3 want=$4 got
	if [ ! -d "$dir/.git" ]; then
		if [ "$ref" = "$want" ]; then
			git init -q "$dir"
			git -C "$dir" fetch -q --depth 1 "$url" "$want"
			git -C "$dir" checkout -q FETCH_HEAD
		else
			git clone -q --depth 1 --branch "$ref" "$url" "$dir" 2>/dev/null
		fi
	fi
	got=$(git -C "$dir" rev-parse HEAD)
	[ "$got" = "$want" ] || die "$url $ref is $got, pinned $want"
}

step "fetching TF-M $TFM_TAG and tf-m-extras"
fetch "$WORK/tf-m" "$TFM_URL" "$TFM_TAG" "$TFM_COMMIT"
fetch "$WORK/tf-m-extras" "$EXTRAS_URL" "$EXTRAS_COMMIT" "$EXTRAS_COMMIT"

step "installing CMake $CMAKE_PIN and TF-M's Python tools"
if [ ! -x "$WORK/venv/bin/python" ]; then
	python3 -m venv "$WORK/venv"
fi
"$WORK/venv/bin/pip" install -q "cmake==$CMAKE_PIN" "ninja==$NINJA_PIN" \
	intelhex "$WORK/tf-m" >"$LOGS/pip.log" 2>&1 ||
	die "pip install failed; see $LOGS/pip.log"
# The image signing runs TF-M's Python tools: CMake's FindPython3 takes the
# venv's interpreter, which has them, when VIRTUAL_ENV names it.
export VIRTUAL_ENV=$WORK/venv
export PATH="$WORK/venv/bin:$PATH"

# The DICE partition (src/tz/tfm, #608 slice 3) and tf-m-extras' Measured
# Boot partition, whose slot 3 (the NS image's measurement, which BL2 shares
# with MCUBOOT_DATA_SHARING) the DICE partition reads, both join the build
# through TF-M's extra Secure Partition path (config/extra_build_config.cmake):
# a ;-list of partition directories and one of manifest lists.
MB_DIR=$WORK/tf-m-extras/partitions/measured_boot
step "building the secure side for $BOARD ($PLATFORM)"
cmake -S "$WORK/tf-m" -B "$BUILD_S" -G Ninja \
	-DTFM_PLATFORM="$PLATFORM" \
	-DTFM_PROFILE="$PROFILE" \
	-DTFM_TOOLCHAIN_FILE="$WORK/tf-m/toolchain_GNUARM.cmake" \
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
	-DBL2=ON \
	-DTFM_PARTITION_INITIAL_ATTESTATION=OFF \
	-DTFM_EXTRA_MANIFEST_LIST_FILES="$REPO/src/tz/tfm/tfw_dice_manifest_list.yaml;$MB_DIR/measured_boot_manifest_list.yaml" \
	-DTFM_EXTRA_PARTITION_PATHS="$REPO/src/tz/tfm;$MB_DIR" \
	-DTFM_PARTITION_TFW_DICE=ON \
	-DTFM_PARTITION_MEASURED_BOOT=ON \
	-DCONFIG_TFM_BOOT_STORE_MEASUREMENTS=ON \
	-DMCUBOOT_DATA_SHARING=ON \
	>"$LOGS/configure-$BOARD-s.log" 2>&1 ||
	die "configure $BOARD secure failed; see $LOGS/configure-$BOARD-s.log"
cmake --build "$BUILD_S" -- install >"$LOGS/build-$BOARD-s.log" 2>&1 ||
	die "build $BOARD secure failed; see $LOGS/build-$BOARD-s.log"

# The example application pulls in api_ns's own CMakeLists, whose default
# image signing (BL2 on) signs both images in the non-secure build's `all`.
step "building the non-secure application for $BOARD"
cmake -S "$WORK/tf-m-extras/examples/tf-m-example-ns-app" -B "$BUILD_NS" \
	-G Ninja \
	-DCONFIG_SPE_PATH="$BUILD_S/api_ns" \
	-DTFM_TOOLCHAIN_FILE="$BUILD_S/api_ns/cmake/toolchain_ns_GNUARM.cmake" \
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE" >"$LOGS/configure-$BOARD-ns.log" 2>&1 ||
	die "configure $BOARD ns failed; see $LOGS/configure-$BOARD-ns.log"
cmake --build "$BUILD_NS" >"$LOGS/build-$BOARD-ns.log" 2>&1 ||
	die "build $BOARD ns failed; see $LOGS/build-$BOARD-ns.log"

rm -rf "$OUT"
mkdir -p "$OUT"
# take FILE...: copy each from the first build directory that has it.
take() {
	local f d
	for f in "$@"; do
		for d in "$BUILD_S/bin" "$BUILD_S/api_ns/bin" "$BUILD_NS/bin"; do
			if [ -f "$d/$f" ]; then
				cp "$d/$f" "$OUT/$f"
				continue 2
			fi
		done
		die "$f was not built (looked in $BUILD_S/bin, $BUILD_S/api_ns/bin, $BUILD_NS/bin)"
	done
}
take bl2.axf bl2.bin tfm_s.axf tfm_ns.axf \
	tfm_s_signed.bin tfm_ns_signed.bin tfm_s_ns_signed.bin

(cd "$OUT" && arm-none-eabi-size bl2.axf tfm_s.axf tfm_ns.axf) >"$OUT/sizes.txt"

# QEMU loads BL2 as its kernel and the two signed images at the start of the
# secure primary slot: an521's flash_layout.h puts BL2 at FLASH_BASE_ADDRESS
# 0x10000000, FLASH_AREA_BL2_SIZE 0x80000 long, so slot 0 is at 0x10080000,
# and tfm_s_ns_signed.bin holds both images at their slot offsets from there
# (TF-M's own an521 QEMU recipe loads it at that address).
if [ "$BOARD" = an521 ]; then
	echo "tfm_s_ns_signed.bin 0x10080000" >"$OUT/load.txt"
fi

echo "TF-M $TFM_TAG ($TFM_COMMIT), tf-m-extras $EXTRAS_COMMIT"
echo "platform $PLATFORM, $PROFILE, BL2 on, Initial Attestation off, DICE + Measured Boot on, CMAKE_BUILD_TYPE=$BUILD_TYPE"
cat "$OUT/sizes.txt"
echo "tfm-build: $BOARD images in $OUT"
