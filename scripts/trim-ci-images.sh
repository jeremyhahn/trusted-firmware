#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Bound the content-tagged devcontainer images on the runner's Docker host.
set -u

if [ "$#" -eq 0 ]; then
  echo "usage: $0 <image> [<image>...]" >&2
  exit 2
fi
keep=${CI_IMAGE_KEEP:-3}
case $keep in
  '' | *[!0-9]*) echo "CI_IMAGE_KEEP must be a number: $keep" >&2; exit 2 ;;
esac

hash_tag='^[0-9a-f]{16}(-[a-z]+)?$'
removed=0
for image in "$@"; do
  repo=${image%:*}
  n=0
  # `docker image ls` lists the newest first.
  for tag in $(docker image ls --format '{{.Tag}}' "$repo" 2>/dev/null); do
    [[ $tag =~ $hash_tag ]] || continue
    ref="$repo:$tag"
    n=$((n + 1))
    if [ "$n" -le "$keep" ] || [ "$ref" = "$image" ]; then
      continue
    fi
    if [ -n "$(docker ps -aq --filter "ancestor=$ref")" ]; then
      echo "keep  $ref (in use)"
      continue
    fi
    if [ "${REAP_DRY_RUN:-0}" = 1 ]; then
      echo "dry   docker image rm $ref"
    elif docker image rm "$ref" >/dev/null 2>&1; then
      echo "trim  $ref"
      removed=$((removed + 1))
    else
      echo "warn  $ref not removed"
    fi
  done
done
echo "trim: $removed image tag(s) removed, the newest $keep kept per repository"
exit 0
