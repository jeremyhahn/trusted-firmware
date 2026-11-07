#!/usr/bin/env bash
# Run a command in the toolchain image against a COPY of the checkout.
#
#   usage: scripts/in-toolchain-image.sh <image> <command...>
#   e.g.   scripts/in-toolchain-image.sh go-enclave-toolchain:abc make build
#
# in-toolchain-template: v4 (ci-template v1.29.5). Ships beside ci.yml; copy it to
# scripts/ with the workflow. From go-quicraft's scripts/in-toolchain-image.sh
# (a141c3c), with the container prefix and uid taken from the environment.
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
# are GOPROXY reopened, a writable module cache mounted (below; /tmp/gomodcache
# with CI_CACHE=off), and the forge credentials passed through, by NAME from
# this environment (the four variables dependency-resolution.md §6 has the
# runner inject), so no token ever appears on a command line.
#
# v2 (ci-template v1.22, Company Agent m616 item 2): the caches live in named
# volumes on the daemon's host (ci-standard.md §6), never in the container's
# writable layer or its 3 GB /tmp: GOCACHE at /cache/gobuild and
# GOLANGCI_LINT_CACHE at /cache/golangci for every stage, and, for a bridge
# stage, GOMODCACHE at /cache/gomod (a no-network stage keeps the image's own
# read-only module cache, so it still proves the image works offline). The
# volumes are <CI_STACK_PREFIX>cache-<class>-{gobuild,golangci,gomod}, one set
# per repo, trust class and daemon host, kept across jobs. CI_CACHE_CLASS
# (default pr) is `main` only for a push to main, so a pull request never
# writes a cache that main's build reads. A new volume's root is given to
# IN_IMAGE_UID by a helper container holding CHOWN and nothing else.
# scripts/trim-ci-caches.sh bounds their size. CI_CACHE=off mounts none (v1).
#
# v3 (ci-template v1.22.2, Company Agent m641 item 2 and m646 item 3): the
# stage runs in this sibling container, so the sibling holds the job slot's
# CPU bound. The job container's cgroup cpu.max gives it (rounded up; nproc
# when the job has no quota; IN_IMAGE_CPUS overrides), and the sibling gets
# --cpus N with GOMAXPROCS=N and GOFLAGS=-p=N. golangci-lint's default
# concurrency follows the container CPU quota, so lint takes N with no flag.
# A 4-CPU slot on the Dells gives 4; never a hardcoded number.
#
# The container is named <CI_STACK_PREFIX>tc-<pid>-<random>, under the prefix
# scripts/reap-stale-ci-stacks.sh reaps (default <checkout dir>-ci-, which in CI
# is <repo>-ci-), and is removed on every exit. IN_IMAGE_UID is the non-root
# uid the command runs as (default 10001; devcontainer.md §5: any non-root uid).
#
# CUSTOMIZE (trusted-firmware, #253): IN_IMAGE_CACHE=<runner dir>:<container
# dir> carries a cache the job restores with actions/cache (the OP-TEE stack,
# several GB, keyed by its pin) through the same copy-in: the runner dir, when
# it exists, is streamed into the container dir (which the image creates and
# the user owns) before the start. IN_IMAGE_CACHE_SAVE=1 streams the container
# dir back over the runner dir after the command, whatever its status, for
# actions/cache to save; the job sets it only when the restore missed.
# v4 (ci-template v1.29.4 and v1.29.5, Company Agent rulings 261(c) and
# 262(a)): the stage takes the job slot's memory bound
# (--memory and an equal --memory-swap from the job container's cgroup
# memory.max, 8g with no limit, IN_IMAGE_MEMORY overrides), and GOTMPDIR
# joins TMPDIR on /var/tmp in the container's layer (CUSTOMIZE below). A scratch directory that is missing or not writable fails
# the stage by name. A bridge stage's GOPROXY puts DEVCACHE_GOPROXY (Athens)
# first when the runner sets it: <DEVCACHE_GOPROXY>|https://proxy.golang.org|direct,
# so any error falls through to the public proxy; GOPRIVATE travels with it.
#
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

