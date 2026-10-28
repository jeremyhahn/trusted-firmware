#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Put a content-tagged devcontainer image on the Docker daemon, building it
# only when no copy of it exists (Company Agent m644).
#
#   usage: scripts/ensure-image.sh <make-target> <image> [<trusted-image>]
#   e.g.   scripts/ensure-image.sh docker-build-toolchain "$(make -s toolchain-image)"
#
# <image> is tagged by a hash of every input its stages read (mk/docker.mk,
# `make -s toolchain-hash`), so any copy under that tag is the image this
# commit would build. <trusted-image> is the same hash in a trust class this
# run reads but never writes: main's, for a pull request (ci-standard.md §6).
# The first of these that succeeds wins:
#
#   1. <image> is on the daemon;
#   2. <trusted-image> is on the daemon, and is tagged <image>;
#   3. with IMAGE_PULL=1, <image> and then <trusted-image> pull from the
#      registry (a trusted one is tagged <image>);
#   4. `make <make-target>` builds it, with the registry as its cache.
#
# With IMAGE_PUSH=1 a built image is pushed with its moving cache tag
# (IMAGE_CACHE_REF, the next build's cache-from). A failed push is reported
# and the job goes on: the image is on the daemon, and the next run builds
# again. Nothing here removes an image (trim-ci-images.sh bounds them).
#
# Legs on one daemon run this at once, so each tag goes through
# scripts/docker-tag.sh, which passes a lost race to the same image.
set -euo pipefail
here=$(dirname "$0")

target=${1:-}
image=${2:-}
trusted=${3:-}
if [ -z "$target" ] || [ -z "$image" ]; then
  echo "usage: $0 <make-target> <image> [<trusted-image>]" >&2
  exit 2
fi

have() { docker image inspect "$1" >/dev/null 2>&1; }
adopt() {
  [ "$1" = "$image" ] || "$here/docker-tag.sh" "$1" "$image"
  echo "==> $image: reused $2, not built"
}

if have "$image"; then
  echo "==> $image: on the daemon, not built"
  exit 0
fi
if [ -n "$trusted" ] && have "$trusted"; then
  adopt "$trusted" "$trusted from the daemon"
  exit 0
fi
if [ "${IMAGE_PULL:-0}" = 1 ]; then
  for ref in "$image" ${trusted:+"$trusted"}; do
    if docker pull -q "$ref" >/dev/null 2>&1; then
      adopt "$ref" "$ref from the registry"
      exit 0
    fi
  done
  echo "==> $image: not on the daemon or the registry, building"
else
  echo "==> $image: not on the daemon, building (IMAGE_PULL unset: no registry)"
fi

make --no-print-directory "$target"

if [ "${IMAGE_PUSH:-0}" = 1 ]; then
  for ref in "$image" ${IMAGE_CACHE_REF:+"$IMAGE_CACHE_REF"}; do
    [ "$ref" = "$image" ] || "$here/docker-tag.sh" "$image" "$ref"
    if docker push -q "$ref" >/dev/null 2>&1; then
      echo "==> pushed $ref"
    else
      echo "warn  push of $ref refused: the next cold run builds it again"
    fi
  done
fi
