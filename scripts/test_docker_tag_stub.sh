#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# A stand-in for docker, for scripts/test_docker_tag.py: it plays the daemon of run 70520, where another leg tagged the same content-addressed name in the same second, so every `tag` fails.
set -euo pipefail

[ -z "${STUB_LOG:-}" ] || echo "$*" >>"$STUB_LOG"

# removed REF: true when an earlier call in STUB_LOG removed REF.
removed() {
  [ -n "${STUB_LOG:-}" ] && [ -f "$STUB_LOG" ] || return 1
  local line
  while IFS= read -r line; do
    case "$line" in
    "image rm $1" | "image rm "*" $1" | "rmi $1" | "rmi "*" $1") return 0 ;;
    esac
  done <"$STUB_LOG"
  return 1
}

# run_ref ARGS...: the image a `docker run` names, its first non-option.
run_ref() {
  shift
  while [ $# -gt 0 ]; do
    case "$1" in
    -v | -e | -u | -w | --user | --env | --volume | --workdir) shift 2 ;;
    -*) shift ;;
    *) echo "$1"; return ;;
    esac
  done
}

case "${1:-}" in
build | rmi) exit 0 ;;
run)
  ref=$(run_ref "$@")
  case "$ref" in
  *-"${IMAGE_BUILD_ID:-build}") ;;
  *)
    echo "test_docker_tag_stub: run on $ref, not the build-id name" >&2
    exit 1
    ;;
  esac
  if removed "$ref"; then
    echo "Unable to find image '$ref' locally" >&2
    exit 125
  fi
  exit 0
  ;;
tag)
  echo "${STUB_TAG_ERROR:-Error response from daemon: failed to create an image $3 with target ${STUB_BUILT_ID:-} after deleting the existing one: AlreadyExists: image \"$3\" already exists}" >&2
  exit 1
  ;;
image)
  case "${2:-}" in
  rm) exit 0 ;;
  inspect)
    ref=${!#}
    case "$ref" in
    *-"${IMAGE_BUILD_ID:-build}") id=${STUB_BUILT_ID:-} label=${STUB_BUILT_LABEL:-} ;;
    *) id=${STUB_TARGET_ID:-} label=${STUB_TARGET_LABEL:-} ;;
    esac
    if [ -z "$id" ]; then
      echo "Error response from daemon: No such image: $ref" >&2
      exit 1
    fi
    case "$*" in
    *tfw.image.inputs*) echo "$label" ;;
    *) echo "$id" ;;
    esac
    exit 0
    ;;
  esac
  ;;
esac
echo "test_docker_tag_stub: unexpected call: $*" >&2
exit 1
