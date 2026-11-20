#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Measure the flash and RAM footprint of TF-M v2.3.1, Profile Medium with
# BL2 (MCUboot), on the STM32H573I-DK (#483; ADR #15 follow-up F1,
# docs/cortex-m/adr-trust-model-vs-tf-m.md).
#
# Fact-finding only: nothing here is linked into our firmware, and no TF-M
# source enters this repository. TF-M and tf-m-extras are fetched at their
# pinned commits into a work directory outside the tree, and built there:
#
#   base   TF-M as the platform ships it at Profile Medium: BL2 and the
#          secure image (SPM, Crypto, ITS, PS, Initial Attestation, FWU,
#          Platform).
#   stub   the same, plus tf-m-extras' example partition as the stub
#          Application RoT partition: one IPC service, no crypto of its own.
#          The secure image's growth over base is the stub's cost.
#   ns     tf-m-extras' example non-secure application, built against the
#          stub build's exported api_ns: the non-secure stub.
#
# Needs: git, python3 with venv, arm-none-eabi-gcc (the toolchain image's,
# docker/packages.txt), and network access to git.trustedfirmware.org and
# PyPI. CMake and TF-M's Python tools come from PyPI into a venv in the work
# directory. Takes about 15 minutes; logs go to $WORK/logs.
#
# Usage: scripts/tfm-measure.sh [work-dir]   (default /tmp/tfm-measure)
set -euo pipefail

# The pins, shared with scripts/tfm-build.sh.
# shellcheck source=scripts/tfm-pins.sh
. "$(dirname "$0")/tfm-pins.sh"
PLATFORM=stm/stm32h573i_dk

WORK=${1:-/tmp/tfm-measure}
LOGS=$WORK/logs
mkdir -p "$WORK" "$LOGS"

die() { echo "tfm-measure: $*" >&2; exit 1; }
step() { echo "tfm-measure: $*" >&2; }

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
export PATH="$WORK/venv/bin:$PATH"

# secure NAME [extra cmake args]: configure and build one secure tree.
secure() {
	local name=$1
	shift
	step "building the secure side ($name)"
	cmake -S "$WORK/tf-m" -B "$WORK/build-$name" -G Ninja \
		-DTFM_PLATFORM="$PLATFORM" \
		-DTFM_PROFILE="$PROFILE" \
		-DTFM_TOOLCHAIN_FILE="$WORK/tf-m/toolchain_GNUARM.cmake" \
		-DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
		"$@" >"$LOGS/configure-$name.log" 2>&1 ||
		die "configure $name failed; see $LOGS/configure-$name.log"
	cmake --build "$WORK/build-$name" -- install \
		>"$LOGS/build-$name.log" 2>&1 ||
		die "build $name failed; see $LOGS/build-$name.log"
}

example=$WORK/tf-m-extras/examples/example_partition
secure base
secure stub \
	-DTFM_PARTITION_EXAMPLE=ON \
	-DTFM_EXTRA_MANIFEST_LIST_FILES="$example/tfm_example_manifest_list.yaml" \
	-DTFM_EXTRA_PARTITION_PATHS="$example"

step "building the non-secure stub"
cmake -S "$WORK/tf-m-extras/examples/tf-m-example-ns-app" -B "$WORK/build-ns" \
	-G Ninja \
	-DCONFIG_SPE_PATH="$WORK/build-stub/api_ns" \
	-DTFM_TOOLCHAIN_FILE="$WORK/build-stub/api_ns/cmake/toolchain_ns_GNUARM.cmake" \
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE" >"$LOGS/configure-ns.log" 2>&1 ||
	die "configure ns failed; see $LOGS/configure-ns.log"
cmake --build "$WORK/build-ns" >"$LOGS/build-ns.log" 2>&1 ||
	die "build ns failed; see $LOGS/build-ns.log"

# berkeley ELF: prints "text data bss" from arm-none-eabi-size.
berkeley() {
	local text data bss rest
	read -r text data bss rest < <(arm-none-eabi-size -B "$1" | tail -n 1)
	echo "$text $data $bss"
}

kib() { printf '%d.%d' $(($1 / 1024)) $((($1 % 1024) * 10 / 1024)); }

# row LABEL TEXT DATA BSS: one table row; flash = text + data (the data
# section's load image), RAM = data + bss.
row() {
	local flash=$(($2 + $3)) ram=$(($3 + $4))
	printf '| %s | %d | %d | %d | %d (%s KiB) | %d (%s KiB) |\n' \
		"$1" "$2" "$3" "$4" "$flash" "$(kib "$flash")" "$ram" "$(kib "$ram")"
}

read -r bt bd bb < <(berkeley "$WORK/build-base/bin/bl2.axf")
read -r st sd sb < <(berkeley "$WORK/build-base/bin/tfm_s.axf")
read -r pt pd pb < <(berkeley "$WORK/build-stub/bin/tfm_s.axf")
read -r nt nd nb < <(berkeley "$WORK/build-ns/bin/tfm_ns.axf")

[ $((pt + pd)) -gt $((st + sd)) ] ||
	die "the stub build's secure image is no larger than base: the partition was not linked"

echo "TF-M $TFM_TAG ($TFM_COMMIT), tf-m-extras $EXTRAS_COMMIT"
echo "platform $PLATFORM, $PROFILE, BL2 on, CMAKE_BUILD_TYPE=$BUILD_TYPE"
echo "$(arm-none-eabi-gcc --version | head -n 1); cmake $CMAKE_PIN"
echo
echo "| Image | text | data | bss | flash, bytes | RAM, bytes |"
echo "|---|---|---|---|---|---|"
row "BL2 (MCUboot)" "$bt" "$bd" "$bb"
row "secure image, Profile Medium" "$st" "$sd" "$sb"
row "secure image with the stub partition" "$pt" "$pd" "$pb"
row "the stub partition (difference)" $((pt - st)) $((pd - sd)) $((pb - sb))
row "BL2 + secure image with the stub" $((bt + pt)) $((bd + pd)) $((bb + pb))
row "non-secure stub" "$nt" "$nd" "$nb"
