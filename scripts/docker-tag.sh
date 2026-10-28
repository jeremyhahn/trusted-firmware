#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Tag <source> as <target>, where <target> is a content-addressed devcontainer
# image name (mk/docker.mk).
#
#   usage: scripts/docker-tag.sh <source> <target>
#
# CI legs on one daemon that miss the cache build the same hash at once, each
# to its own <image>-$(IMAGE_BUILD_ID), and then all tag the shared name. The
# daemon lets one win; the others fail with AlreadyExists (run 70520). The tag
# is a hash of every input, so the winner's image is the one this leg built:
# a lost race is a success when <target> resolves to <source>'s id. Any other
# error, or a <target> that holds another image, still fails. DOCKER names the
# client, as in mk/docker.mk.
set -euo pipefail

docker=${DOCKER:-docker}
src=${1:-}
dst=${2:-}
if [ -z "$src" ] || [ -z "$dst" ]; then
  echo "usage: $0 <source> <target>" >&2
  exit 2
fi

if err=$("$docker" tag "$src" "$dst" 2>&1); then
  exit 0
fi
case "$err" in
*AlreadyExists*) ;;
*)
  echo "$err" >&2
  exit 1
  ;;
esac

id() { "$docker" image inspect --format '{{.Id}}' "$1" 2>/dev/null; }
want=$(id "$src") || want=
have=$(id "$dst") || have=
if [ -n "$want" ] && [ "$want" = "$have" ]; then
  echo "==> $dst: another leg tagged the same image ($want) first; kept"
  exit 0
fi
echo "$err" >&2
echo "docker-tag: $dst resolves to '${have:-nothing}', not $src's '${want:-nothing}'" >&2
exit 1
