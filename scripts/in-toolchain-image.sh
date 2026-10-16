#!/usr/bin/env bash
# Run a command in the toolchain image against a COPY of the checkout.
#
#   usage: scripts/in-toolchain-image.sh <image> <command...>
#   e.g.   scripts/in-toolchain-image.sh trusted-firmware-toolchain:abc make build
#
# How CI runs a stage in the devcontainer image (handbook ci-standard.md §2). A
# CI runner reaches its Docker daemon through a socket, so a bind mount would
# resolve on the daemon's host, where the checkout does not exist. This
# creates the container, streams the checkout into it (`.git` included: the
# releasability tests read it) owned by the image's non-root user, then starts
# it and returns the command's exit status. It works the same against a local
# daemon; on a workstation `make docker-run CMD=...` (bind mount) is the
# shorter path.
#
# The container keeps the integration service's posture where a copy-in allows
# it: every capability dropped, no-new-privileges, the same ulimits and pid
# bound, a non-root user, no network. The root filesystem is writable only
# because `docker cp` cannot write into a read-only one.
#
# IN_IMAGE_NETWORK=bridge gives the container a network, for the stages that
# must reach one: `make deps-check` asks the module proxy for the newest
# releases, and `make security` fetches the vulnerability databases. Only then
# are GOPROXY reopened, a writable module cache used under /tmp, and the forge
# credentials passed through, by NAME from
# this environment (the four variables dependency-resolution.md §6 has the
# runner inject), so no token ever appears on a command line.
#
# The container is named trusted-firmware-ci-tc-<pid>-<random>, under the prefix
# scripts/reap-stale-ci-stacks.sh reaps, and is removed on every exit.
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <image> <command...>" >&2
  exit 2
fi
image=$1
shift
network=${IN_IMAGE_NETWORK:-none}
case $network in
  none | bridge) ;;
  *) echo "IN_IMAGE_NETWORK must be none or bridge: $network" >&2; exit 2 ;;
esac

uid=10001
name="trusted-firmware-ci-tc-$$-${RANDOM}"
args=(
  --name "$name"
  --init
  --cap-drop ALL
  --security-opt no-new-privileges:true
  --pids-limit 4096
  --ulimit memlock=67108864:67108864
  --ulimit core=1048576:1048576
  --ulimit nofile=65536:65536
  --tmpfs "/tmp:rw,exec,nosuid,nodev,size=3g,mode=1777"
  --network "$network"
  --user "$uid:$uid"
  --workdir /src
  --env HOME=/tmp
  # The Go build cache, golangci-lint's cache and (below) the module cache go
  # under /var/tmp (mode 1777) in the container's own writable layer: on disk,
  # not in the /tmp tmpfs, whose RAM-backed 3 GB go-xkms's dependency tree
  # overfilled in xkms-security (CI run 53647). The layer goes with the
  # container, so cleanup removes them. trivy's databases (the Java one is near
  # 1 GB) and every tool's temporary files go there too (CI run 53668).
  --env GOCACHE=/var/tmp/gocache
  --env GOLANGCI_LINT_CACHE=/var/tmp/golangci-lint
  --env TRIVY_CACHE_DIR=/var/tmp/trivy
  --env TMPDIR=/var/tmp
  --env CI="${CI:-}"
)
if [ "$network" = bridge ]; then
  # The baked module cache is read-only to the run's user and holds only the
  # root module's build list; the nested modules deps-check walks fetch more.
  args+=(--env "GOPROXY=https://proxy.golang.org,direct" --env GOMODCACHE=/var/tmp/gomodcache)
  for v in GOPRIVATE GIT_CONFIG_COUNT GIT_CONFIG_KEY_0 GIT_CONFIG_VALUE_0; do
    if [ -n "${!v:-}" ]; then args+=(--env "$v"); fi
  done
fi

stage=
cleanup() {
  docker rm -f -v "$name" >/dev/null 2>&1 || true
  if [ -n "$stage" ]; then rm -rf "$stage"; fi
}
trap cleanup EXIT

docker create "${args[@]}" "$image" sh -c "$*" >/dev/null
if [ -f .git ]; then
  # A git worktree: `.git` is a file pointing at a git dir outside the checkout,
  # which the container does not have. Stream in a standalone .git instead: the
  # common git dir (objects, refs, config) without the other worktrees, this
  # worktree's HEAD and index, the objects of any alternates folded in, and
  # core.bare off in case the common dir is a bare repository.
  gitdir=$(git rev-parse --absolute-git-dir)
  common=$(git rev-parse --path-format=absolute --git-common-dir)
  stage=$(mktemp -d)
  cp -a "$common/." "$stage/.git/"
  rm -rf "$stage/.git/worktrees"
  cp -a "$gitdir/HEAD" "$stage/.git/HEAD"
  if [ -f "$gitdir/index" ]; then cp -a "$gitdir/index" "$stage/.git/index"; fi
  if [ -f "$stage/.git/objects/info/alternates" ]; then
    while IFS= read -r alt; do
      case $alt in '' | '#'*) continue ;; esac
      case $alt in /*) ;; *) alt="$common/objects/$alt" ;; esac
      cp -a -n "$alt/." "$stage/.git/objects/"
    done <"$stage/.git/objects/info/alternates"
    rm -f "$stage/.git/objects/info/alternates"
  fi
  git config --file "$stage/.git/config" core.bare false
  tar -C "$PWD" --owner="$uid" --group="$uid" --numeric-owner --exclude=./.git -cf - . |
    docker cp -a - "$name:/src"
  tar -C "$stage" --owner="$uid" --group="$uid" --numeric-owner -cf - .git |
    docker cp -a - "$name:/src"
else
  tar -C "$PWD" --owner="$uid" --group="$uid" --numeric-owner -cf - . |
    docker cp -a - "$name:/src"
fi
docker start -a "$name"
