#!/usr/bin/env bash
# Tests for scripts/in-toolchain-image.sh's CPU bound: the stage's --cpus, GOMAXPROCS and GOFLAGS=-p all take the computed bound, capped at the daemon's NCPU when `docker info` reports a positive integer below it; empty, 0, malformed or failed output leaves the bound as it was. It also covers the IN_IMAGE_UID guard: 0 and a zero-padded 00 are refused.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
pass=0
fail=0
ok() { pass=$((pass + 1)); printf 'ok   %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; }

D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
mkdir -p "$D/bin" "$D/src" "$D/state"
echo x > "$D/src/file"

cat > "$D/bin/docker" <<'STUB'
#!/usr/bin/env bash
set -eu
printf '%s\n' "$*" >> "$STUB_STATE/calls"
case $1 in
  info) printf '%s\n' "${STUB_NCPU:-}"; exit "${STUB_NCPU_RC:-0}" ;;
  create | start | rm) ;;
  cp) cat > /dev/null ;;
  *) exit 1 ;;
esac
STUB
printf '#!/bin/sh\nexit 0\n' > "$D/bin/tar"
chmod +x "$D/bin/docker" "$D/bin/tar"

# run [env...]: the script on a stub stage, its status in $rc.
run() {
  rm -f "$D/state/calls"
  rc=0
  (cd "$D/src" && env -u IN_IMAGE_CACHE PATH="$D/bin:$PATH" STUB_STATE="$D/state" CI_CACHE=off \
    IN_IMAGE_MEMORY=8g CI_STACK_PREFIX=p- "$@" \
    "$here/in-toolchain-image.sh" img make test) > "$D/out" 2>&1 || rc=$?
}
called() { grep -q -- "$1" "$D/state/calls"; }

# cpus_are <case> <want> <env...>: the stage's three CPU values are all <want>.
cpus_are() {
  local what=$1 want=$2
  shift 2
  run "$@"
  if [ "$rc" = 0 ] && called '^info --format {{.NCPU}}$' &&
    called "^create .*--cpus $want " && called "^create .*--env GOMAXPROCS=$want " &&
    called "^create .*--env GOFLAGS=-p=$want "; then
    ok "CPU bound $want: $what"
  else
    bad "CPU bound $want: $what (rc=$rc)"
    cat "$D/out" "$D/state/calls" 2>/dev/null || true
  fi
}

cpus_are "host 128, daemon 80" 80 IN_IMAGE_CPUS=128 STUB_NCPU=80
cpus_are "requested 4, daemon 80" 4 IN_IMAGE_CPUS=4 STUB_NCPU=80
cpus_are "daemon equal to the bound" 16 IN_IMAGE_CPUS=16 STUB_NCPU=16
cpus_are "daemon output empty" 128 IN_IMAGE_CPUS=128 STUB_NCPU=
cpus_are "daemon output 0" 128 IN_IMAGE_CPUS=128 STUB_NCPU=0
cpus_are "daemon output malformed" 128 IN_IMAGE_CPUS=128 STUB_NCPU=abc
cpus_are "docker info fails with no output" 128 IN_IMAGE_CPUS=128 STUB_NCPU= STUB_NCPU_RC=1
# An unreachable daemon: the CLI still renders the template (NCPU 0) and exits 1.
cpus_are "docker info fails printing 0" 128 IN_IMAGE_CPUS=128 STUB_NCPU=0 STUB_NCPU_RC=1
# A failed lookup that still prints a positive count must not cap: only a zero exit counts.
cpus_are "docker info fails printing 80" 128 IN_IMAGE_CPUS=128 STUB_NCPU=80 STUB_NCPU_RC=1

# A bad IN_IMAGE_CPUS is refused before the daemon is asked.
run IN_IMAGE_CPUS=abc STUB_NCPU=80
if [ "$rc" = 2 ] && grep -q 'IN_IMAGE_CPUS must be a positive integer: abc' "$D/out" && [ ! -s "$D/state/calls" ]; then
  ok "a bad IN_IMAGE_CPUS is refused with no docker call"
else bad "a bad IN_IMAGE_CPUS is refused with no docker call (rc=$rc)"; cat "$D/out"; fi

# A zero-padded uid is still root: 00 must be refused like 0.
run IN_IMAGE_UID=00 IN_IMAGE_CPUS=4 STUB_NCPU=80
if [ "$rc" = 2 ] && grep -q 'IN_IMAGE_UID must be a non-root numeric uid: 00' "$D/out" && [ ! -s "$D/state/calls" ]; then
  ok "a zero-padded root IN_IMAGE_UID is refused with no docker call"
else bad "a zero-padded root IN_IMAGE_UID is refused with no docker call (rc=$rc)"; cat "$D/out"; fi

run IN_IMAGE_UID=0 IN_IMAGE_CPUS=4 STUB_NCPU=80
if [ "$rc" = 2 ] && grep -q 'IN_IMAGE_UID must be a non-root numeric uid: 0' "$D/out" && [ ! -s "$D/state/calls" ]; then
  ok "IN_IMAGE_UID=0 is refused with no docker call"
else bad "IN_IMAGE_UID=0 is refused with no docker call (rc=$rc)"; cat "$D/out"; fi

run IN_IMAGE_CPUS=00 STUB_NCPU=80
if [ "$rc" = 2 ] && grep -q 'IN_IMAGE_CPUS must be a positive integer: 00' "$D/out" && [ ! -s "$D/state/calls" ]; then
  ok "a zero-padded IN_IMAGE_CPUS is refused with no docker call"
else bad "a zero-padded IN_IMAGE_CPUS is refused with no docker call (rc=$rc)"; cat "$D/out"; fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
