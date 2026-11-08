#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Tests of the public-releasability gate (scripts/releasability.sh) and of the
# export-ignore list in .gitattributes. Run by `make releasability-test`, which
# `make ci` includes. There is no skip guard: a test that cannot run fails.
#
# This file carries the fleet values, because they are what the gate exists to
# find. Each such line has its own inline waiver, so the gate passes over it.
# Every other tracked file uses neutral fixtures.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/.." && pwd)
gate=$here/releasability.sh

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

failures=0
checks=0
n=0

fail() { echo "FAIL: $*"; failures=$((failures + 1)); }

# fresh: a new throwaway git work tree, named by $t.
fresh() {
  n=$((n + 1))
  t=$tmp/w$n
  mkdir -p "$t"
  git -C "$t" init -q
}

# The plants below are neutral stand-ins (example.internal, an RFC 5737
# address, made-up host names), not the fleet values, so run_gate gives the
# gate this fixture list, one rule per plant, instead of the fleet list it
# reads in production (scripts/releasability.patterns or the CI secret).
# The address rule names the one planted address, so the RFC 5737 addresses
# the clean cases use stay clean.
fixture=$tmp/fixture.patterns
{
  printf '%s\t%s\n' lab-address '\.example\.internal'
  printf '%s\t%s\n' lab-address '(^|[^0-9.])192\.0\.2\.22([^0-9]|$)'
  printf '%s\t%s\n' lab-host 'gpu-host'                      # lab-string-ok: test fixture
  printf '%s\t%s\n' lab-host '(^|[^A-Za-z0-9-])lab-node-[0-9]+'
  printf '%s\t%s\n' lab-host '(^|[^A-Za-z0-9-])ci-node-[0-9]+'
  printf '%s\t%s\n' lab-host '<host>'
  printf '%s\t%s\n' lab-host 'vstation-[0-9]+'               # lab-string-ok: test fixture
  printf '%s\t%s\n' owner-account '(^|[^A-Za-z0-9_./-])jeremyhahn($|[^A-Za-z0-9_/-])'   # lab-string-ok: test fixture
  printf '%s\t%s\n' owner-account '(^|[^A-Za-z0-9_./-])jhahn($|[^A-Za-z0-9_/-])'        # lab-string-ok: test fixture
} > "$fixture"

# run_gate: the gate on $t with the fixture list; output in $out, status in $rc.
run_gate() {
  git -C "$t" add -A
  rc=0
  out=$(RELEASABILITY_ROOT=$t RELEASABILITY_PATTERNS=$fixture "$gate" 2>&1) || rc=$?
}

# expect_hit CLASS CONTENT: a file holding CONTENT on its first line fails the
# gate with `doc.md:1: CLASS`.
expect_hit() {
  checks=$((checks + 1))
  fresh
  printf '%s\n' "$2" > "$t/doc.md"
  run_gate
  if [ "$rc" -ne 1 ]; then fail "$1: gate exited $rc, want 1, for: $2"; return; fi
  case "$out" in *"doc.md:1: $1"*) ;; *) fail "$1: no 'doc.md:1: $1' in output for: $2" ;; esac
}

# expect_clean CONTENT: the same file passes.
expect_clean() {
  checks=$((checks + 1))
  fresh
  printf '%s\n' "$1" > "$t/doc.md"
  run_gate
  if [ "$rc" -ne 0 ]; then fail "gate rejected clean content (exit $rc): $1 -> $out"; fi
}

# ------------------------------------------------------- a hit in each class

expect_hit lab-address 'forge at http://git.example.internal/o/r'   # lab-string-ok: plant
expect_hit lab-address 'endpoint 192.0.2.22:7020'                   # lab-string-ok: plant
expect_hit lab-host    'runs on gpu-host'                           # lab-string-ok: plant
expect_hit lab-host    'built on lab-node-1'                          # lab-string-ok: plant
expect_hit lab-host    'built on ci-node-2'                       # lab-string-ok: plant
expect_hit lab-host    'forge <host>'                              # lab-string-ok: plant
expect_hit lab-host    'ssh vstation-12'                           # lab-string-ok: plant
expect_hit owner-account 'ask jeremyhahn about it'                 # lab-string-ok: plant
expect_hit owner-account 'owner `jeremyhahn`.'                     # lab-string-ok: plant
expect_hit owner-account 'user jhahn'                              # lab-string-ok: plant

# A hit on the second line is reported on the second line.
checks=$((checks + 1)); fresh
printf 'clean\nhost gpu-host\n' > "$t/doc.md"                       # lab-string-ok: plant
run_gate
case "$out" in *"doc.md:2: lab-host"*) ;; *) fail "second-line hit not reported as doc.md:2: $out" ;; esac

# ---------------------------------------------------------- what must pass

