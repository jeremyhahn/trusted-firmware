#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Tag <source> as <target>, where <target> is a content-addressed devcontainer image name (mk/docker.mk).
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
label() {
  local v
  v=$("$docker" image inspect --format '{{index .Config.Labels "tfw.image.inputs"}}' "$1" 2>/dev/null) || v=
  [ "$v" = "<no value>" ] || echo "$v"
}
want_label=$(label "$src")
have_label=$(label "$dst")
if [ -n "$want_label" ] && [ "$want_label" = "$have_label" ]; then
  echo "==> $dst: another leg tagged an image of the same inputs ($want_label) first; kept"
  exit 0
fi
echo "$err" >&2
echo "docker-tag: $dst resolves to '${have:-nothing}' labelled '${have_label:-nothing}', not $src's '${want:-nothing}' labelled '${want_label:-nothing}'" >&2
exit 1
