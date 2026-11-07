#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Tests of the public-releasability gate (scripts/releasability.sh) and of the
# export-ignore list in .gitattributes. Run by `make releasability-test`, which
# `make ci` includes. There is no skip guard: a test that cannot run fails.
#
# No fleet's real values are written here. The gate runs against the public
# list in the tree and a private list this test writes, which holds reference
# stand-ins: RFC 5737 addresses, RFC 2606 names and core-01 style devices. The
# real private list is never read: every run names its private list. A plant
# that the public list would find carries an inline waiver, so the gate passes
# over this file.
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

# The stand-in private list: one line per class, in the format of the real one.
priv=$tmp/private.patterns
{
  printf 'lab-address\t%s\t%s\n' '(^|[^0-9.])192\.0\.2\.' '# lab-string-ok: stand-in fleet subnet (RFC 5737)'
  printf 'lab-host\t%s\t%s\n' '(^|[^A-Za-z0-9])core-0[0-9]' '# lab-string-ok: stand-in fleet device names'
  printf 'lab-host\t%s\t%s\n' 'forge\.example\.test' '# lab-string-ok: stand-in forge host (RFC 2606)'
  printf 'owner-path\t%s\t%s\n' '/(home|Users)/example([^A-Za-z0-9_]|$)' '# lab-string-ok: stand-in owner home'
  printf 'owner-account\t%s\t%s\n' '(^|[^A-Za-z0-9_./-])exampleowner($|[^A-Za-z0-9_/-])' '# lab-string-ok: stand-in owner account; module and org paths are not a hit'
} > "$priv"
# The private list each run_gate names; '' runs the public list alone.
use_priv=$priv

# fresh: a new throwaway git work tree, named by $t.
fresh() {
  n=$((n + 1))
  t=$tmp/w$n
  mkdir -p "$t"
  git -C "$t" init -q
}

# run_gate: the gate on $t; output in $out, status in $rc.
run_gate() {
  git -C "$t" add -A
  rc=0
  out=$(RELEASABILITY_ROOT=$t RELEASABILITY_PRIVATE_PATTERNS=$use_priv "$gate" 2>&1) || rc=$?
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

# From the private list.
expect_hit lab-address 'forge at http://192.0.2.7/o/r'              # lab-string-ok: plant
expect_hit lab-address 'endpoint 192.0.2.22:7020'                   # lab-string-ok: plant
expect_hit lab-host    'runs on core-01'                            # lab-string-ok: plant
expect_hit lab-host    'ssh core-07.example'                        # lab-string-ok: plant
expect_hit lab-host    'forge https://forge.example.test/o/r'       # lab-string-ok: plant
expect_hit owner-account 'ask exampleowner about it'                # lab-string-ok: plant
expect_hit owner-account 'owner `exampleowner`.'                    # lab-string-ok: plant
# From the public list.
expect_hit lab-host    'forge at http://git.example.internal/o/r'   # lab-string-ok: plant
expect_hit lab-host    'printer at printer.home.arpa'               # lab-string-ok: plant
expect_hit owner-path  'cd /home/example/sources'                   # lab-string-ok: plant
expect_hit owner-path  'built in /Users/someone/src'                # lab-string-ok: plant
expect_hit owner-path  'key at ~/.agent-identity/keys/agent.key'    # lab-string-ok: plant

# A hit on the second line is reported on the second line.
checks=$((checks + 1)); fresh
printf 'clean\nhost core-01\n' > "$t/doc.md"                        # lab-string-ok: plant
run_gate
case "$out" in *"doc.md:2: lab-host"*) ;; *) fail "second-line hit not reported as doc.md:2: $out" ;; esac

# ---------------------------------------------------------- what must pass

expect_clean 'documentation addresses 198.51.100.7 and 203.0.113.9'
expect_clean 'opaque RFC 1918 fixtures 192.168.1.20 and 10.0.0.5'
expect_clean 'a version 1192.0.2.1 is not the subnet'
expect_clean 'a hostname score-01 is not a device'
expect_clean 'names example.com, host.example, a.test and b.invalid'
expect_clean 'module github.com/exampleowner/trusted-firmware/sdk/go'
expect_clean 'see exampleowner/go-xkms and https://github.com/exampleowner/arduino-generic'
expect_clean 'build host, CI runner, bench host'
expect_clean 'a relative path build/host, and $HOME/src'
# Benchmark provenance and the other inline waivers: same line, or the line above.
expect_clean 'measured on core-01, 2026-09-14 <!-- lab-string-ok: benchmark provenance -->'   # lab-string-ok: plant
expect_clean "$(printf '<!-- lab-string-ok: benchmark provenance -->\nmeasured on core-01')"   # lab-string-ok: plant

# The marker with no reason is not a waiver.
expect_hit lab-host 'measured on core-01 <!-- lab-string-ok: -->'   # lab-string-ok: plant
expect_hit lab-host 'measured on core-01 # lab-string-ok:'          # lab-string-ok: plant

# A binary file is not scanned.
checks=$((checks + 1)); fresh
printf 'core-01\0\001\002' > "$t/blob.bin"                          # lab-string-ok: plant
run_gate
if [ "$rc" -ne 0 ]; then fail "binary file was scanned: $out"; fi