uid=${IN_IMAGE_UID:-10001}
case $uid in
  "" | 0 | *[!0-9]*) echo "IN_IMAGE_UID must be a non-root numeric uid: $uid" >&2; exit 2 ;;
esac
prefix=${CI_STACK_PREFIX:-$(basename "$PWD")-ci-}
name="${prefix}tc-$$-${RANDOM}"
cache=${CI_CACHE:-on}
case $cache in
  on | off) ;;
  *) echo "CI_CACHE must be on or off: $cache" >&2; exit 2 ;;
esac
class=${CI_CACHE_CLASS:-pr}
case $class in
  pr | main) ;;
  *) echo "CI_CACHE_CLASS must be pr or main: $class" >&2; exit 2 ;;
esac
cache_host='' cache_ctr='' # CUSTOMIZE: IN_IMAGE_CACHE, above
if [ -n "${IN_IMAGE_CACHE:-}" ]; then
  cache_host=${IN_IMAGE_CACHE%:*}
  cache_ctr=${IN_IMAGE_CACHE##*:}
  case $cache_host:$cache_ctr in
    /*:/*) ;;
    *) echo "IN_IMAGE_CACHE must be <absolute runner dir>:<absolute container dir>: $IN_IMAGE_CACHE" >&2; exit 2 ;;
  esac
fi
cache_save=${IN_IMAGE_CACHE_SAVE:-0}
case $cache_save in
  0 | 1) ;;
  *) echo "IN_IMAGE_CACHE_SAVE must be 0 or 1: $cache_save" >&2; exit 2 ;;
esac
cpus=${IN_IMAGE_CPUS:-}
if [ -z "$cpus" ]; then
  quota=max period=100000
  if [ -r /sys/fs/cgroup/cpu.max ]; then read -r quota period < /sys/fs/cgroup/cpu.max; fi
  case $quota in
    max | "" | *[!0-9]*) cpus=$(nproc) ;;
    *) cpus=$(((quota + period - 1) / period)) ;;
  esac
fi
case $cpus in
  "" | 0 | *[!0-9]*) echo "IN_IMAGE_CPUS must be a positive integer: $cpus" >&2; exit 2 ;;
esac
memory=${IN_IMAGE_MEMORY:-}
if [ -z "$memory" ]; then
  limit=max
  if [ -r /sys/fs/cgroup/memory.max ]; then read -r limit < /sys/fs/cgroup/memory.max; fi
  case $limit in
    max | "" | *[!0-9]*) memory=8g ;;
    *) memory=$limit ;;
  esac
fi
if ! [[ $memory =~ ^[1-9][0-9]*[mg]?$ ]]; then
  echo "IN_IMAGE_MEMORY must be a size in bytes, m or g, e.g. 16g: $memory" >&2
  exit 2
fi
args=(
  --name "$name"
  --cpus "$cpus"
  --env GOMAXPROCS="$cpus"
  --env GOFLAGS="-p=$cpus"
  --init
  --cap-drop ALL
  --security-opt no-new-privileges:true
  --pids-limit 4096
  --memory "$memory"
  --memory-swap "$memory"
  --ulimit memlock=67108864:67108864
  --ulimit core=1048576:1048576
  --ulimit nofile=65536:65536
  --tmpfs "/tmp:rw,exec,nosuid,nodev,size=3g,mode=1777"
  --network "$network"
  --user "$uid:$uid"
  --workdir /src
  --env HOME=/tmp
  --env CI="${CI:-}"
  # CUSTOMIZE (trusted-firmware): trivy's databases (the Java one is near
  # 1 GB) and every tool's temporary files go under /var/tmp (mode 1777) in
  # the container's own writable layer: on disk, not in the /tmp tmpfs, whose
  # RAM-backed 3 GB go-xkms's dependency tree overfilled in xkms-security
  # (CI runs 53647, 53668). The layer goes with the container, so cleanup
  # removes them. The Go caches are in v2's volumes below.
  --env TRIVY_CACHE_DIR=/var/tmp/trivy
  --env TMPDIR=/var/tmp
  --env GOTMPDIR=/var/tmp
)
modcache=/var/tmp/gomodcache # CUSTOMIZE: /var/tmp, above
if [ "$cache" = on ]; then
  vols="${prefix}cache-${class}"
  docker run --rm --network none --cap-drop ALL --cap-add CHOWN \
    --security-opt no-new-privileges:true --user 0:0 \
    -v "$vols-gobuild:/c/gobuild" -v "$vols-golangci:/c/golangci" -v "$vols-gomod:/c/gomod" \
    "$image" sh -c "for d in /c/*; do [ \"\$(stat -c %u:%g \"\$d\")\" = $uid:$uid ] || chown $uid:$uid \"\$d\"; done" >/dev/null
  args+=(
    -v "$vols-gobuild:/cache/gobuild" --env GOCACHE=/cache/gobuild
    -v "$vols-golangci:/cache/golangci" --env GOLANGCI_LINT_CACHE=/cache/golangci
  )
  if [ "$network" = bridge ]; then
    args+=(-v "$vols-gomod:/cache/gomod")
    modcache=/cache/gomod
  fi
else
  # CUSTOMIZE (trusted-firmware): with CI_CACHE=off, the Go build and
  # golangci-lint caches go under /var/tmp, as above.
  args+=(--env GOCACHE=/var/tmp/gocache --env GOLANGCI_LINT_CACHE=/var/tmp/golangci-lint)
fi
if [ "$network" = bridge ]; then
  # The baked module cache is read-only to the run's user and holds only the
  # root module's build list; the nested modules deps-check walks fetch more.
  # DS-7352: on the GitHub pool the job env carries GOPROXY (the LAN module
  # proxy, no `direct`) with GOPRIVATE, GONOPROXY=none and GONOSUMDB; they
  # travel as they are. Elsewhere DEVCACHE_GOPROXY (Athens) goes first.
  gp=${GOPROXY:-"${DEVCACHE_GOPROXY:+$DEVCACHE_GOPROXY|}https://proxy.golang.org|direct"}
  args+=(--env "GOPROXY=$gp" --env "GOMODCACHE=$modcache")
  for v in GOPRIVATE GONOPROXY GONOSUMDB GIT_CONFIG_COUNT GIT_CONFIG_KEY_0 GIT_CONFIG_VALUE_0; do
    if [ -n "${!v:-}" ]; then args+=(--env "$v"); fi
  done
fi

stage= # CUSTOMIZE: the worktree stage below
cleanup() {
  docker rm -f -v "$name" >/dev/null 2>&1 || true
  if [ -n "$stage" ]; then rm -rf "$stage"; fi
}
trap cleanup EXIT

# The scratch directory is checked in the container first (v4), so a missing
# or unwritable one fails the stage here by name, not later as ENOSPC or EACCES.
# shellcheck disable=SC2016 # expands in the container, not here
guard='[ -d "$TMPDIR" ] && [ -w "$TMPDIR" ] || { echo "in-toolchain-image: scratch directory $TMPDIR is missing or not writable" >&2; exit 2; }'
docker create "${args[@]}" "$image" sh -c "$guard; $*" >/dev/null
if [ -f .git ]; then
  # CUSTOMIZE (trusted-firmware): a git worktree, where this repo's work
  # happens: `.git` is a file pointing at a git dir outside the checkout,
  # which the container does not have, and build, test and security run git.
  # Stream in a standalone .git instead: the common git dir (objects, refs,
  # config) without the other worktrees, this worktree's HEAD and index, the
  # objects of any alternates folded in, and core.bare off in case the common
  # dir is a bare repository.
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
if [ -n "$cache_host" ] && [ -d "$cache_host" ]; then # CUSTOMIZE: IN_IMAGE_CACHE
  tar -C "$cache_host" --owner="$uid" --group="$uid" --numeric-owner -cf - . |
    docker cp -a - "$name:$cache_ctr"
fi
rc=0
docker start -a "$name" || rc=$?
if [ -n "$cache_host" ] && [ "$cache_save" = 1 ]; then # CUSTOMIZE: IN_IMAGE_CACHE
  rm -rf "$cache_host"
  mkdir -p "$cache_host"
  docker cp "$name:$cache_ctr" - | tar -C "$cache_host" --strip-components=1 -xf -
fi
(exit "$rc") # the command's status, as `docker start -a` returned it
