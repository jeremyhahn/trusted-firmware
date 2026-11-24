#!/usr/bin/env bash
# Run a command in the toolchain image against a copy of the checkout.
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
# The daemon refuses --cpus above its own CPU count, and the job container can
# see more CPUs than the daemon that runs the stage (a pooled runner host), so
# the bound is capped at the daemon's NCPU when the daemon reports one.
if ! daemon_cpus=$(docker info --format '{{.NCPU}}' 2>/dev/null); then daemon_cpus=; fi
case $daemon_cpus in
  "" | 0 | *[!0-9]*) ;;
  *) if [ "$cpus" -gt "$daemon_cpus" ]; then cpus=$daemon_cpus; fi ;;
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
  args+=(--env "GOPROXY=${DEVCACHE_GOPROXY:+$DEVCACHE_GOPROXY|}https://proxy.golang.org" --env "GOMODCACHE=$modcache")
  # ci-template v1.36: the Trivy database repositories the runner sets (the
  # dev-cache ghcr mirror on the LAN first, the public ones after) travel
  # with the stage, so `make security` and `trivy image` fetch the vulnerability
  # and Java databases from the LAN instead of the internet.
  for v in GOPRIVATE GIT_CONFIG_COUNT GIT_CONFIG_KEY_0 GIT_CONFIG_VALUE_0 \
    TRIVY_DB_REPOSITORY TRIVY_JAVA_DB_REPOSITORY TRIVY_INSECURE; do
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