expect_clean 'documentation addresses 192.0.2.10, 198.51.100.7 and 203.0.113.9'
expect_clean 'opaque RFC 1918 fixtures 192.168.1.20 and 10.0.0.5'
expect_clean 'a version 110.4.5.1 is not the bench VLAN'
expect_clean 'module github.com/jeremyhahn/trusted-firmware/sdk/go'
expect_clean 'see jeremyhahn/go-xkms and https://github.com/jeremyhahn/arduino-generic'
expect_clean 'build host, CI runner, bench host'
# Benchmark provenance and the other inline waivers: same line, or the line above.
expect_clean 'measured on gpu-host, 2026-09-14 <!-- lab-string-ok: benchmark provenance -->'   # lab-string-ok: plant
expect_clean "$(printf '<!-- lab-string-ok: benchmark provenance -->\nmeasured on gpu-host')"   # lab-string-ok: plant

# The marker with no reason is not a waiver.
expect_hit lab-host 'measured on gpu-host <!-- lab-string-ok: -->'   # lab-string-ok: plant
expect_hit lab-host 'measured on gpu-host # lab-string-ok:'          # lab-string-ok: plant

# A binary file is not scanned.
checks=$((checks + 1)); fresh
printf 'gpu-host\0\001\002' > "$t/blob.bin"                         # lab-string-ok: plant
run_gate
if [ "$rc" -ne 0 ]; then fail "binary file was scanned: $out"; fi

# ------------------------------------------------- export-ignored, not scanned

checks=$((checks + 1)); fresh
printf 'ignored.md export-ignore\n' > "$t/.gitattributes"
printf 'host gpu-host\n' > "$t/ignored.md"                          # lab-string-ok: plant
printf 'neutral text\n' > "$t/kept.md"
run_gate
if [ "$rc" -ne 0 ]; then fail "an export-ignored file was scanned: $out"; fi
case "$out" in *"ignored.md: strip-at-publish"*) ;; *) fail "export-ignored file not reported as strip-at-publish: $out" ;; esac

# The same file, not ignored, fails: the test above is not passing vacuously.
checks=$((checks + 1)); fresh
printf 'ignored.md export-ignore\n' > "$t/.gitattributes"
printf 'host gpu-host\n' > "$t/other.md"                            # lab-string-ok: plant
run_gate
if [ "$rc" -ne 1 ]; then fail "a published file with a fleet value passed: $out"; fi

# --------------------------------------------------------- the pattern list

checks=$((checks + 1)); fresh
printf 'neutral text\n' > "$t/doc.md"
printf 'no-such-class\tfoo\t# note\n' > "$tmp/bad.patterns"
git -C "$t" add -A
rc=0
RELEASABILITY_ROOT=$t RELEASABILITY_PATTERNS=$tmp/bad.patterns "$gate" > /dev/null 2>&1 || rc=$?
if [ "$rc" -ne 2 ]; then fail "an unknown class in the pattern list exited $rc, want 2"; fi

# ------------------------------------------------- the export-ignore list

# Paths the published tree must not hold: development tooling at any depth, and
# the forge-only workflow files. One definition, used by both checks below.
is_stripped() {
  case "$1" in
    CLAUDE.md|AGENTS.md|*/CLAUDE.md|*/AGENTS.md|.claude/*|.serena/*) return 0 ;;
    .github/workflows/automerge.yml|.github/workflows/qwen-review.yml) return 0 ;;
    .github/workflows/code-review.yml|.github/actionlint.yaml) return 0 ;;
  esac
  return 1
}

# git archive of a tree carrying this repository's .gitattributes holds none of
# them and keeps the public workflow beside them.
checks=$((checks + 1)); fresh
cp "$repo/.gitattributes" "$t/.gitattributes"
mkdir -p "$t/pkg" "$t/.claude/sub" "$t/.serena" "$t/.github/workflows"
for f in CLAUDE.md AGENTS.md pkg/CLAUDE.md pkg/AGENTS.md .claude/settings.json .claude/sub/x \
         .serena/project.yml .github/actionlint.yaml .github/workflows/automerge.yml \
         .github/workflows/qwen-review.yml .github/workflows/code-review.yml; do
  printf 'x\n' > "$t/$f"
done
for f in README.md pkg/main.c .github/workflows/ci.yml; do printf 'x\n' > "$t/$f"; done
git -C "$t" add -A
listing=$(git -C "$t" archive "$(git -C "$t" write-tree)" | tar -t)
while IFS= read -r f; do
  if is_stripped "$f"; then fail "git archive holds $f"; fi
done <<< "$listing"
for f in README.md pkg/main.c .github/workflows/ci.yml; do
  case $'\n'"$listing"$'\n' in *$'\n'"$f"$'\n'*) ;; *) fail "git archive lost the published file $f" ;; esac
done

# In this repository's own index: every stripped path is export-ignored, and
# nothing else is.
checks=$((checks + 1))
while IFS= read -r -d '' path && IFS= read -r -d '' _ && IFS= read -r -d '' value; do
  if is_stripped "$path"; then want=set; else want=unspecified; fi
  if [ "$value" != "$want" ]; then fail "$path: export-ignore is '$value', want '$want'"; fi
done < <(git -C "$repo" ls-files -z | git -C "$repo" check-attr -z --stdin export-ignore)

if [ "$failures" -ne 0 ]; then
  echo "releasability tests: $failures failure(s) in $checks checks"
  exit 1
fi
echo "releasability tests: $checks checks passed"
