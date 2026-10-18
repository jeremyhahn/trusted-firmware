#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Bound the content-tagged devcontainer images on the runner's Docker host.
#
#   usage: scripts/trim-ci-images.sh <image> [<image>...]
#   e.g.   scripts/trim-ci-images.sh "$TOOLCHAIN_IMAGE" "$ESP32_IMAGE"
#
# The toolchain and esp32 images are tagged by a hash of their inputs
# (mk/docker.mk) and shared by every run on the host, so no job removes the one
# it ran (Company Agent m644). Each new pin or go.sum would then leave one more
# multi-GiB image behind. For each <image>'s repository this keeps the newest
# CI_IMAGE_KEEP (default 3) hash tags and removes the older ones, never <image>
# itself and never one a container (running or stopped) was made from. A tag
# that is not a hash tag (the moving cache tag, a developer's own) is left.
# Removing a tag whose image carries another tag only untags it.
#
# A run of an older hash that loses its image here pulls it again, or builds
# it. A failed removal is reported and skipped: this never fails the job.
# REAP_DRY_RUN=1 prints what would be removed and removes nothing.
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