# ------------------------------------------------- export-ignored, not scanned

checks=$((checks + 1)); fresh
printf 'ignored.md export-ignore\n' > "$t/.gitattributes"
printf 'host core-01\n' > "$t/ignored.md"                           # lab-string-ok: plant
printf 'neutral text\n' > "$t/kept.md"
run_gate
if [ "$rc" -ne 0 ]; then fail "an export-ignored file was scanned: $out"; fi
case "$out" in *"ignored.md: strip-at-publish"*) ;; *) fail "export-ignored file not reported as strip-at-publish: $out" ;; esac

# The same file, not ignored, fails: the test above is not passing vacuously.
checks=$((checks + 1)); fresh
printf 'ignored.md export-ignore\n' > "$t/.gitattributes"
printf 'host core-01\n' > "$t/other.md"                             # lab-string-ok: plant
run_gate
if [ "$rc" -ne 1 ]; then fail "a published file with a fleet value passed: $out"; fi

# ------------------------------------------------------ the private list

# Without it, its values pass and the public list still holds.
use_priv=
expect_clean 'runs on core-01, endpoint 192.0.2.22:7020'
expect_hit owner-path 'cd /home/example/sources'                    # lab-string-ok: plant
checks=$((checks + 1)); fresh
printf 'neutral text\n' > "$t/doc.md"
run_gate
case "$out" in *"(the public pattern list only)"*) ;; *) fail "a public-only run does not say so: $out" ;; esac
use_priv=$priv

# gate_env VAR=VALUE...: the gate on $t, RELEASABILITY_PRIVATE_PATTERNS unset,
# with the environment given; output in $out, status in $rc.
gate_env() {
  git -C "$t" add -A
  rc=0
  out=$(env -u RELEASABILITY_PRIVATE_PATTERNS -u XDG_CONFIG_HOME "$@" RELEASABILITY_ROOT="$t" "$gate" 2>&1) || rc=$?
}
mkdir -p "$tmp/xdg/trusted-firmware" "$tmp/home/.config/trusted-firmware" "$tmp/empty"
cp "$priv" "$tmp/xdg/trusted-firmware/releasability.patterns"
cp "$priv" "$tmp/home/.config/trusted-firmware/releasability.patterns"

# Unset, it is read from $XDG_CONFIG_HOME/trusted-firmware/releasability.patterns...
checks=$((checks + 1)); fresh
printf 'runs on core-01\n' > "$t/doc.md"                            # lab-string-ok: plant
gate_env XDG_CONFIG_HOME="$tmp/xdg" HOME="$tmp/empty"
if [ "$rc" -ne 1 ]; then fail "the private list under XDG_CONFIG_HOME was not read (exit $rc): $out"; fi
case "$out" in *"doc.md:1: lab-host"*) ;; *) fail "no 'doc.md:1: lab-host' from the XDG_CONFIG_HOME list: $out" ;; esac

# ...else from $HOME/.config/trusted-firmware/releasability.patterns...
checks=$((checks + 1))
gate_env HOME="$tmp/home"
if [ "$rc" -ne 1 ]; then fail "the private list under HOME/.config was not read (exit $rc): $out"; fi

# ...and where there is none, the public list runs alone and passes.
checks=$((checks + 1))
gate_env XDG_CONFIG_HOME="$tmp/empty" HOME="$tmp/empty"
if [ "$rc" -ne 0 ]; then fail "no private list, yet the gate failed (exit $rc): $out"; fi

# Named but missing is a misconfiguration, not a pass.
checks=$((checks + 1)); fresh
printf 'neutral text\n' > "$t/doc.md"
use_priv=$tmp/no-such.patterns
run_gate
use_priv=$priv
if [ "$rc" -ne 2 ]; then fail "a missing private list exited $rc, want 2: $out"; fi

# --------------------------------------------------------- the pattern lists

checks=$((checks + 1)); fresh
printf 'neutral text\n' > "$t/doc.md"
printf 'no-such-class\tfoo\t# note\n' > "$tmp/bad.patterns"
git -C "$t" add -A
rc=0
RELEASABILITY_ROOT=$t RELEASABILITY_PATTERNS=$tmp/bad.patterns RELEASABILITY_PRIVATE_PATTERNS=$priv \
  "$gate" > /dev/null 2>&1 || rc=$?
if [ "$rc" -ne 2 ]; then fail "an unknown class in the public list exited $rc, want 2"; fi

checks=$((checks + 1))
rc=0
RELEASABILITY_ROOT=$t RELEASABILITY_PRIVATE_PATTERNS=$tmp/bad.patterns "$gate" > /dev/null 2>&1 || rc=$?
if [ "$rc" -ne 2 ]; then fail "an unknown class in the private list exited $rc, want 2"; fi

# The public list in this tree is clean against the stand-ins: it names no
# fleet value, and the tree passes it.
checks=$((checks + 1))
rc=0
out=$(RELEASABILITY_ROOT=$repo RELEASABILITY_PRIVATE_PATTERNS= "$gate" 2>&1) || rc=$?
if [ "$rc" -ne 0 ]; then fail "this tree fails the public list: $out"; fi

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
